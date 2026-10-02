#include "ui/sidebar/OutlinePanel.h"
#include "Logger.hpp"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QSet>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFont>
#include <QColor>
#include <QBrush>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <QContextMenuEvent>
#include <QtConcurrent>

#include "core/config/ConfigManager.h"

// ============================================================
// 离线正则扫描辅助函数（在子线程中执行）
// ============================================================

// V2.1 M6 修复：单一数据源，消除 isOutlineSupported / scanSymbolsOffline / onAsyncScanFinished 三处重复
QSet<QString> OutlinePanel::supportedSuffixes()
{
    return {
        QStringLiteral("py"),
        QStringLiteral("js"), QStringLiteral("ts"),
        QStringLiteral("cpp"), QStringLiteral("h"),
        QStringLiteral("hpp"), QStringLiteral("cc"),
        QStringLiteral("cxx"), QStringLiteral("c"),
        QStringLiteral("md")
    };
}

/// @brief 离线正则扫描符号（在子线程中执行，避免大文件阻塞 UI）
/// @param filePath 文件路径（用于判断语言类型）
/// @param content 文件内容
/// @return 符号列表：(显示文本, 行号)
static QList<QPair<QString, int>> scanSymbolsOffline(const QString& filePath, const QString& content)
{
    QList<QPair<QString, int>> entries;

    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();

    QRegularExpression re;
    if (suffix == QStringLiteral("py")) {
        re.setPattern(QStringLiteral("^(\\s*)(class|def)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("js") || suffix == QStringLiteral("ts")) {
        re.setPattern(QStringLiteral("^(\\s*)(function|class|const|let|var)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("h") ||
               suffix == QStringLiteral("hpp") || suffix == QStringLiteral("cc") ||
               suffix == QStringLiteral("cxx") || suffix == QStringLiteral("c")) {
        re.setPattern(QStringLiteral("^(\\s*)(class|struct|enum|namespace|void|int|bool|double|float|QString|auto|inline|static)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("md")) {
        re.setPattern(QStringLiteral("^(#{1,6})\\s+(.+)$"));
    } else {
        return entries;  // 不支持的语言返回空列表
    }

    QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        auto m = re.match(lines[i]);
        if (m.hasMatch()) {
            QString keyword = m.captured(2);
            QString name = m.captured(3);

            // V2.1: 移除前置图标，仅保留纯文字符号名
            // 保留 keyword 作为前缀用于配色区分（离线模式无 LSP kind，用关键字推断）
            QString text = name;
            entries.append(qMakePair(text, i));
            Q_UNUSED(keyword)
        }
    }

    return entries;
}

// ============================================================
// 构造函数
// ============================================================

OutlinePanel::OutlinePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 2, 2);
    layout->setSpacing(2);

    // 顶部标题栏 + 筛选框
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    auto* title = new QLabel(tr("大纲"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    headerLayout->addWidget(title);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("筛选符号..."));
    m_filter->setObjectName(QStringLiteral("outlineFilter"));
    m_filter->setClearButtonEnabled(true);
    headerLayout->addWidget(m_filter, 1);

    layout->addLayout(headerLayout);

    // 大纲树
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("sideFileTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(14);
    m_tree->setRootIsDecorated(true);
    m_tree->setSortingEnabled(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    // 使用支持 emoji 的字体
    {
        QFont emojiFont = m_tree->font();
        emojiFont.setFamilies({QStringLiteral("Segoe UI Emoji"),
                               QStringLiteral("Apple Color Emoji"),
                               QStringLiteral("Noto Color Emoji")});
        m_tree->setFont(emojiFont);
    }
    layout->addWidget(m_tree);

    // 提示标签（无符号时显示）
    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("settingsHint"));
    m_hint->setWordWrap(true);
    m_hint->setText(tr("打开文件后显示符号大纲\n\n支持：\n• LSP 符号（精确）\n• 正则扫描（离线 fallback）"));
    m_hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_hint);

    // 信号连接
    // V2.1 H1 修复：仅连接 itemClicked，移除 itemDoubleClicked
    // 原因：Qt 双击序列为 click→click→doubleClick，两个信号连同一槽会触发 3 次跳转，
    //       且首次 click 可能触发文件切换导致后续点击命中失效 item
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &OutlinePanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemExpanded,
            this, &OutlinePanel::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemCollapsed,
            this, &OutlinePanel::onItemCollapsed);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &OutlinePanel::onCustomContextMenuRequested);
    connect(m_filter, &QLineEdit::textChanged,
            this, &OutlinePanel::onFilterChanged);

    // 异步扫描结果监听器
    m_scanWatcher = new QFutureWatcher<QList<QPair<QString, int>>>(this);
    connect(m_scanWatcher, &QFutureWatcher<QList<QPair<QString, int>>>::finished,
            this, &OutlinePanel::onAsyncScanFinished);

    // V2.1 M5 修复：筛选框防抖定时器（150ms），避免大符号树每次按键全量遍历
    m_filterDebounceTimer = new QTimer(this);
    m_filterDebounceTimer->setSingleShot(true);
    m_filterDebounceTimer->setInterval(150);
    connect(m_filterDebounceTimer, &QTimer::timeout, this, [this]() {
        applyFilter(m_pendingFilterText);
    });

    // V2.1 M3 修复：启动时恢复上次的折叠状态
    loadExpansionStatesFromDisk();
}

// ============================================================
// LSP 符号解析
// ============================================================

QString OutlinePanel::symbolIcon(int kind) const
{
    // LSP SymbolKind 映射到单字符图标
    switch (kind) {
    case 1:  return QString::fromUtf8("\xF0\x9F\x93\x84"); // 📄 File
    case 2:
    case 3:
    case 4:  return QString::fromUtf8("\xF0\x9F\x93\x81"); // 📁 Module/Namespace/Package
    case 5:  return QStringLiteral("C");  // Class
    case 6:  return QStringLiteral("M");  // Method
    case 7:
    case 8:  return QStringLiteral("F");  // Property/Field
    case 9:  return QStringLiteral("C");  // Constructor
    case 10: return QStringLiteral("E");  // Enum
    case 11: return QStringLiteral("I");  // Interface
    case 12: return QStringLiteral("f");  // Function
    case 13: return QStringLiteral("V");  // Variable
    case 14: return QStringLiteral("K");  // Constant
    case 22: return QStringLiteral("m");  // EnumMember
    case 23: return QStringLiteral("S");  // Struct
    case 24: return QStringLiteral("~");  // Event
    case 25: return QStringLiteral("O");  // Operator
    case 26: return QStringLiteral("T");  // TypeParameter
    default: return QStringLiteral("\xE2\x97\x8F");  // •
    }
}

QColor OutlinePanel::symbolColor(int kind) const
{
    // V2.0: 符号分类视觉区分（类蓝/函数绿/成员黄/枚举紫/宏橙）
    switch (kind) {
    case 5:  return QColor("#4EC9B0");   // Class — 蓝绿色（类）
    case 23: return QColor("#4EC9B0");   // Struct — 蓝绿色（结构体）
    case 11: return QColor("#B8D7A3");   // Interface — 浅绿
    case 6:  return QColor("#DCDCAA");   // Method — 黄色（方法）
    case 12: return QColor("#DCDCAA");   // Function — 黄色（函数）
    case 9:  return QColor("#DCDCAA");   // Constructor — 黄色（构造函数）
    case 7:
    case 8:  return QColor("#9CDCFE");   // Property/Field — 浅蓝（成员变量）
    case 13: return QColor("#9CDCFE");   // Variable — 浅蓝
    case 14: return QColor("#4FC1FF");   // Constant — 亮蓝
    case 10: return QColor("#C586C0");   // Enum — 紫色（枚举）
    case 22: return QColor("#C586C0");   // EnumMember — 紫色
    case 2:
    case 3:
    case 4:  return QColor("#C8C8C8");   // Module/Namespace/Package — 灰色
    case 24: return QColor("#E2C08D");   // Event — 橙色
    case 25: return QColor("#E2C08D");   // Operator — 橙色
    case 26: return QColor("#E2C08D");   // TypeParameter — 橙色
    default: return QColor("#D4D4D4");   // 默认前景色
    }
}

bool OutlinePanel::isMocGeneratedSymbol(const QString& name)
{
    // V2.1: Qt MOC 自动生成的元符号过滤规则
    // 这些符号由 Q_OBJECT 宏展开自动生成，非开发者手写业务代码，应从大纲中过滤
    static const QSet<QString> mocSymbols = {
        QStringLiteral("staticMetaObject"),
        QStringLiteral("metaObject"),
        QStringLiteral("qt_metacast"),
        QStringLiteral("qt_metacall"),
        QStringLiteral("qt_static_metacall"),
        QStringLiteral("tr"),
        QStringLiteral("trUtf8"),
        QStringLiteral("QPrivateSignal")
    };
    return mocSymbols.contains(name);
}

void OutlinePanel::extractSymbolPosition(const QVariantMap& sym, int& line, int& col) const
{
    line = 0;
    col = 0;
    // LSP documentSymbol 有两种格式：
    // 1. DocumentSymbol：有 selectionRange（符号名精确范围）
    // 2. SymbolInformation：有 location.range
    QVariantMap range;
    if (sym.contains(QStringLiteral("selectionRange"))) {
        range = sym.value(QStringLiteral("selectionRange")).toMap();
    } else if (sym.contains(QStringLiteral("location"))) {
        QVariantMap loc = sym.value(QStringLiteral("location")).toMap();
        range = loc.value(QStringLiteral("range")).toMap();
    } else if (sym.contains(QStringLiteral("range"))) {
        range = sym.value(QStringLiteral("range")).toMap();
    }

    if (range.contains(QStringLiteral("start"))) {
        QVariantMap start = range.value(QStringLiteral("start")).toMap();
        line = start.value(QStringLiteral("line")).toInt();
        col = start.value(QStringLiteral("character")).toInt();
    }
}

void OutlinePanel::extractSymbolEndPosition(const QVariantMap& sym, int& endLine, int& endCol) const
{
    // V2.1: 提取符号整体范围的结束位置（range.end，非 selectionRange.end）
    // 用于结构体/类完整作用域选中
    endLine = -1;
    endCol = -1;

    QVariantMap range;
    if (sym.contains(QStringLiteral("range"))) {
        range = sym.value(QStringLiteral("range")).toMap();
    } else if (sym.contains(QStringLiteral("location"))) {
        QVariantMap loc = sym.value(QStringLiteral("location")).toMap();
        range = loc.value(QStringLiteral("range")).toMap();
    }

    if (range.contains(QStringLiteral("end"))) {
        QVariantMap end = range.value(QStringLiteral("end")).toMap();
        endLine = end.value(QStringLiteral("line")).toInt();
        endCol = end.value(QStringLiteral("character")).toInt();
    }
}

bool OutlinePanel::isFlatSymbolList(const QList<QVariantMap>& symbols) const
{
    // V2.1: 检测是否为扁平结构（SymbolInformation 格式）
    // 扁平结构特征：有 location 字段，无 children 字段
    // 嵌套结构特征：有 selectionRange 字段，可能有 children 字段
    if (symbols.isEmpty()) return false;
    const QVariantMap& first = symbols.first();
    return first.contains(QStringLiteral("location")) && !first.contains(QStringLiteral("children"));
}

QList<QVariantMap> OutlinePanel::buildNestedFromFlat(const QList<QVariantMap>& flatSymbols) const
{
    // V2.1 安全修复：消除悬空指针 + O(n²) 复杂度
    //
    // 原实现问题：
    //   1. QHash<QString, QVariantMap*> 存储 QList 元素指针，QList reallocation 导致悬空
    //   2. 每次添加子节点复制整个 children 列表，O(n²) 复杂度
    //
    // 修复方案：
    //   1. 使用索引（QHash<QString, int>）替代裸指针，避免 reallocation 问题
    //   2. 使用 QVariantList 引用直接修改，避免反复复制
    //   3. 先收集所有符号，再统一构建父子关系，最后提取顶层

    // 预分配空间，避免 reallocation
    QList<QVariantMap> result;
    result.reserve(flatSymbols.size());

    // V2.1 H2 修复：用 name@line 作唯一键，name → indices 列表用于父节点查找
    // 原因：纯 name 作 key 时，同名符号（如两个 Foo 类）后者覆盖前者，
    //       导致子节点挂到错误的父节点下
    QHash<QString, int> uniqueNameToIndex;          // "name@line" → index
    QHash<QString, QList<int>> nameToIndices;       // name → [indices sorted by line]

    // 第一遍：收集所有符号，初始化 children 字段
    for (const QVariantMap& sym : flatSymbols) {
        QString name = sym.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;

        QVariantMap mutableSym = sym;
        mutableSym[QStringLiteral("children")] = QVariantList();

        // 提取 range 信息（扁平结构的 range 在 location 内）
        int startLine = 0;
        if (sym.contains(QStringLiteral("location"))) {
            QVariantMap loc = sym.value(QStringLiteral("location")).toMap();
            QVariantMap range = loc.value(QStringLiteral("range")).toMap();
            mutableSym[QStringLiteral("range")] = range;
            mutableSym[QStringLiteral("selectionRange")] = range;
            startLine = range.value(QStringLiteral("start")).toMap()
                            .value(QStringLiteral("line")).toInt();
        }

        int idx = result.size();
        result.append(mutableSym);

        // 唯一键：name@line（消除同名符号冲突）
        QString uniqueKey = QStringLiteral("%1@%2").arg(name).arg(startLine);
        uniqueNameToIndex[uniqueKey] = idx;
        nameToIndices[name].append(idx);
    }

    // 第二遍：根据 containerName 构建父子关系（"最近前驱"算法）
    // 使用 QSet 记录已挂载的子节点索引，最后提取未挂载的作为顶层
    QSet<int> childIndices;
    for (int i = 0; i < result.size(); ++i) {
        QString containerName = result[i].value(QStringLiteral("containerName")).toString();
        if (containerName.isEmpty()) continue;

        // containerName 可能是限定名（A::B），取最后一段
        QString parentName = containerName;
        int lastSep = parentName.lastIndexOf(QStringLiteral("::"));
        if (lastSep >= 0) {
            parentName = parentName.mid(lastSep + 2);
        }

        // 查找同名符号中最近的前驱（行号最大且小于当前符号的）
        auto it = nameToIndices.find(parentName);
        if (it == nameToIndices.end()) continue;

        // 获取当前符号的行号
        int childLine = 0;
        QVariantMap childRange = result[i].value(QStringLiteral("range")).toMap();
        QVariantMap childStart = childRange.value(QStringLiteral("start")).toMap();
        childLine = childStart.value(QStringLiteral("line")).toInt();

        int bestParentIdx = -1;
        int bestParentLine = -1;
        for (int candidateIdx : it.value()) {
            if (candidateIdx == i) continue;
            QVariantMap candRange = result[candidateIdx].value(QStringLiteral("range")).toMap();
            QVariantMap candStart = candRange.value(QStringLiteral("start")).toMap();
            int candLine = candStart.value(QStringLiteral("line")).toInt();
            // 父节点必须在子节点之前（行号更小），且选择最接近的
            if (candLine < childLine && candLine > bestParentLine) {
                bestParentLine = candLine;
                bestParentIdx = candidateIdx;
            }
        }

        if (bestParentIdx >= 0) {
            QVariantList children = result[bestParentIdx].value(QStringLiteral("children")).toList();
            children.append(result[i]);
            result[bestParentIdx].insert(QStringLiteral("children"), children);
            childIndices.insert(i);
        }
    }

    // 第三遍：提取顶层符号（未被挂载为子节点的）
    QList<QVariantMap> topLevel;
    topLevel.reserve(result.size() - childIndices.size());
    for (int i = 0; i < result.size(); ++i) {
        if (!childIndices.contains(i)) {
            topLevel.append(result[i]);
        }
    }

    LOG_DEBUG("[OutlinePanel] buildNestedFromFlat: 输入=" << flatSymbols.size()
              << " 顶层=" << topLevel.size()
              << " 子节点=" << childIndices.size());
    return topLevel;
}

void OutlinePanel::populateOutlineTreeFromList(QTreeWidgetItem* parent, const QList<QVariantMap>& symbols)
{
    LOG_DEBUG("[OutlinePanel] populateOutlineTreeFromList: parent=" << (void*)parent
              << " symbols.size=" << symbols.size());

    for (const QVariantMap& sym : symbols) {
        QString name = sym.value(QStringLiteral("name")).toString();
        int kind = sym.value(QStringLiteral("kind")).toInt();
        if (name.isEmpty()) continue;

        // V2.1: 过滤 Qt MOC 自动生成的元符号（staticMetaObject/qt_metacast/QPrivateSignal 等）
        if (isMocGeneratedSymbol(name)) {
            continue;
        }

        int line = 0, col = 0;
        extractSymbolPosition(sym, line, col);

        // V2.1: 提取符号结束位置（用于结构体/类完整作用域选中）
        int endLine = -1, endCol = -1;
        extractSymbolEndPosition(sym, endLine, endCol);

        // 诊断日志：打印每个符号的信息，包括是否有 children
        QVariant childrenVar = sym.value(QStringLiteral("children"));
        int childCount = childrenVar.isValid() ? childrenVar.toList().size() : 0;
        LOG_DEBUG("[OutlinePanel] 符号: name=" << name.toStdString()
                  << " kind=" << kind << " line=" << line
                  << " children=" << childCount
                  << " parent=" << (void*)parent);

        // V2.1 修复：parent 为 nullptr 时，QTreeWidgetItem(nullptr) 不会自动添加到 QTreeWidget
        // 必须手动调用 addTopLevelItem 将顶层符号挂到树上，否则 UI 空白
        QTreeWidgetItem* item = nullptr;
        if (parent) {
            item = new QTreeWidgetItem(parent);  // 自动添加为 parent 的子节点
        } else {
            item = new QTreeWidgetItem();  // 创建无父级 item
            m_tree->addTopLevelItem(item); // 手动添加到树顶层
        }
        item->setText(0, name);  // V2.1: 移除前置图标，仅保留纯文字符号名
        // V2.0: 符号分类着色（保留配色区分，不靠图标）
        QColor color = symbolColor(kind);
        if (color.isValid()) {
            item->setForeground(0, QBrush(color));
        }
        item->setToolTip(0, tr("行 %1 · 列 %2").arg(line + 1).arg(col + 1));
        // 存储跳转信息：UserRole=line, UserRole+1=col, UserRole+2=symbolName
        // V2.1: UserRole+3=endLine, UserRole+4=endCol（符号整体结束位置）
        item->setData(0, Qt::UserRole, line);
        item->setData(0, Qt::UserRole + 1, col);
        item->setData(0, Qt::UserRole + 2, name);
        item->setData(0, Qt::UserRole + 3, endLine);
        item->setData(0, Qt::UserRole + 4, endCol);

        // 递归处理子符号（DocumentSymbol 格式）
        if (childrenVar.isValid()) {
            QVariantList children = childrenVar.toList();
            if (!children.isEmpty()) {
                QList<QVariantMap> childMaps;
                for (const QVariant& c : children) {
                    childMaps.append(c.toMap());
                }
                populateOutlineTreeFromList(item, childMaps);
            }
        }
    }
}

// ============================================================
// 公共 API
// ============================================================

void OutlinePanel::updateOutline(const QString& filePath, const QList<QVariantMap>& symbols)
{
    // V2.1 C2 修复：LSP 符号到达时取消挂起的离线扫描，
    // 防止粗糙的正则结果覆盖精确的 LSP 结果
    if (m_scanWatcher->isRunning()) {
        m_scanWatcher->cancel();
    }

    // V2.1 C4 修复：无条件保存折叠状态（同文件刷新也需保存，
    // 否则 m_tree->clear() 会丢失用户手动折叠的节点，expandAll 后又全展开）
    saveExpansionState();

    m_filePath = filePath;
    m_tree->clear();

    // 清空筛选框（新文件不需要保留旧筛选）
    if (m_filter) {
        m_filter->blockSignals(true);
        m_filter->clear();
        m_filter->blockSignals(false);
    }

    if (symbols.isEmpty()) {
        if (m_hint) {
            m_hint->setText(tr("未获取到符号\n\n可能原因：\n• 当前文件无 LSP 支持\n• 文件为空"));
            m_hint->show();
        }
        return;
    }

    // 填充大纲树
    // V2.1: 兼容扁平结构（SymbolInformation）和嵌套结构（DocumentSymbol）
    QList<QVariantMap> nestedSymbols = symbols;
    if (isFlatSymbolList(symbols)) {
        LOG_DEBUG("[OutlinePanel] 检测到扁平结构（SymbolInformation），转换为嵌套结构");
        nestedSymbols = buildNestedFromFlat(symbols);
    }
    populateOutlineTreeFromList(nullptr, nestedSymbols);

    // V2.1: 默认展开所有节点（对标 VS Code：打开文件后大纲默认全部展开）
    m_tree->expandAll();

    // 恢复该文件的折叠状态
    restoreExpansionState();

    if (m_hint) m_hint->hide();

    LOG_DEBUG("[OutlinePanel] 大纲更新: " << symbols.size() << " 个顶层符号, file=" << filePath.toStdString());
}

void OutlinePanel::clearOutline()
{
    // 保存当前文件的折叠状态
    if (!m_filePath.isEmpty()) {
        saveExpansionState();
    }

    m_tree->clear();
    // V2.1 L4 修复：清理已关闭文件的折叠状态，避免长会话内存增长
    if (!m_filePath.isEmpty()) {
        m_expandedStates.remove(m_filePath);
    }
    m_filePath.clear();
    if (m_filter) {
        m_filter->blockSignals(true);
        m_filter->clear();
        m_filter->blockSignals(false);
    }
    if (m_hint) {
        m_hint->setText(tr("打开文件后显示符号大纲\n\n支持：\n• LSP 符号（精确）\n• 正则扫描（离线 fallback）"));
        m_hint->show();
    }
}

void OutlinePanel::resetFilePath(const QString& filePath)
{
    // V2.1: 立即同步 m_filePath，防止 LSP 异步窗口期点击大纲跳转到错误文件
    // 场景：切换文件 A→B，requestSymbols(B) 异步未返回，用户点击大纲中 A 的符号
    //       若 m_filePath 仍为 A，onItemClicked 会发射 symbolClicked(A,...) 导致跳转回 A
    // 修复：切换文件时立即调用 resetFilePath(B)，清空树并同步路径
    if (m_filePath != filePath) {
        saveExpansionState();
        m_filePath = filePath;
        m_tree->clear();
        if (m_hint) {
            m_hint->setText(tr("正在解析符号..."));
            m_hint->show();
        }
    }
}

void OutlinePanel::updateOutlineFromText(const QString& filePath, const QString& content)
{
    // V2.0: 离线正则扫描移入子线程，避免大文件阻塞 UI
    startAsyncScan(filePath, content);
}

// ============================================================
// 异步扫描
// ============================================================

void OutlinePanel::startAsyncScan(const QString& filePath, const QString& content)
{
    // 如果有正在进行的扫描，取消它（结果将被忽略）
    if (m_scanWatcher->isRunning()) {
        m_scanWatcher->cancel();
        // 注意：cancel 不会立即停止，但结果会被忽略（通过 m_pendingScanFilePath 校验）
    }

    // V2.1 C4 修复：无条件保存折叠状态（同文件编辑后异步扫描也需保存）
    saveExpansionState();

    m_pendingScanFilePath = filePath;
    m_filePath = filePath;

    // 显示扫描中提示
    if (m_hint) {
        m_hint->setText(tr("正在扫描符号..."));
        m_hint->show();
    }

    // 在子线程中执行正则扫描
    QFuture<QList<QPair<QString, int>>> future = QtConcurrent::run(
        scanSymbolsOffline, filePath, content);
    m_scanWatcher->setFuture(future);
}

void OutlinePanel::onAsyncScanFinished()
{
    // V2.1 L1 修复：cancel 后的 future 不应调用 result()（会抛 QException）
    if (m_scanWatcher->isCanceled()) {
        return;
    }

    // 防止 stale 结果：如果用户已切换到其他文件，忽略旧结果
    if (m_pendingScanFilePath != m_filePath) {
        return;
    }

    QList<QPair<QString, int>> entries = m_scanWatcher->result();

    // 检查文件是否支持离线扫描
    QFileInfo fi(m_pendingScanFilePath);
    QString suffix = fi.suffix().toLower();
    if (!supportedSuffixes().contains(suffix)) {
        if (m_hint) {
            m_hint->setText(tr("该文件类型不支持离线大纲\n\n支持：\n• C/C++ (.cpp/.h)\n• Python (.py)\n• JS/TS (.js/.ts)\n• Markdown (.md)"));
            m_hint->show();
        }
        return;
    }

    if (entries.isEmpty()) {
        if (m_hint) {
            m_hint->setText(tr("未扫描到符号\n\n（离线正则扫描，结果可能不完整）"));
            m_hint->show();
        }
        return;
    }

    // 清空筛选框
    if (m_filter) {
        m_filter->blockSignals(true);
        m_filter->clear();
        m_filter->blockSignals(false);
    }

    m_tree->clear();

    // 扁平添加（离线模式不构建层级）
    for (const auto& e : entries) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, e.first);  // V2.1: 已是纯文字符号名，无图标前缀
        item->setToolTip(0, tr("行 %1").arg(e.second + 1));
        item->setData(0, Qt::UserRole, e.second);  // 行号
        item->setData(0, Qt::UserRole + 1, 0);     // 列号
        item->setData(0, Qt::UserRole + 2, e.first);  // 符号名
    }

    if (m_hint) m_hint->hide();

    LOG_DEBUG("[OutlinePanel] 异步扫描完成: " << entries.size() << " 个符号, file=" << m_pendingScanFilePath.toStdString());

    // V2.1: 通知父级 ExplorerPanel 扫描完成（用于自动展开大纲区域）
    emit scanFinished(m_pendingScanFilePath, entries.size());
}

// ============================================================
// 筛选过滤
// ============================================================

void OutlinePanel::onFilterChanged(const QString& text)
{
    // V2.1 M5 修复：防抖 150ms，避免每次按键触发全量树遍历
    m_pendingFilterText = text.trimmed();
    m_filterDebounceTimer->start();
}

void OutlinePanel::applyFilter(const QString& text)
{
    if (text.isEmpty()) {
        // 清空筛选：显示所有节点
        QTreeWidgetItemIterator it(m_tree);
        while (*it) {
            (*it)->setHidden(false);
            ++it;
        }
        // V2.1 M4 修复：清空筛选后恢复用户保存的折叠状态
        // 原因：filterItem 在子节点匹配时会 setExpanded(true)，清空筛选后这些节点仍保持展开
        restoreExpansionState();
        return;
    }

    // 递归过滤：从顶层节点开始
    int topLevelCount = m_tree->topLevelItemCount();
    for (int i = 0; i < topLevelCount; ++i) {
        filterItem(m_tree->topLevelItem(i), text);
    }
}

bool OutlinePanel::filterItem(QTreeWidgetItem* item, const QString& text)
{
    QString name = item->data(0, Qt::UserRole + 2).toString();
    if (name.isEmpty()) {
        name = item->text(0);
    }

    bool selfMatch = name.contains(text, Qt::CaseInsensitive);

    // 递归检查子节点
    bool childMatch = false;
    int childCount = item->childCount();
    for (int i = 0; i < childCount; ++i) {
        if (filterItem(item->child(i), text)) {
            childMatch = true;
        }
    }

    // 显示条件：自身匹配 或 有子节点匹配
    bool shouldShow = selfMatch || childMatch;
    item->setHidden(!shouldShow);

    // 如果有子节点匹配，展开当前节点以显示匹配的子节点
    if (childMatch && !selfMatch) {
        item->setExpanded(true);
    }

    return shouldShow;
}

// ============================================================
// 折叠状态持久化
// ============================================================

void OutlinePanel::saveExpansionState()
{
    if (m_filePath.isEmpty()) return;

    QSet<QString> expandedNames;
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if ((*it)->isExpanded()) {
            // V2.1 H3 修复：用 name@line 作唯一键，消除同名符号折叠状态共享
            QString name = (*it)->data(0, Qt::UserRole + 2).toString();
            if (name.isEmpty()) {
                name = (*it)->text(0);
            }
            int line = (*it)->data(0, Qt::UserRole).toInt();
            QString uniqueKey = QStringLiteral("%1@%2").arg(name).arg(line);
            expandedNames.insert(uniqueKey);
        }
        ++it;
    }

    m_expandedStates[m_filePath] = expandedNames;
}

void OutlinePanel::restoreExpansionState()
{
    if (m_filePath.isEmpty()) return;

    auto it = m_expandedStates.find(m_filePath);
    if (it == m_expandedStates.end()) {
        // 没有保存过状态，默认展开到第 1 层
        m_tree->expandToDepth(1);
        return;
    }

    const QSet<QString>& expandedNames = it.value();
    QTreeWidgetItemIterator treeIt(m_tree);
    while (*treeIt) {
        QString name = (*treeIt)->data(0, Qt::UserRole + 2).toString();
        if (name.isEmpty()) {
            name = (*treeIt)->text(0);
        }
        int line = (*treeIt)->data(0, Qt::UserRole).toInt();
        QString uniqueKey = QStringLiteral("%1@%2").arg(name).arg(line);
        (*treeIt)->setExpanded(expandedNames.contains(uniqueKey));
        ++treeIt;
    }
}

void OutlinePanel::collapseAll()
{
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        (*it)->setExpanded(false);
        ++it;
    }
}

void OutlinePanel::expandAll()
{
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        (*it)->setExpanded(true);
        ++it;
    }
}

// ============================================================
// 右键菜单
// ============================================================

void OutlinePanel::onCustomContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) return;

    // V2.1 安全修复：按值捕获所需数据，避免裸指针在嵌套事件循环中被释放（Use-After-Free）
    // menu.exec() 会启动嵌套事件循环，期间 LSP 响应/文件切换可能触发 m_tree->clear() 删除 item
    const int itemLine = item->data(0, Qt::UserRole).toInt();
    const int itemCol = item->data(0, Qt::UserRole + 1).toInt();
    const QString itemName = item->data(0, Qt::UserRole + 2).toString();
    const int itemEndLine = item->data(0, Qt::UserRole + 3).toInt();
    const int itemEndCol = item->data(0, Qt::UserRole + 4).toInt();

    QMenu menu(this);

    // 跳转到定义
    QAction* jumpAction = menu.addAction(tr("跳转到定义"));
    connect(jumpAction, &QAction::triggered, this, [this, itemLine, itemCol, itemEndLine, itemEndCol]() {
        if (m_filePath.isEmpty()) return;
        emit symbolClicked(m_filePath, itemLine, itemCol, itemEndLine, itemEndCol);
    });

    // 复制符号名
    QAction* copyAction = menu.addAction(tr("复制符号名"));
    connect(copyAction, &QAction::triggered, this, [itemName]() {
        QApplication::clipboard()->setText(itemName);
    });

    menu.addSeparator();

    // 折叠全部
    QAction* collapseAction = menu.addAction(tr("折叠全部"));
    connect(collapseAction, &QAction::triggered, this, &OutlinePanel::collapseAll);

    // 展开全部
    QAction* expandAction = menu.addAction(tr("展开全部"));
    connect(expandAction, &QAction::triggered, this, &OutlinePanel::expandAll);

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// ============================================================
// 槽函数
// ============================================================

void OutlinePanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) {
        LOG_DEBUG("[OutlinePanel] onItemClicked: item 为空，忽略");
        return;
    }

    int line = item->data(0, Qt::UserRole).toInt();
    int col = item->data(0, Qt::UserRole + 1).toInt();
    QString name = item->data(0, Qt::UserRole + 2).toString();
    int endLine = item->data(0, Qt::UserRole + 3).toInt();
    int endCol = item->data(0, Qt::UserRole + 4).toInt();

    LOG_DEBUG("[OutlinePanel] 节点点击: name=" << name.toStdString()
              << " line=" << line << " col=" << col
              << " endLine=" << endLine << " endCol=" << endCol
              << " filePath=" << m_filePath.toStdString());

    if (m_filePath.isEmpty()) {
        LOG_DEBUG("[OutlinePanel] onItemClicked: m_filePath 为空，无法跳转");
        return;
    }

    // V2.1: 发射跳转信号（含结束位置，支持结构体/类完整作用域选中）
    emit symbolClicked(m_filePath, line, col, endLine, endCol);
    LOG_DEBUG("[OutlinePanel] 已发射 symbolClicked 信号");
}

void OutlinePanel::onItemExpanded(QTreeWidgetItem* item)
{
    Q_UNUSED(item)
    // 折叠状态在 saveExpansionState 时统一保存
}

void OutlinePanel::onItemCollapsed(QTreeWidgetItem* item)
{
    Q_UNUSED(item)
    // 折叠状态在 saveExpansionState 时统一保存
}

// ============================================================
// V2.1 M3 修复：折叠状态持久化到磁盘（QSettings）
// ============================================================

void OutlinePanel::saveExpansionStatesToDisk()
{
    // 先保存当前文件的状态
    if (!m_filePath.isEmpty()) {
        saveExpansionState();
    }

    // 将 m_expandedStates 序列化到 ConfigManager
    // 格式：filePath1:name@line,name@line;filePath2:name@line,...
    QStringList fileEntries;
    for (auto it = m_expandedStates.begin(); it != m_expandedStates.end(); ++it) {
        const QString& filePath = it.key();
        const QSet<QString>& names = it.value();
        if (names.isEmpty()) continue;
        QStringList nameList(names.begin(), names.end());
        fileEntries.append(filePath + QStringLiteral(":") + nameList.join(QStringLiteral(",")));
    }
    ConfigManager::instance().setValue(
        QStringLiteral("outline/expansionStates"),
        fileEntries.join(QStringLiteral(";")));
}

void OutlinePanel::loadExpansionStatesFromDisk()
{
    QString saved = ConfigManager::instance().getValue(
        QStringLiteral("outline/expansionStates")).toString();
    if (saved.isEmpty()) return;

    // 反序列化
    const QStringList fileEntries = saved.split(QStringLiteral(";"), Qt::SkipEmptyParts);
    for (const QString& entry : fileEntries) {
        int colonIdx = entry.indexOf(QStringLiteral(":"));
        if (colonIdx < 0) continue;
        QString filePath = entry.left(colonIdx);
        QStringList nameList = entry.mid(colonIdx + 1).split(QStringLiteral(","), Qt::SkipEmptyParts);
        if (nameList.isEmpty()) continue;
        m_expandedStates[filePath] = QSet<QString>(nameList.begin(), nameList.end());
    }
}
