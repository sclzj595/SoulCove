#include "ui/sidebar/SideBar.h"
#include "Logger.hpp"
#include "ui/sidebar/GitPanel.h"
#include "ui/sidebar/OutlinePanel.h"  // V1.9: 大纲面板（已抽出）
#include "ui/sidebar/TasksPanel.h"    // M15: 任务面板（已抽出）
#include "ui/sidebar/SearchPanel.h"   // 搜索面板（已抽出）

#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDebug>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QFileDialog>
#include <QTextCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <QEvent>

SideBar::SideBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sideBar"));
    // 不再固定宽度，由外层 QSplitter 控制拖拽调整
    setMinimumWidth(160);
    setMaximumWidth(600);   // 允许拖到很宽

    // === 主布局：图标栏(48px) + 内容面板 ===
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    // === 活动图标栏（左侧窄条）===
    m_activityBar = new QWidget(this);
    m_activityBar->setObjectName(QStringLiteral("activityBar"));
    m_activityBar->setFixedWidth(48);

    m_activityLayout = new QVBoxLayout(m_activityBar);
    m_activityLayout->setContentsMargins(4, 8, 4, 8);
    m_activityLayout->setSpacing(2);

    // 使用 QString::fromUtf8() 显式指定 UTF-8 编码，确保 Windows/Qt 正确显示 emoji
    m_btnExplorer   = createActivityBtn(QString::fromUtf8("\xF0\x9F\x81\x81"), Activity::Explorer,   tr("资源管理器"));  // 📁
    m_btnSearch     = createActivityBtn(QString::fromUtf8("\xF0\x9F\x94\x8D"), Activity::Search,     tr("搜索"));         // 🔍
    m_btnGit        = createActivityBtn(QString::fromUtf8("\xE2\x9C\x8D"), Activity::Git,        tr("源代码管理"));    // ✍
    m_btnTasks     = createActivityBtn(QString::fromUtf8("\xE2\x9A\x92"), Activity::Tasks,      tr("任务"));          // ⚙ (任务图标)
    m_btnOutline   = createActivityBtn(QString::fromUtf8("\xE2\x99\xA0"), Activity::Outline,    tr("大纲"));          // ♠ (大纲图标，类似 VSCode 的横线图标)
    m_btnExtensions = createActivityBtn(QString::fromUtf8("\xE2\xAC\xA2"), Activity::Extensions, tr("扩展"));          // ⬢

    // 终端按钮（独立动作按钮，不切换面板，直接触发信号）
    m_btnTerminal = new QPushButton(QString::fromUtf8("\xE2\x96\xBA"), m_activityBar);  // ▶ (终端图标)
    m_btnTerminal->setFixedSize(40, 40);
    m_btnTerminal->setCursor(Qt::PointingHandCursor);
    m_btnTerminal->setObjectName(QStringLiteral("activityBtn"));
    m_btnTerminal->setToolTip(tr("切换终端 (Ctrl+`)"));
    m_btnTerminal->setCheckable(true);
    connect(m_btnTerminal, &QPushButton::clicked, this, &SideBar::onTerminalClicked);

    m_btnExplorer->setChecked(true);
    m_btnExplorer->setProperty("active", true);

    m_activityLayout->addWidget(m_btnExplorer);
    m_activityLayout->addWidget(m_btnSearch);
    m_activityLayout->addWidget(m_btnGit);
    m_activityLayout->addWidget(m_btnTasks);       // M15: 任务按钮
    m_activityLayout->addWidget(m_btnOutline);     // V1.9: 大纲按钮
    m_activityLayout->addWidget(m_btnExtensions);
    m_activityLayout->addWidget(m_btnTerminal);
    m_activityLayout->addStretch();

    // === 右侧面板堆栈 ===
    m_panelWidget = new QWidget(this);
    m_panelWidget->setObjectName(QStringLiteral("sidePanel"));
    auto* panelOuterLayout = new QVBoxLayout(m_panelWidget);
    panelOuterLayout->setContentsMargins(0, 0, 0, 0);
    panelOuterLayout->setSpacing(0);

    m_panelStack = new QStackedWidget(m_panelWidget);

    // --- Explorer 面板 ---
    m_explorerPanel = new QWidget();
    auto* explorerLayout = new QVBoxLayout(m_explorerPanel);
    explorerLayout->setContentsMargins(4, 6, 2, 2);
    explorerLayout->setSpacing(2);

    // 面板标题行
    m_panelTitle = new QLabel(tr("资源管理器"), m_explorerPanel);
    m_panelTitle->setObjectName(QStringLiteral("panelTitle"));
    explorerLayout->addWidget(m_panelTitle);

    // 工具栏行（新建/刷新/折叠 + 路径显示）
    m_explorerHeader = new QWidget(m_explorerPanel);
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

    m_fileTree = new QTreeWidget(m_explorerPanel);
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
    explorerLayout->addWidget(m_fileTree);

    m_panelStack->addWidget(m_explorerPanel);

    // --- Search 面板（已抽出为 SearchPanel）---
    m_searchPanel = new SearchPanel(this);
    // 转发搜索面板的信号到 SideBar 的信号（供 Widget 连接）
    connect(m_searchPanel, &SearchPanel::fileOpenRequested,
            this, &SideBar::fileOpenRequested);
    connect(m_searchPanel, &SearchPanel::locateRequested,
            this, [this](const QString& filePath, int line, int col) {
        // 搜索结果的定位复用 outlineSymbolClicked 信号（Widget 层已连接跳转逻辑）
        emit outlineSymbolClicked(filePath, line, col);
    });
    m_panelStack->addWidget(m_searchPanel);

    // --- Git 面板（源代码管理）---
    m_gitPanel = new QWidget();
    auto* gitLayout = new QVBoxLayout(m_gitPanel);
    gitLayout->setContentsMargins(0, 0, 0, 0);
    gitLayout->setSpacing(0);

    m_gitPanelWidget = new GitPanel(m_gitPanel);
    gitLayout->addWidget(m_gitPanelWidget);

    m_panelStack->addWidget(m_gitPanel);

    // --- Tasks 面板（M15: 任务系统，已抽出为 TasksPanel）---
    m_tasksPanel = new TasksPanel(this);
    m_panelStack->addWidget(m_tasksPanel);

    // --- Extensions 面板 ---
    m_extensionsPanel = new QWidget();
    auto* extLayout = new QVBoxLayout(m_extensionsPanel);
    extLayout->setContentsMargins(8, 8, 4, 4);
    extLayout->setSpacing(4);

    auto* extTitle = new QLabel(tr("扩展"), m_extensionsPanel);
    extTitle->setObjectName(QStringLiteral("panelTitle"));
    extLayout->addWidget(extTitle);

    auto* extHint = new QLabel(tr("暂无已安装扩展\n\n未来可集成：\n• 终端 (Terminal)\n• Git 集成\n• 代码片段"), m_extensionsPanel);
    extHint->setObjectName(QStringLiteral("settingsHint"));
    extHint->setWordWrap(true);
    extLayout->addWidget(extHint);
    extLayout->addStretch();

    m_panelStack->addWidget(m_extensionsPanel);

    // --- Outline 面板（V1.9: 大纲/符号导航，已抽出为 OutlinePanel）---
    m_outlinePanel = new OutlinePanel(this);
    // 转发面板的符号点击信号为 SideBar 的 outlineSymbolClicked 信号
    connect(m_outlinePanel, &OutlinePanel::symbolClicked,
            this, [this](const QString& filePath, int line, int col) {
        emit outlineSymbolClicked(filePath, line, col);
    });
    m_panelStack->addWidget(m_outlinePanel);

    panelOuterLayout->addWidget(m_panelStack);

    // 填充文件树
    refreshFileList();

    // === 组装 ===
    m_mainLayout->addWidget(m_activityBar);
    m_mainLayout->addWidget(m_panelWidget);

    // 信号连接
    connect(m_fileTree, &QTreeWidget::itemDoubleClicked,
            this, &SideBar::onFileItemDoubleClicked);
    connect(m_fileTree, &QTreeWidget::itemClicked,
            this, &SideBar::onFileItemClicked);

    // 右键上下文菜单
    m_fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileTree, &QTreeWidget::customContextMenuRequested,
            this, &SideBar::onFileContextMenu);

    // 注：Search 面板的信号连接由 SearchPanel 内部处理，
    //     SideBar 构造时已转发 fileOpenRequested/locateRequested 信号

    // Explorer 工具栏按钮
    connect(m_btnNewFile, &QPushButton::clicked,
            this, &SideBar::onExplorerNewFile);
    connect(m_btnOpenFolder, &QPushButton::clicked,
            this, &SideBar::onExplorerOpenFolder);
    connect(m_btnRefresh, &QPushButton::clicked,
            this, &SideBar::onExplorerRefresh);
    connect(m_btnCollapseAll, &QPushButton::clicked,
            this, &SideBar::onExplorerCollapseAll);

    // 监听主题切换
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &SideBar::refreshActivityStyles);

    // 注：Tasks 面板的信号连接由 TasksPanel 内部处理（直连 TaskManager 单例）
    // 注：大纲树点击信号由 OutlinePanel 内部处理并发射 symbolClicked，
    //     SideBar 构造时已连接转发到 outlineSymbolClicked
}

QPushButton* SideBar::createActivityBtn(const QString& iconText, Activity activity, const QString& tooltip)
{
    auto* btn = new QPushButton(iconText, m_activityBar);
    btn->setFixedSize(40, 40);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setCheckable(true);
    btn->setToolTip(tooltip);
    btn->setProperty("activity", static_cast<int>(activity));
    // 使用支持 emoji 的字体（Windows: Segoe UI Emoji / macOS: Apple Color Emoji）
    QFont emojiFont = btn->font();
    emojiFont.setFamilies({QStringLiteral("Segoe UI Emoji"), QStringLiteral("Apple Color Emoji"), QStringLiteral("Noto Color Emoji")});
    btn->setFont(emojiFont);

    connect(btn, &QPushButton::clicked, this, &SideBar::onActivityButtonClicked);
    return btn;
}

void SideBar::switchToActivity(Activity activity)
{
    m_currentActivity = activity;

    for (auto* btn : {m_btnExplorer, m_btnSearch, m_btnGit, m_btnTasks, m_btnOutline, m_btnExtensions}) {
        bool isTarget = (btn->property("activity").toInt() == static_cast<int>(activity));
        btn->setChecked(isTarget);
        btn->setProperty("active", isTarget);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }

    // 切换面板堆栈
    switch (activity) {
    case Activity::Explorer:   m_panelStack->setCurrentWidget(m_explorerPanel);   break;
    case Activity::Search:     m_panelStack->setCurrentWidget(m_searchPanel);     break;
    case Activity::Git:        m_panelStack->setCurrentWidget(m_gitPanel);        break;
    case Activity::Tasks:     m_panelStack->setCurrentWidget(m_tasksPanel);      break;
    case Activity::Outline:   m_panelStack->setCurrentWidget(m_outlinePanel);    break;
    case Activity::Extensions: m_panelStack->setCurrentWidget(m_extensionsPanel); break;
    }
}

void SideBar::setPanelWidth(int width)
{
    setFixedWidth(width);
    m_panelWidget->setMinimumWidth(width - 50);
}

QString SideBar::fileIcon(const QString& suffix) const
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

void SideBar::refreshFileList()
{
    m_fileTree->clear();

    // VSCode风格：没有打开文件夹时显示提示，不自动加载默认目录
    if (m_workspaceFolders.isEmpty()) {
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
            LOG_DEBUG("[SideBar] 工作区文件夹不存在:" << folderPath);
            continue;
        }

        // 单文件夹时不显示根节点（直接展开内容），多文件夹时显示根节点
        if (m_workspaceFolders.size() == 1) {
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

    m_fileTree->expandAll();

    // 更新路径显示
    if (m_workspaceFolders.size() == 1) {
        QString displayPath = QDir(m_workspaceFolders.first()).absolutePath();
        if (displayPath.length() > 40) {
            displayPath = QStringLiteral("...") + displayPath.right(37);
        }
        m_pathLabel->setText(displayPath);
        m_pathLabel->setToolTip(QDir(m_workspaceFolders.first()).absolutePath());
    } else {
        m_pathLabel->setText(tr("工作区 (%1 个文件夹)").arg(m_workspaceFolders.size()));
        m_pathLabel->setToolTip(m_workspaceFolders.join(QStringLiteral("\n")));
    }

    LOG_DEBUG("[SideBar] 文件树加载完成: " << m_workspaceFolders.size() << " 个文件夹");
}

void SideBar::setWorkDirectory(const QString& dirPath)
{
    // 兼容旧接口：清空工作区并设置单个文件夹
    m_workspaceFolders.clear();
    if (!dirPath.isEmpty()) {
        m_workspaceFolders.append(dirPath);
    }
    m_workDir = dirPath;
    if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    refreshFileList();
    emit workspaceFoldersChanged(m_workspaceFolders);
}

bool SideBar::addWorkspaceFolder(const QString& dirPath)
{
    if (dirPath.isEmpty()) return false;

    QString absPath = QDir(dirPath).absolutePath();
    // 去重
    if (m_workspaceFolders.contains(absPath)) {
        return false;
    }

    m_workspaceFolders.append(absPath);
    // 兼容 m_workDir（保持为第一个文件夹）
    if (m_workspaceFolders.size() == 1) {
        m_workDir = absPath;
        if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    }
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    refreshFileList();
    emit workspaceFoldersChanged(m_workspaceFolders);
    return true;
}

void SideBar::removeWorkspaceFolder(int index)
{
    if (index < 0 || index >= m_workspaceFolders.size()) return;

    m_workspaceFolders.removeAt(index);
    // 更新 m_workDir
    if (m_workspaceFolders.isEmpty()) {
        m_workDir.clear();
    } else {
        m_workDir = m_workspaceFolders.first();
    }
    if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    refreshFileList();
    emit workspaceFoldersChanged(m_workspaceFolders);
}

void SideBar::clearWorkspace()
{
    m_workspaceFolders.clear();
    m_workDir.clear();
    if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    refreshFileList();
    emit workspaceFoldersChanged(m_workspaceFolders);
}

void SideBar::populateFileTree(QTreeWidgetItem* parentItem, const QDir& dir)
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
        fileItem->setToolTip(0, fi.absoluteFilePath());
    }
}

// ========== 槽函数 ==========

void SideBar::onActivityButtonClicked()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    switchToActivity(static_cast<Activity>(btn->property("activity").toInt()));
}

void SideBar::onFileItemDoubleClicked(QTreeWidgetItem* item, int column)
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

void SideBar::onFileItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;
    // 单击文件夹时切换展开/折叠状态
    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type == QStringLiteral("dir") || type == QStringLiteral("workspaceRoot")) {
        item->setExpanded(!item->isExpanded());
    }
}

void SideBar::onExplorerNewFile()
{
    emit fileCreateRequested();
}

void SideBar::onExplorerRefresh()
{
    refreshFileList();
}

void SideBar::onExplorerCollapseAll()
{
    m_fileTree->collapseAll();
}

void SideBar::onExplorerOpenFolder()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("打开文件夹"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        // 修复 P0-1: 复用 setWorkDirectory() 同时更新 m_workDir 和 m_workspaceFolders，
        // 否则 refreshFileList() 因 m_workspaceFolders 为空而只显示提示项，文件树不填充
        setWorkDirectory(dir);
        emit openFolderRequested();
    }
}

void SideBar::onTerminalClicked()
{
    emit terminalToggleRequested();
}

void SideBar::setTerminalButtonChecked(bool checked)
{
    if (m_btnTerminal) {
        m_btnTerminal->blockSignals(true);
        m_btnTerminal->setChecked(checked);
        m_btnTerminal->blockSignals(false);
    }
}

void SideBar::refreshActivityStyles()
{
    for (auto* btn : {m_btnExplorer, m_btnSearch, m_btnGit, m_btnTasks, m_btnOutline, m_btnExtensions, m_btnTerminal}) {
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
        btn->update();
    }
}

QTreeWidgetItem* SideBar::findTreeItemByPath(QTreeWidgetItem* parent, const QString& filePath) const
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

void SideBar::selectFileByPath(const QString& filePath)
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

void SideBar::onFileContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_fileTree->itemAt(pos);
    QMenu menu(this);

    if (item) {
        QString type = item->data(0, Qt::UserRole + 1).toString();
        QString filePath = item->data(0, Qt::UserRole).toString();
        QString fileName = item->text(0);

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
                removeWorkspaceFolder(wsIndex);
            });
        }

        menu.addSeparator();
    }

    // 始终可用的操作
    QAction* actNew = menu.addAction(tr("新建文件..."));
    connect(actNew, &QAction::triggered, this, &SideBar::fileCreateRequested);

    // V1.9: 新建文件夹
    QAction* actNewFolder = menu.addAction(tr("新建文件夹..."));
    connect(actNewFolder, &QAction::triggered, this, &SideBar::folderCreateRequested);

    menu.addSeparator();

    // V1.9: 添加文件夹到工作区
    QAction* actAddFolder = menu.addAction(tr("添加文件夹到工作区..."));
    connect(actAddFolder, &QAction::triggered, this, [this]() {
        emit addFolderToWorkspaceRequested();
    });

    QAction* actRefresh = menu.addAction(tr("刷新文件列表"));
    connect(actRefresh, &QAction::triggered, this, &SideBar::refreshFileList);

    menu.exec(m_fileTree->mapToGlobal(pos));
}

// ============================================================
// V1.9: 文件树拖拽移动文件
// ============================================================

bool SideBar::eventFilter(QObject* obj, QEvent* event)
{
    // 拦截文件树 viewport 的 Drop 事件，自定义移动逻辑
    if (obj == m_fileTree->viewport() && event->type() == QEvent::Drop) {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        if (handleTreeDropEvent(dropEvent)) {
            return true;  // 事件已处理，阻止 QTreeWidget 默认行为（默认会移动 item）
        }
    }
    return QWidget::eventFilter(obj, event);
}

bool SideBar::handleTreeDropEvent(QDropEvent* event)
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
        // 拖到空白处 → 工作目录根
        targetDir = m_workDir;
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

    // 发射信号交由 Widget 层处理实际移动
    emit fileMoveRequested(sourcePath, targetDir);
    event->accept();
    return true;
}

// ============================================================
// V1.9: 大纲面板（符号导航）— 委托给 OutlinePanel
// ============================================================

void SideBar::updateOutline(const QString& filePath, const QList<QVariantMap>& symbols)
{
    if (m_outlinePanel) m_outlinePanel->updateOutline(filePath, symbols);
}

void SideBar::clearOutline()
{
    if (m_outlinePanel) m_outlinePanel->clearOutline();
}

void SideBar::updateOutlineFromText(const QString& filePath, const QString& content)
{
    if (m_outlinePanel) m_outlinePanel->updateOutlineFromText(filePath, content);
}
