#include "ui/sidebar/ExplorerPanel.h"
#include "ui/sidebar/OutlinePanel.h"
#include "Logger.hpp"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QDropEvent>
#include <QEvent>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QSet>
#include <QToolButton>
#include <QSplitter>
#include <QLocale>          // P3-M05: 文件修改时间本地化格式
#include <QDateTime>        // P3-M05: 文件修改时间格式化
#include <QByteArray>

#include "core/config/ConfigManager.h"

ExplorerPanel::ExplorerPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* explorerLayout = new QVBoxLayout(this);
    explorerLayout->setContentsMargins(4, 6, 2, 2);
    explorerLayout->setSpacing(2);

    // 面板标题行
    m_panelTitle = new QLabel(tr("资源管理器"), this);
    m_panelTitle->setObjectName(QStringLiteral("panelTitle"));
    explorerLayout->addWidget(m_panelTitle);

    // 工具栏行（新建/刷新/折叠 + 路径显示）
    m_explorerHeader = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(m_explorerHeader);
    headerLayout->setContentsMargins(0, 1, 4, 1);
    headerLayout->setSpacing(2);

    m_btnNewFile = new QPushButton(QStringLiteral("+"), m_explorerHeader);
    m_btnNewFile->setToolTip(tr("新建文件"));
    m_btnNewFile->setFixedSize(22, 20);
    m_btnNewFile->setProperty("iconButton", true);
    m_btnNewFile->setFont(QFont("Segoe UI", 11, QFont::Bold));

    m_btnOpenFolder = new QPushButton(QString::fromUtf8("\xF0\x9F\x81\x81"), m_explorerHeader);  // 📁
    m_btnOpenFolder->setToolTip(tr("打开文件夹"));
    m_btnOpenFolder->setFixedSize(22, 20);
    m_btnOpenFolder->setProperty("iconButton", true);
    m_btnOpenFolder->setFont(QFont("Segoe UI", 10, QFont::Bold));

    m_btnRefresh = new QPushButton(QStringLiteral("\u21BB"), m_explorerHeader);
    m_btnRefresh->setToolTip(tr("刷新文件列表"));
    m_btnRefresh->setFixedSize(22, 20);
    m_btnRefresh->setProperty("iconButton", true);

    m_btnCollapseAll = new QPushButton(QStringLiteral("\u2261"), m_explorerHeader);
    m_btnCollapseAll->setToolTip(tr("折叠全部"));
    m_btnCollapseAll->setFixedSize(22, 20);
    m_btnCollapseAll->setProperty("iconButton", true);

    m_pathLabel = new QLabel(m_explorerHeader);
    m_pathLabel->setObjectName(QStringLiteral("pathLabel"));
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathLabel->setWordWrap(false);

    headerLayout->addWidget(m_btnNewFile);
    headerLayout->addWidget(m_btnOpenFolder);
    headerLayout->addWidget(m_btnRefresh);
    headerLayout->addWidget(m_btnCollapseAll);
    headerLayout->addWidget(m_pathLabel, 1); // stretch

    explorerLayout->addWidget(m_explorerHeader);

    m_fileTree = new QTreeWidget(this);
    m_fileTree->setObjectName(QStringLiteral("sideFileTree"));
    m_fileTree->setHeaderHidden(true);
    m_fileTree->setAnimated(true);
    m_fileTree->setIndentation(16);
    m_fileTree->setRootIsDecorated(true);
    m_fileTree->setSortingEnabled(false);
    // V1.9: 启用文件树内部拖拽（用于移动文件）
    m_fileTree->setDragEnabled(true);
    m_fileTree->setAcceptDrops(true);
    m_fileTree->setDropIndicatorShown(true);
    m_fileTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_fileTree->viewport()->installEventFilter(this);  // 拦截 viewport 的 drop 事件
    // 强制使用支持 emoji 的字体，确保文件图标正确渲染
    {
        QFont emojiFont = m_fileTree->font();
        emojiFont.setFamilies({QStringLiteral("Segoe UI Emoji"),
                               QStringLiteral("Apple Color Emoji"),
                               QStringLiteral("Noto Color Emoji")});
        m_fileTree->setFont(emojiFont);
    }
    // 注意：m_fileTree 暂不加入 layout，稍后加入 QSplitter

    // === V2.1: 大纲区域（嵌入文件树下方，VSCode 风格）===
    // 布局：[可点击标题栏(▼/▶ 大纲)] + [OutlinePanel 符号树]
    m_outlineContainer = new QWidget(this);
    m_outlineContainer->setObjectName(QStringLiteral("outlineContainer"));
    auto* outlineLayout = new QVBoxLayout(m_outlineContainer);
    outlineLayout->setContentsMargins(0, 4, 0, 0);
    outlineLayout->setSpacing(0);

    // 可点击的标题栏（点击切换折叠/展开）
    m_outlineHeader = new QWidget(m_outlineContainer);
    m_outlineHeader->setObjectName(QStringLiteral("outlineHeader"));
    m_outlineHeader->setCursor(Qt::PointingHandCursor);
    m_outlineHeader->setFixedHeight(22);
    auto* outlineHeaderLayout = new QHBoxLayout(m_outlineHeader);
    outlineHeaderLayout->setContentsMargins(6, 0, 6, 0);
    outlineHeaderLayout->setSpacing(4);

    m_outlineArrow = new QToolButton(m_outlineHeader);
    m_outlineArrow->setObjectName(QStringLiteral("outlineArrow"));
    m_outlineArrow->setArrowType(Qt::RightArrow);  // 默认折叠 → 右箭头
    m_outlineArrow->setFixedSize(16, 16);
    m_outlineArrow->setStyleSheet(QStringLiteral("QToolButton{border:none;background:transparent;}"));
    m_outlineTitle = new QLabel(tr("大纲"), m_outlineHeader);
    m_outlineTitle->setObjectName(QStringLiteral("panelTitle"));
    outlineHeaderLayout->addWidget(m_outlineArrow);
    outlineHeaderLayout->addWidget(m_outlineTitle);
    outlineHeaderLayout->addStretch();

    outlineLayout->addWidget(m_outlineHeader);

    // 符号树组件（复用现有 OutlinePanel，所有能力保留）
    m_outlineSection = new OutlinePanel(m_outlineContainer);
    m_outlineSection->setObjectName(QStringLiteral("outlineSection"));
    outlineLayout->addWidget(m_outlineSection);
    m_outlineSection->hide();  // 默认折叠

    // === V2.1: 内容分割器（文件树 / 大纲区域可拖拽调节高度，对齐 VSCode）===
    m_contentSplitter = new QSplitter(Qt::Vertical, this);
    m_contentSplitter->setObjectName(QStringLiteral("contentSplitter"));
    m_contentSplitter->setChildrenCollapsible(false);  // 防止拖到 0 高度
    m_contentSplitter->setHandleWidth(4);              // 分割线宽度 4px
    m_contentSplitter->addWidget(m_fileTree);          // 上：文件树
    m_contentSplitter->addWidget(m_outlineContainer);  // 下：大纲区域
    // 初始比例：文件树占满，大纲折叠（高度 0）
    m_contentSplitter->setSizes({999, 0});
    // 文件树最小高度 80px，大纲区域最小高度 60px（标题栏 22 + 符号树至少 38）
    m_contentSplitter->setStretchFactor(0, 1);  // 文件树可拉伸
    m_contentSplitter->setStretchFactor(1, 0);  // 大纲区域默认不拉伸

    explorerLayout->addWidget(m_contentSplitter);

    // === 信号连接 ===
    connect(m_fileTree, &QTreeWidget::itemDoubleClicked,
            this, &ExplorerPanel::onItemDoubleClicked);
    connect(m_fileTree, &QTreeWidget::itemClicked,
            this, &ExplorerPanel::onItemClicked);

    // 右键上下文菜单
    m_fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileTree, &QTreeWidget::customContextMenuRequested,
            this, &ExplorerPanel::onContextMenu);

    // 工具栏按钮
    connect(m_btnNewFile, &QPushButton::clicked,
            this, &ExplorerPanel::onNewFile);
    connect(m_btnOpenFolder, &QPushButton::clicked,
            this, &ExplorerPanel::onOpenFolderClicked);
    connect(m_btnRefresh, &QPushButton::clicked,
            this, &ExplorerPanel::onRefresh);
    connect(m_btnCollapseAll, &QPushButton::clicked,
            this, &ExplorerPanel::onCollapseAll);

    // V2.1: 大纲标题栏点击 → 切换折叠/展开
    m_outlineHeader->installEventFilter(this);
    // V2.1: 大纲符号点击 → 转发给 SideBar/Widget 跳转
    connect(m_outlineSection, &OutlinePanel::symbolClicked,
            this, &ExplorerPanel::outlineSymbolClicked);
    // V2.1: 离线扫描完成 → 自动展开大纲区域（若扫描到符号）
    // V2.1 H4 修复：用户手动折叠过则不自动展开
    connect(m_outlineSection, &OutlinePanel::scanFinished,
            this, [this](const QString&, int symbolCount) {
        if (symbolCount > 0 && !m_userCollapsedOutline) {
            setOutlineExpanded(true);
        }
    });
    // V2.1 M2 修复：启动时恢复上次的 splitter 高度比例
    loadState();
}

void ExplorerPanel::setWorkspaceFolders(const QStringList& folders)
{
    m_workspaceFolders = folders;
    refreshFileList();
}

// ============================================================
// P2-H04: 多文件夹工作区 — 按路径增删根文件夹
// ============================================================

bool ExplorerPanel::addFolderToWorkspace(const QString& folder)
{
    if (folder.isEmpty()) return false;
    QString abs = QDir(folder).absolutePath();
    if (m_workspaceFolders.contains(abs)) {
        return false;
    }
    m_workspaceFolders.append(abs);
    refreshFileList();
    LOG_DEBUG("[ExplorerPanel] 添加文件夹到工作区: " << abs);
    return true;
}

bool ExplorerPanel::removeFolderFromWorkspace(const QString& folder)
{
    if (folder.isEmpty()) return false;
    QString abs = QDir(folder).absolutePath();
    if (!m_workspaceFolders.contains(abs)) {
        return false;
    }
    m_workspaceFolders.removeAll(abs);
    refreshFileList();
    LOG_DEBUG("[ExplorerPanel] 从工作区移除文件夹: " << abs);
    return true;
}

// ============================================================
// 文件树逻辑
// ============================================================

QString ExplorerPanel::fileIcon(const QString& suffix) const
{
    // 单字符 emoji 前缀，配合 QSS 中 font-family: "Segoe UI Emoji" 确保正确渲染
    if (suffix == QStringLiteral("py"))   return QString::fromUtf8("\xF0\x9F\x90\x8D"); // 🐍
    if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("cc") || suffix == QStringLiteral("cxx"))
        return QString::fromUtf8("\xF0\x9F\x94\xA7");                             // 🔧 C++
    if (suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp"))
        return QString::fromUtf8("\xF0\x9F\x93\x8B");                             // 📋 头文件
    if (suffix == QStringLiteral("md"))   return QString::fromUtf8("\xF0\x9F\x93\x96");// 📖
    if (suffix == QStringLiteral("json")) return QString::fromUtf8("\xF0\x9F\x93\xA6");// 📦 JSON
    if (suffix == QStringLiteral("txt"))  return QString::fromUtf8("\xF0\x9F\x93\x84");// 📄
    if (suffix == QStringLiteral("js"))   return QString::fromUtf8("\xF0\x9F\x94\xA5");// 🔥 JS
    if (suffix == QStringLiteral("ts"))   return QString::fromUtf8("\xF0\x9F\x92\x99");// 💙 TS
    if (suffix == QStringLiteral("html")) return QString::fromUtf8("\xF0\x9F\x8C\x90");// 🌐 HTML
    if (suffix == QStringLiteral("css"))  return QString::fromUtf8("\xF0\x9F\x8E\xA8");// 🎨 CSS
    if (suffix == QStringLiteral("qss"))  return QString::fromUtf8("\xE2\x9C\xA8");// ✨ QSS
    if (suffix == QStringLiteral("cmake"))return QString::fromUtf8("\xF0\x9F\x94\xA8");// 🚀 CMake
    if (suffix == QStringLiteral("pro"))  return QString::fromUtf8("\xF0\x9F\x93\xBB");// 📻 Qt
    if (suffix == QStringLiteral("go"))   return QString::fromUtf8("\xF0\x9F\x90\x98");// 🐘 Go
    if (suffix == QStringLiteral("java")) return QString::fromUtf8("\xE2\x98\x95"); // ☕ Java
    if (suffix == QStringLiteral("rs"))   return QString::fromUtf8("\xE2\x99\xBB"); // ♻ Rust
    if (suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml"))
        return QString::fromUtf8("\xF0\x9F\x93\x9D");                            // 📝 YAML
    if (suffix == QStringLiteral("ini"))  return QString::fromUtf8("\xE2\x9A\x99");// ⚙ INI
    return QString();
}

void ExplorerPanel::refreshFileList()
{
    m_fileTree->clear();

    // VSCode风格：没有打开文件夹时显示提示，不自动加载默认目录
    if (m_workspaceFolders.isEmpty() && m_remoteMounts.isEmpty()) {
        m_pathLabel->setText(tr("（未打开文件夹）"));
        m_pathLabel->setToolTip(QString());

        // 显示提示项
        auto* hintItem = new QTreeWidgetItem(m_fileTree);
        hintItem->setText(0, QString::fromUtf8("  \xF0\x9F\x81\x81  ") + tr("点击上方按钮打开文件夹"));
        hintItem->setData(0, Qt::UserRole, QString());
        hintItem->setData(0, Qt::UserRole + 1, QStringLiteral("hint"));
        hintItem->setFlags(hintItem->flags() & ~Qt::ItemIsSelectable);
        hintItem->setForeground(0, QColor(128, 128, 128));
        return;
    }

    // V1.9: 多文件夹工作区 — 每个文件夹作为根节点
    for (int i = 0; i < m_workspaceFolders.size(); ++i) {
        const QString& folderPath = m_workspaceFolders[i];
        QDir dir(folderPath);
        if (!dir.exists()) {
            LOG_DEBUG("[ExplorerPanel] 工作区文件夹不存在:" << folderPath);
            continue;
        }

        // 单文件夹时不显示根节点（直接展开内容），多文件夹时显示根节点
        if (m_workspaceFolders.size() == 1 && m_remoteMounts.isEmpty()) {
            // 单文件夹模式：直接填充（兼容旧行为）
            populateFileTree(nullptr, dir);
        } else {
            // 多文件夹模式：每个文件夹作为根节点
            QTreeWidgetItem* rootItem = new QTreeWidgetItem(m_fileTree);
            rootItem->setText(0, QString::fromUtf8("\xF0\x9F\x81\x81 ") + dir.dirName());
            rootItem->setData(0, Qt::UserRole, folderPath);
            rootItem->setData(0, Qt::UserRole + 1, QStringLiteral("workspaceRoot"));
            rootItem->setData(0, Qt::UserRole + 2, i);  // 工作区索引
            rootItem->setToolTip(0, folderPath);
            rootItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            populateFileTree(rootItem, dir);
            rootItem->setExpanded(true);
        }
    }

    // P3-M01 子项4: 显示远程挂载点（云图标前缀）
    for (auto it = m_remoteMounts.constBegin(); it != m_remoteMounts.constEnd(); ++it) {
        const QString& mountPoint = it.key();
        const QString& sessionName = it.value();
        QDir dir(mountPoint);
        if (!dir.exists()) {
            LOG_DEBUG("[ExplorerPanel] 远程挂载点不存在:" << mountPoint);
            continue;
        }

        // 云图标 + 会话名/挂载点名
        QString displayName = QString::fromUtf8("\xE2\x98\x81 ") +  // ☁
                              (sessionName.isEmpty() ? dir.dirName() : sessionName);
        QTreeWidgetItem* rootItem = new QTreeWidgetItem(m_fileTree);
        rootItem->setText(0, displayName);
        rootItem->setData(0, Qt::UserRole, mountPoint);
        rootItem->setData(0, Qt::UserRole + 1, QStringLiteral("remoteMount"));
        rootItem->setData(0, Qt::UserRole + 2, sessionName);  // 会话名
        rootItem->setToolTip(0, tr("远程挂载: %1\n会话: %2").arg(mountPoint, sessionName));
        rootItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        populateFileTree(rootItem, dir);
        rootItem->setExpanded(true);
    }

    m_fileTree->expandAll();

    // 更新路径显示
    int totalRoots = m_workspaceFolders.size() + m_remoteMounts.size();
    if (totalRoots == 1 && m_remoteMounts.isEmpty()) {
        QString displayPath = QDir(m_workspaceFolders.first()).absolutePath();
        if (displayPath.length() > 40) {
            displayPath = QStringLiteral("...") + displayPath.right(37);
        }
        m_pathLabel->setText(displayPath);
        m_pathLabel->setToolTip(QDir(m_workspaceFolders.first()).absolutePath());
    } else if (totalRoots == 1 && m_workspaceFolders.isEmpty()) {
        m_pathLabel->setText(tr("远程: %1").arg(m_remoteMounts.constBegin().value()));
        m_pathLabel->setToolTip(m_remoteMounts.constBegin().key());
    } else {
        m_pathLabel->setText(tr("工作区 (%1 本地 + %2 远程)")
                                 .arg(m_workspaceFolders.size())
                                 .arg(m_remoteMounts.size()));
        m_pathLabel->setToolTip(m_workspaceFolders.join(QStringLiteral("\n")) +
                                QStringLiteral("\n[Remote] ") + m_remoteMounts.keys().join(QStringLiteral("\n[Remote] ")));
    }

    LOG_DEBUG("[ExplorerPanel] 文件树加载完成: " << m_workspaceFolders.size()
              << " 本地 + " << m_remoteMounts.size() << " 远程");
}

// ============================================================
// P3-M01 子项4: 远程挂载点管理
// ============================================================

void ExplorerPanel::addRemoteMount(const QString& mountPoint, const QString& sessionName)
{
    if (mountPoint.isEmpty()) return;
    QString mp = QDir(mountPoint).absolutePath();
    m_remoteMounts[mp] = sessionName;
    refreshFileList();
    LOG_INFO("[ExplorerPanel] 添加远程挂载点: " << mp.toStdString()
             << " (session=" << sessionName.toStdString() << ")");
}

void ExplorerPanel::removeRemoteMount(const QString& mountPoint)
{
    if (mountPoint.isEmpty()) return;
    QString mp = QDir(mountPoint).absolutePath();
    if (m_remoteMounts.remove(mp) > 0) {
        refreshFileList();
        LOG_INFO("[ExplorerPanel] 移除远程挂载点: " << mp.toStdString());
    }
}

void ExplorerPanel::populateFileTree(QTreeWidgetItem* parentItem, const QDir& dir)
{
    // 先添加子目录
    QFileInfoList dirEntries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : dirEntries) {
        QTreeWidgetItem* dirItem = parentItem
            ? new QTreeWidgetItem(parentItem)
            : new QTreeWidgetItem(m_fileTree);
        dirItem->setText(0, QStringLiteral("\u25B6 ") + fi.fileName());
        dirItem->setData(0, Qt::UserRole, fi.absoluteFilePath());
        dirItem->setData(0, Qt::UserRole + 1, QStringLiteral("dir"));
        dirItem->setToolTip(0, fi.absoluteFilePath());
        dirItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);

        // 递归填充子目录
        populateFileTree(dirItem, QDir(fi.absoluteFilePath()));
    }

    // 再添加文件
    QFileInfoList fileEntries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : fileEntries) {
        QTreeWidgetItem* fileItem = parentItem
            ? new QTreeWidgetItem(parentItem)
            : new QTreeWidgetItem(m_fileTree);
        QString suffix = fi.suffix().toLower();
        fileItem->setText(0, fileIcon(suffix) + fi.fileName());
        fileItem->setData(0, Qt::UserRole, fi.absoluteFilePath());
        fileItem->setData(0, Qt::UserRole + 1, QStringLiteral("file"));
        // P3-M05: 文件 tooltip 显示路径 + 修改时间 + 大小（使用本地化格式）
        // 通过 QLocale::currentLocale() 适配中英文环境下的日期/数字呈现
        QString modTime = QLocale().toString(
            fi.lastModified(), QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        QString sizeStr = QLocale().toString(fi.size());
        fileItem->setToolTip(0,
            QStringLiteral("%1\n%2: %3\n%4: %5")
                .arg(fi.absoluteFilePath(),
                     tr("修改时间"), modTime,
                     tr("大小"), sizeStr));
    }
}

QTreeWidgetItem* ExplorerPanel::findTreeItemByPath(QTreeWidgetItem* parent, const QString& filePath) const
{
    int count = parent ? parent->childCount() : m_fileTree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem* item = parent ? parent->child(i) : m_fileTree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == filePath) {
            return item;
        }
        if (item->childCount() > 0) {
            QTreeWidgetItem* found = findTreeItemByPath(item, filePath);
            if (found) return found;
        }
    }
    return nullptr;
}

void ExplorerPanel::selectFileByPath(const QString& filePath)
{
    QTreeWidgetItem* item = findTreeItemByPath(nullptr, filePath);
    if (item) {
        m_fileTree->setCurrentItem(item);
        m_fileTree->scrollToItem(item);
        // 展开父节点
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            parent->setExpanded(true);
            parent = parent->parent();
        }
    } else {
        m_fileTree->clearSelection();
    }
}

// ============================================================
// 槽函数
// ============================================================

void ExplorerPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;
    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type == QStringLiteral("dir") || type == QStringLiteral("workspaceRoot")) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        emit fileOpenRequested(filePath);
    }
}

void ExplorerPanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;
    // 单击文件夹时切换展开/折叠状态
    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type == QStringLiteral("dir") || type == QStringLiteral("workspaceRoot")) {
        item->setExpanded(!item->isExpanded());
    }
}

void ExplorerPanel::onNewFile()
{
    emit fileCreateRequested();
}

void ExplorerPanel::onRefresh()
{
    refreshFileList();
}

void ExplorerPanel::onCollapseAll()
{
    m_fileTree->collapseAll();
}

void ExplorerPanel::onOpenFolderClicked()
{
    // 仅发射信号，QFileDialog 由 SideBar 处理（工作区状态归 SideBar 管理）
    emit openFolderClicked();
}

void ExplorerPanel::onContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_fileTree->itemAt(pos);
    QMenu menu(this);

    if (item) {
        QString type = item->data(0, Qt::UserRole + 1).toString();
        QString filePath = item->data(0, Qt::UserRole).toString();

        // 文件/文件夹通用操作
        QAction* actCopyPath = menu.addAction(tr("复制路径"));
        connect(actCopyPath, &QAction::triggered, this, [filePath]() {
            QApplication::clipboard()->setText(filePath);
        });

        if (type == QStringLiteral("file")) {
            // 文件特有操作
            QAction* actOpen = menu.addAction(tr("在编辑器中打开"));
            connect(actOpen, &QAction::triggered, this, [this, filePath]() {
                emit fileOpenRequested(filePath);
            });

            menu.addSeparator();

            QAction* actRename = menu.addAction(tr("重命名"));
            connect(actRename, &QAction::triggered, this, [this, filePath]() {
                emit fileRenameRequested(filePath);
            });

            QAction* actDelete = menu.addAction(tr("删除"));
            connect(actDelete, &QAction::triggered, this, [this, filePath]() {
                emit fileDeleteRequested(filePath);
            });
        } else if (type == QStringLiteral("dir")) {
            // 文件夹特有操作
            QAction* actOpenFolder = menu.addAction(tr("在文件管理器中打开"));
            connect(actOpenFolder, &QAction::triggered, this, [this, filePath]() {
                emit openInFolderRequested(filePath);
            });
        } else if (type == QStringLiteral("workspaceRoot")) {
            // V1.9: 工作区根节点特有操作
            int wsIndex = item->data(0, Qt::UserRole + 2).toInt();

            QAction* actOpenFolder = menu.addAction(tr("在文件管理器中打开"));
            connect(actOpenFolder, &QAction::triggered, this, [this, filePath]() {
                emit openInFolderRequested(filePath);
            });

            menu.addSeparator();

            QAction* actRemove = menu.addAction(tr("从工作区移除"));
            connect(actRemove, &QAction::triggered, this, [this, wsIndex]() {
                emit removeWorkspaceFolderRequested(wsIndex);
            });
        }

        menu.addSeparator();
    }

    // 始终可用的操作
    QAction* actNew = menu.addAction(tr("新建文件..."));
    connect(actNew, &QAction::triggered, this, &ExplorerPanel::fileCreateRequested);

    // V1.9: 新建文件夹
    QAction* actNewFolder = menu.addAction(tr("新建文件夹..."));
    connect(actNewFolder, &QAction::triggered, this, &ExplorerPanel::folderCreateRequested);

    menu.addSeparator();

    // V1.9: 添加文件夹到工作区
    QAction* actAddFolder = menu.addAction(tr("添加文件夹到工作区..."));
    connect(actAddFolder, &QAction::triggered, this, [this]() {
        emit addFolderToWorkspaceRequested();
    });

    QAction* actRefresh = menu.addAction(tr("刷新文件列表"));
    connect(actRefresh, &QAction::triggered, this, &ExplorerPanel::refreshFileList);

    menu.exec(m_fileTree->mapToGlobal(pos));
}

// ============================================================
// V1.9: 文件树拖拽移动文件
// ============================================================

bool ExplorerPanel::eventFilter(QObject* obj, QEvent* event)
{
    // 拦截文件树 viewport 的 Drop 事件，自定义移动逻辑
    if (obj == m_fileTree->viewport() && event->type() == QEvent::Drop) {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        if (handleTreeDropEvent(dropEvent)) {
            return true;  // 事件已处理，阻止 QTreeWidget 默认行为（默认会移动 item）
        }
    }
    // V2.1: 拦截大纲标题栏的鼠标点击 → 切换折叠/展开
    if (obj == m_outlineHeader && event->type() == QEvent::MouseButtonPress) {
        onOutlineHeaderClicked();
        return true;  // 阻止事件传播
    }
    return QWidget::eventFilter(obj, event);
}

bool ExplorerPanel::handleTreeDropEvent(QDropEvent* event)
{
    // 获取拖拽源（被拖拽的 item）
    QTreeWidgetItem* sourceItem = m_fileTree->currentItem();
    if (!sourceItem) return false;

    QString sourcePath = sourceItem->data(0, Qt::UserRole).toString();
    QString sourceType = sourceItem->data(0, Qt::UserRole + 1).toString();
    if (sourcePath.isEmpty() || sourceType != QStringLiteral("file")) {
        // 仅支持文件拖拽移动（文件夹移动复杂，暂不支持）
        event->ignore();
        return false;
    }

    // 获取放置目标
    QTreeWidgetItem* targetItem = m_fileTree->itemAt(event->position().toPoint());
    QString targetDir;

    if (targetItem) {
        QString targetType = targetItem->data(0, Qt::UserRole + 1).toString();
        if (targetType == QStringLiteral("dir")) {
            targetDir = targetItem->data(0, Qt::UserRole).toString();
        } else if (targetType == QStringLiteral("file")) {
            // 拖到文件上 → 使用其所在目录
            QString targetPath = targetItem->data(0, Qt::UserRole).toString();
            targetDir = QFileInfo(targetPath).absolutePath();
        }
    } else {
        // 拖到空白处 → 工作目录根（使用第一个工作区文件夹）
        if (!m_workspaceFolders.isEmpty()) {
            targetDir = m_workspaceFolders.first();
        }
    }

    if (targetDir.isEmpty()) {
        event->ignore();
        return false;
    }

    // 防止拖到自身所在目录（无意义操作）
    QString sourceDir = QFileInfo(sourcePath).absolutePath();
    if (QDir(sourceDir) == QDir(targetDir)) {
        event->ignore();
        return false;
    }

    // 防止拖到子目录（避免循环，简单起见不允许）
    QString targetAbs = QDir(targetDir).absolutePath();
    QString sourceAbs = QFileInfo(sourcePath).absoluteFilePath();
    if (sourceAbs.startsWith(targetAbs + QStringLiteral("/"))) {
        event->ignore();
        return false;
    }

    // 发射信号交由 SideBar/Widget 层处理实际移动
    emit fileMoveRequested(sourcePath, targetDir);
    event->accept();
    return true;
}

// ============================================================
// V2.1: 大纲区域（嵌入文件树下方，VSCode 风格）
// ============================================================

bool ExplorerPanel::isOutlineSupported(const QString& filePath)
{
    // V2.1 M6 修复：使用 OutlinePanel::supportedSuffixes() 单一数据源，消除重复
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    return OutlinePanel::supportedSuffixes().contains(suffix);
}

void ExplorerPanel::updateOutline(const QString& filePath, const QList<QVariantMap>& symbols)
{
    // V2.1: 更新大纲符号（LSP documentSymbol 响应）
    if (!m_outlineSection) return;

    // 仅支持大纲的文件类型才展开显示
    bool supported = isOutlineSupported(filePath);
    if (!supported) {
        setOutlineExpanded(false);
        m_outlineSection->clearOutline();
        return;
    }

    m_outlineSection->updateOutline(filePath, symbols);

    // V2.1 H4 修复：有符号数据时自动展开，但用户手动折叠过则尊重用户意图
    if (!symbols.isEmpty() && !m_userCollapsedOutline) {
        setOutlineExpanded(true);
    }
}

void ExplorerPanel::updateOutlineFromText(const QString& filePath, const QString& content)
{
    // V2.1: 离线正则扫描更新大纲（无 LSP 时的 fallback）
    if (!m_outlineSection) return;

    bool supported = isOutlineSupported(filePath);
    if (!supported) {
        setOutlineExpanded(false);
        m_outlineSection->clearOutline();
        return;
    }

    // V2.1 H4 修复：乐观展开仅当用户未手动折叠时
    if (!m_userCollapsedOutline) {
        setOutlineExpanded(true);
    }
    m_outlineSection->updateOutlineFromText(filePath, content);
}

void ExplorerPanel::clearOutline()
{
    // V2.1: 清空大纲并折叠（文件关闭时调用）
    if (m_outlineSection) {
        m_outlineSection->clearOutline();
    }
    setOutlineExpanded(false);
}

void ExplorerPanel::resetOutlineFilePath(const QString& filePath)
{
    // V2.1 C3 修复：LSP 异步请求期间立即同步文件路径，防止点击大纲跳转到错误文件
    // 场景：用户切换 A→B 文件，LSP 还在请求 A 的符号，此时 m_filePath 仍为 A，
    //       若用户在 B 文件期间点击残留的 A 符号节点，会跳转到 A 的位置。
    // 此方法在发起 LSP 请求前同步路径，并清空树（消除残留符号）。
    if (m_outlineSection) {
        m_outlineSection->resetFilePath(filePath);
    }
}

void ExplorerPanel::setOutlineExpanded(bool expanded)
{
    // V2.1: 切换大纲区域折叠/展开状态
    if (m_outlineExpanded == expanded) return;
    m_outlineExpanded = expanded;

    if (m_outlineSection) {
        m_outlineSection->setVisible(expanded);
    }
    updateOutlineHeaderArrow();

    // V2.1: 通过 QSplitter 调整文件树/大纲区域的高度比例
    if (m_contentSplitter) {
        int totalH = m_contentSplitter->height();
        if (totalH <= 0) totalH = 400;  // 首次展开时 splitter 尚未显示，用默认值
        if (expanded) {
            // 展开：文件树 55% : 大纲 45%（最小 150px）
            int outlineH = qMax(150, static_cast<int>(totalH * 0.45));
            int treeH = qMax(80, totalH - outlineH);
            m_contentSplitter->setSizes({treeH, outlineH});
        } else {
            // 折叠：文件树占满，大纲高度 0（标题栏仍可见）
            m_contentSplitter->setSizes({totalH, 0});
        }
    }
}

void ExplorerPanel::updateOutlineHeaderArrow()
{
    // V2.1: 更新标题栏箭头方向（Qt 原生矢量箭头，跨字体兼容）
    if (m_outlineArrow) {
        m_outlineArrow->setArrowType(m_outlineExpanded
            ? Qt::DownArrow    // ▼ 展开
            : Qt::RightArrow); // ▶ 折叠
    }
}

void ExplorerPanel::onOutlineHeaderClicked()
{
    // V2.1: 标题栏点击 → 切换折叠/展开
    setOutlineExpanded(!m_outlineExpanded);
    // V2.1 H4 修复：记录用户手动操作
    // - 折叠：设置标志，后续符号更新不再自动展开
    // - 展开：清除标志，恢复自动展开行为
    m_userCollapsedOutline = !m_outlineExpanded;
}

// ============================================================
// V2.1 M2 修复：splitter 高度比例持久化到磁盘
// ============================================================

void ExplorerPanel::saveState()
{
    // 保存 splitter 高度比例到 ConfigManager（QSettings）
    if (m_contentSplitter) {
        QByteArray sizes = m_contentSplitter->saveState();
        ConfigManager::instance().setValue(
            QStringLiteral("explorer/splitterSizes"),
            QString::fromLatin1(sizes.toBase64()));
    }
}

void ExplorerPanel::loadState()
{
    // 恢复 splitter 高度比例
    if (!m_contentSplitter) return;

    QString saved = ConfigManager::instance().getValue(
        QStringLiteral("explorer/splitterSizes")).toString();
    if (saved.isEmpty()) return;

    QByteArray sizes = QByteArray::fromBase64(saved.toLatin1());
    m_contentSplitter->restoreState(sizes);
}

void ExplorerPanel::saveOutlineState()
{
    // V2.1 M3: 透传给 OutlinePanel 保存折叠状态
    if (m_outlineSection) {
        m_outlineSection->saveExpansionStatesToDisk();
    }
}
