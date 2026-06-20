#include "ui/sidebar/OutlinePanel.h"
#include "Logger.hpp"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFont>

OutlinePanel::OutlinePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 6, 2, 2);
    layout->setSpacing(2);

    auto* title = new QLabel(tr("大纲"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("sideFileTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(14);
    m_tree->setRootIsDecorated(true);
    m_tree->setSortingEnabled(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
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

    connect(m_tree, &QTreeWidget::itemClicked,
            this, &OutlinePanel::onItemClicked);
}

// ============================================================
// LSP 符号解析
// ============================================================

QString OutlinePanel::symbolIcon(int kind) const
{
    // LSP SymbolKind 映射到单字符图标
    // 1=File 2=Module 3=Namespace 4=Package 5=Class 6=Method 7=Property
    // 8=Field 9=Constructor 10=Enum 11=Interface 12=Function 13=Variable
    // 14=Constant 15=String 16=Number 17=Boolean 18=Array 19=Object
    // 20=Key 21=Null 22=EnumMember 23=Struct 24=Event 25=Operator 26=TypeParameter
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
    default: return QStringLiteral("•");
    }
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

void OutlinePanel::populateOutlineTreeFromList(QTreeWidgetItem* parent, const QList<QVariantMap>& symbols)
{
    for (const QVariantMap& sym : symbols) {
        QString name = sym.value(QStringLiteral("name")).toString();
        int kind = sym.value(QStringLiteral("kind")).toInt();
        if (name.isEmpty()) continue;

        int line = 0, col = 0;
        extractSymbolPosition(sym, line, col);

        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, symbolIcon(kind) + QStringLiteral(" ") + name);
        item->setToolTip(0, tr("行 %1 · 列 %2").arg(line + 1).arg(col + 1));
        // 存储跳转信息：UserRole=line, UserRole+1=col
        item->setData(0, Qt::UserRole, line);
        item->setData(0, Qt::UserRole + 1, col);

        // 递归处理子符号（DocumentSymbol 格式）
        QVariant childrenVar = sym.value(QStringLiteral("children"));
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
    m_filePath = filePath;
    m_tree->clear();

    if (symbols.isEmpty()) {
        if (m_hint) {
            m_hint->setText(tr("未获取到符号\n\n可能原因：\n• 当前文件无 LSP 支持\n• 文件为空"));
            m_hint->show();
        }
        return;
    }

    // 填充大纲树
    populateOutlineTreeFromList(nullptr, symbols);
    m_tree->expandToDepth(1);

    if (m_hint) m_hint->hide();

    LOG_DEBUG("[OutlinePanel] 大纲更新: " << symbols.size() << " 个顶层符号, file=" << filePath.toStdString());
}

void OutlinePanel::clearOutline()
{
    m_tree->clear();
    m_filePath.clear();
    if (m_hint) {
        m_hint->setText(tr("打开文件后显示符号大纲\n\n支持：\n• LSP 符号（精确）\n• 正则扫描（离线 fallback）"));
        m_hint->show();
    }
}

void OutlinePanel::updateOutlineFromText(const QString& filePath, const QString& content)
{
    // 离线正则扫描 fallback（无 LSP 时使用）
    m_filePath = filePath;
    m_tree->clear();

    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();

    // 简单正则匹配常见符号定义
    // C/C++: class/struct/enum/function
    // Python: class/def
    // JS/TS: function/class/const
    QList<QPair<QString, int>> entries;  // (显示文本, 行号)

    QStringList lines = content.split(QLatin1Char('\n'));
    QRegularExpression re;

    if (suffix == QStringLiteral("py")) {
        re.setPattern(QStringLiteral("^(\\s*)(class|def)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("js") || suffix == QStringLiteral("ts")) {
        re.setPattern(QStringLiteral("^(\\s*)(function|class|const|let|var)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("h") ||
               suffix == QStringLiteral("hpp") || suffix == QStringLiteral("cc") ||
               suffix == QStringLiteral("cxx") || suffix == QStringLiteral("c")) {
        // C/C++: class/struct/enum/函数声明（简化匹配）
        re.setPattern(QStringLiteral("^(\\s*)(class|struct|enum|namespace|void|int|bool|double|float|QString|auto|inline|static)\\s+(\\w+)"));
    } else if (suffix == QStringLiteral("md")) {
        // Markdown: 标题
        re.setPattern(QStringLiteral("^(#{1,6})\\s+(.+)$"));
    } else {
        // 不支持的语言
        if (m_hint) {
            m_hint->setText(tr("该文件类型不支持离线大纲\n\n支持：\n• C/C++ (.cpp/.h)\n• Python (.py)\n• JS/TS (.js/.ts)\n• Markdown (.md)"));
            m_hint->show();
        }
        return;
    }

    for (int i = 0; i < lines.size(); ++i) {
        auto m = re.match(lines[i]);
        if (m.hasMatch()) {
            QString indent = m.captured(1);
            QString keyword = m.captured(2);
            QString name = m.captured(3);
            QString icon = QStringLiteral("•");

            if (keyword == QStringLiteral("class") || keyword == QStringLiteral("struct"))
                icon = QStringLiteral("C");
            else if (keyword == QStringLiteral("def") || keyword == QStringLiteral("function"))
                icon = QStringLiteral("f");
            else if (keyword == QStringLiteral("enum"))
                icon = QStringLiteral("E");
            else if (keyword == QStringLiteral("namespace"))
                icon = QStringLiteral("N");
            else if (keyword.startsWith(QStringLiteral("#")))
                icon = QStringLiteral("H");

            QString text = icon + QStringLiteral(" ") + name;
            entries.append(qMakePair(text, i));
        }
    }

    if (entries.isEmpty()) {
        if (m_hint) {
            m_hint->setText(tr("未扫描到符号\n\n（离线正则扫描，结果可能不完整）"));
            m_hint->show();
        }
        return;
    }

    // 扁平添加（离线模式不构建层级）
    for (const auto& e : entries) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, e.first);
        item->setToolTip(0, tr("行 %1").arg(e.second + 1));
        item->setData(0, Qt::UserRole, e.second);  // 行号
        item->setData(0, Qt::UserRole + 1, 0);     // 列号
    }

    if (m_hint) m_hint->hide();
}

// ============================================================
// 槽函数
// ============================================================

void OutlinePanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;

    int line = item->data(0, Qt::UserRole).toInt();
    int col = item->data(0, Qt::UserRole + 1).toInt();

    if (m_filePath.isEmpty()) return;

    // 发射跳转信号（行列均为 0-based）
    emit symbolClicked(m_filePath, line, col);
}
