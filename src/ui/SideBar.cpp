#include "SideBar.h"
#include "Logger.hpp"
#include "ui/GitPanel.h"
#include "core/TaskManager.h"  // M15: 任务管理器

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

    // --- Search 面板 ---
    m_searchPanel = new QWidget();
    auto* searchLayout = new QVBoxLayout(m_searchPanel);
    searchLayout->setContentsMargins(8, 8, 4, 4);
    searchLayout->setSpacing(4);

    auto* searchTitle = new QLabel(tr("搜索"), m_searchPanel);
    searchTitle->setObjectName(QStringLiteral("panelTitle"));
    searchLayout->addWidget(searchTitle);

    m_searchInput = new QLineEdit(m_searchPanel);
    m_searchInput->setPlaceholderText(tr("搜索内容..."));
    m_searchInput->setObjectName(QStringLiteral("searchInput"));
    searchLayout->addWidget(m_searchInput);

    // 替换输入框（V1.9）
    m_replaceInput = new QLineEdit(m_searchPanel);
    m_replaceInput->setPlaceholderText(tr("替换为..."));
    m_replaceInput->setObjectName(QStringLiteral("replaceInput"));
    searchLayout->addWidget(m_replaceInput);

    // 选项行：大小写/正则 + 全部替换按钮
    auto* optLayout = new QHBoxLayout();
    optLayout->setSpacing(4);
    m_chkCaseSensitive = new QCheckBox(tr("Aa"), m_searchPanel);
    m_chkCaseSensitive->setToolTip(tr("区分大小写"));
    m_chkRegex = new QCheckBox(tr(".*"), m_searchPanel);
    m_chkRegex->setToolTip(tr("正则表达式"));
    m_chkSymbolSearch = new QCheckBox(tr("§"), m_searchPanel);  // V1.9: 符号搜索
    m_chkSymbolSearch->setToolTip(tr("符号搜索模式（全局搜索 class/function/struct 等符号定义）"));
    m_btnReplaceAll = new QPushButton(tr("全部替换"), m_searchPanel);
    m_btnReplaceAll->setToolTip(tr("在所有文件中替换"));
    optLayout->addWidget(m_chkCaseSensitive);
    optLayout->addWidget(m_chkRegex);
    optLayout->addWidget(m_chkSymbolSearch);
    optLayout->addStretch();
    optLayout->addWidget(m_btnReplaceAll);
    searchLayout->addLayout(optLayout);

    // 文件类型过滤（V1.9）
    m_fileFilterInput = new QLineEdit(m_searchPanel);
    m_fileFilterInput->setPlaceholderText(tr("文件类型: *.cpp,*.h"));
    m_fileFilterInput->setObjectName(QStringLiteral("fileFilterInput"));
    searchLayout->addWidget(m_fileFilterInput);

    m_searchResults = new QListWidget(m_searchPanel);
    m_searchResults->setObjectName(QStringLiteral("sideFileList"));
    searchLayout->addWidget(m_searchResults);

    m_panelStack->addWidget(m_searchPanel);

    // --- Git 面板（源代码管理）---
    m_gitPanel = new QWidget();
    auto* gitLayout = new QVBoxLayout(m_gitPanel);
    gitLayout->setContentsMargins(0, 0, 0, 0);
    gitLayout->setSpacing(0);

    m_gitPanelWidget = new GitPanel(m_gitPanel);
    gitLayout->addWidget(m_gitPanelWidget);

    m_panelStack->addWidget(m_gitPanel);

    // --- Tasks 面板（M15: 任务系统）---
    m_tasksPanel = new QWidget();
    auto* tasksLayout = new QVBoxLayout(m_tasksPanel);
    tasksLayout->setContentsMargins(4, 6, 2, 2);
    tasksLayout->setSpacing(2);

    auto* tasksTitle = new QLabel(tr("任务"), m_tasksPanel);
    tasksTitle->setObjectName(QStringLiteral("panelTitle"));
    tasksLayout->addWidget(tasksTitle);

    // 任务树（分组显示）
    m_taskTree = new QTreeWidget(m_tasksPanel);
    m_taskTree->setObjectName(QStringLiteral("sideFileTree"));
    m_taskTree->setHeaderHidden(true);
    m_taskTree->setAnimated(true);
    m_taskTree->setIndentation(12);
    m_taskTree->setRootIsDecorated(true);
    m_taskTree->setColumnCount(1);
    m_taskTree->setContextMenuPolicy(Qt::CustomContextMenu);
    tasksLayout->addWidget(m_taskTree, 1);

    // 工具栏按钮
    auto* taskBtnLayout = new QHBoxLayout();
    m_btnRunTask = new QPushButton(tr("▶ 运行"), m_tasksPanel);
    m_btnRunTask->setFixedSize(70, 24);
    m_btnRunTask->setObjectName(QStringLiteral("btnResetSection"));
    m_btnStopAll = new QPushButton(tr("⏹ 停止全部"), m_tasksPanel);
    m_btnStopAll->setFixedSize(80, 24);
    m_btnStopAll->setObjectName(QStringLiteral("btnResetSection"));
    m_btnConfigureTasks = new QPushButton(tr("⚙ 配置"), m_tasksPanel);
    m_btnConfigureTasks->setFixedSize(70, 24);
    m_btnConfigureTasks->setObjectName(QStringLiteral("btnResetSection"));
    taskBtnLayout->addWidget(m_btnRunTask);
    taskBtnLayout->addWidget(m_btnStopAll);
    taskBtnLayout->addWidget(m_btnConfigureTasks);
    taskBtnLayout->addStretch();
    tasksLayout->addLayout(taskBtnLayout);

    // 输出区域
    auto* outputLabel = new QLabel(tr("输出"), m_tasksPanel);
    outputLabel->setObjectName(QStringLiteral("settingsSectionTitle"));
    tasksLayout->addWidget(outputLabel);

    m_taskOutputView = new QPlainTextEdit(m_tasksPanel);
    m_taskOutputView->setReadOnly(true);
    m_taskOutputView->setObjectName(QStringLiteral("settingsHint"));
    m_taskOutputView->setFont(QFont("Consolas", 9));
    m_taskOutputView->setMaximumHeight(150);
    tasksLayout->addWidget(m_taskOutputView);

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

    // --- Outline 面板（V1.9: 大纲/符号导航）---
    m_outlinePanel = new QWidget();
    auto* outlineLayout = new QVBoxLayout(m_outlinePanel);
    outlineLayout->setContentsMargins(4, 6, 2, 2);
    outlineLayout->setSpacing(2);

    auto* outlineTitle = new QLabel(tr("大纲"), m_outlinePanel);
    outlineTitle->setObjectName(QStringLiteral("panelTitle"));
    outlineLayout->addWidget(outlineTitle);

    m_outlineTree = new QTreeWidget(m_outlinePanel);
    m_outlineTree->setObjectName(QStringLiteral("sideFileTree"));
    m_outlineTree->setHeaderHidden(true);
    m_outlineTree->setAnimated(true);
    m_outlineTree->setIndentation(14);
    m_outlineTree->setRootIsDecorated(true);
    m_outlineTree->setSortingEnabled(false);
    m_outlineTree->setSelectionMode(QAbstractItemView::SingleSelection);
    // 使用支持 emoji 的字体
    {
        QFont emojiFont = m_outlineTree->font();
        emojiFont.setFamilies({QStringLiteral("Segoe UI Emoji"),
                               QStringLiteral("Apple Color Emoji"),
                               QStringLiteral("Noto Color Emoji")});
        m_outlineTree->setFont(emojiFont);
    }
    outlineLayout->addWidget(m_outlineTree);

    // 提示标签（无符号时显示）
    m_outlineHint = new QLabel(m_outlinePanel);
    m_outlineHint->setObjectName(QStringLiteral("settingsHint"));
    m_outlineHint->setWordWrap(true);
    m_outlineHint->setText(tr("打开文件后显示符号大纲\n\n支持：\n• LSP 符号（精确）\n• 正则扫描（离线 fallback）"));
    m_outlineHint->setAlignment(Qt::AlignCenter);
    outlineLayout->addWidget(m_outlineHint);

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

    // 搜索功能
    connect(m_searchInput, &QLineEdit::returnPressed,
            this, &SideBar::onSearchTriggered);
    connect(m_searchResults, &QListWidget::itemDoubleClicked,
            this, &SideBar::onSearchResultDoubleClicked);
    // V1.9: 全部替换按钮
    connect(m_btnReplaceAll, &QPushButton::clicked,
            this, [this]() { onSearchReplaceAll(); });

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

    // M15: 任务面板信号连接
    connect(m_taskTree, &QTreeWidget::itemDoubleClicked,
            this, &SideBar::onTaskItemDoubleClicked);
    connect(m_taskTree, &QTreeWidget::customContextMenuRequested,
            this, &SideBar::onTaskItemContextMenu);
    connect(m_btnRunTask, &QPushButton::clicked, this, &SideBar::onRunTaskClicked);
    connect(m_btnStopAll, &QPushButton::clicked, this, &SideBar::onStopAllTasksClicked);
    connect(m_btnConfigureTasks, &QPushButton::clicked, this, &SideBar::onConfigureTasksClicked);

    // 连接 TaskManager 信号
    auto& tm = TaskManager::instance();
    connect(&tm, &TaskManager::taskStarted, this, &SideBar::onTaskStarted);
    connect(&tm, &TaskManager::taskFinished, this, &SideBar::onTaskFinished);
    connect(&tm, &TaskManager::taskOutput, this, &SideBar::onTaskOutput);

    // V1.9: 大纲树点击 → 跳转信号
    connect(m_outlineTree, &QTreeWidget::itemClicked,
            this, &SideBar::onOutlineItemClicked);

    // 初始化任务树
    refreshTaskTree();
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
    }
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
    refreshFileList();
    emit workspaceFoldersChanged(m_workspaceFolders);
}

void SideBar::clearWorkspace()
{
    m_workspaceFolders.clear();
    m_workDir.clear();
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

void SideBar::onSearchTriggered()
{
    QString keyword = m_searchInput->text().trimmed();
    m_searchResults->clear();

    if (keyword.isEmpty() && !(m_chkSymbolSearch && m_chkSymbolSearch->isChecked())) return;

    // V1.9: 符号搜索模式
    if (m_chkSymbolSearch && m_chkSymbolSearch->isChecked()) {
        performSymbolSearch(keyword);
        return;
    }

    // V1.9: 遍历工作区所有文件夹
    if (m_workspaceFolders.isEmpty()) {
        // 无工作区时使用默认 Files 目录
        QString filesDir = QCoreApplication::applicationDirPath() + QStringLiteral("/Files");
        QDir dir(filesDir);
        if (!dir.exists()) dir.mkpath(filesDir);
        searchInDirectory(dir, keyword);
    } else {
        for (const QString& folder : m_workspaceFolders) {
            QDir dir(folder);
            if (dir.exists()) {
                searchInDirectory(dir, keyword);
            }
        }
    }

    if (m_searchResults->count() == 0) {
        m_searchResults->addItem(tr("未找到匹配结果"));
    }
}

void SideBar::searchInDirectory(const QDir& dir, const QString& keyword)
{
    // 递归子目录
    QFileInfoList dirEntries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : dirEntries) {
        searchInDirectory(QDir(fi.absoluteFilePath()), keyword);
    }

    // 准备正则表达式（如果启用）
    QRegularExpression regex;
    if (m_chkRegex && m_chkRegex->isChecked()) {
        regex.setPattern(keyword);
        if (!m_chkCaseSensitive || !m_chkCaseSensitive->isChecked()) {
            regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        }
        if (!regex.isValid()) return;
    }

    Qt::CaseSensitivity cs = (m_chkCaseSensitive && m_chkCaseSensitive->isChecked()) ?
        Qt::CaseSensitive : Qt::CaseInsensitive;

    // 搜索文件内容
    QFileInfoList fileEntries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : fileEntries) {
        // 跳过二进制文件
        QString suffix = fi.suffix().toLower();
        if (suffix == QStringLiteral("exe") || suffix == QStringLiteral("dll") ||
            suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
            suffix == QStringLiteral("ico") || suffix == QStringLiteral("zip"))
            continue;

        // V1.9: 文件类型过滤
        if (!matchesFileFilter(fi.fileName())) continue;

        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QTextStream in(&file);
        int lineNum = 0;
        while (!in.atEnd()) {
            ++lineNum;
            QString line = in.readLine();
            bool matched = false;
            if (m_chkRegex && m_chkRegex->isChecked()) {
                matched = regex.match(line).hasMatch();
            } else {
                matched = line.contains(keyword, cs);
            }
            if (matched) {
                QString display = QStringLiteral("%1:%2  %3")
                    .arg(fi.fileName())
                    .arg(lineNum)
                    .arg(line.trimmed().left(60));
                auto* item = new QListWidgetItem(display, m_searchResults);
                item->setData(Qt::UserRole, fi.absoluteFilePath());
                item->setData(Qt::UserRole + 1, lineNum - 1);  // 转为 0-based
                item->setData(Qt::UserRole + 2, QStringLiteral("text"));  // 标记为文本搜索
                item->setToolTip(fi.absoluteFilePath() + QStringLiteral(":") + QString::number(lineNum));
            }
        }
        file.close();
    }
}

bool SideBar::matchesFileFilter(const QString& fileName) const
{
    if (!m_fileFilterInput || m_fileFilterInput->text().trimmed().isEmpty())
        return true;  // 无过滤条件，匹配所有

    QString filterText = m_fileFilterInput->text().trimmed();
    // 支持逗号分隔的多个模式：*.cpp,*.h,*.py
    QStringList patterns = filterText.split(QStringLiteral(","),
        Qt::SkipEmptyParts);

    for (const QString& pattern : patterns) {
        QString p = pattern.trimmed();
        QRegularExpression re(
            QRegularExpression::wildcardToRegularExpression(p),
            QRegularExpression::CaseInsensitiveOption);
        if (re.match(fileName).hasMatch()) return true;
    }
    return false;
}

void SideBar::onSearchReplaceAll()
{
    QString keyword = m_searchInput->text().trimmed();
    QString replacement = m_replaceInput ? m_replaceInput->text() : QString();

    if (keyword.isEmpty()) return;

    int totalReplaced = 0;
    int fileCount = 0;

    // V1.9: 遍历工作区所有文件夹
    QStringList searchDirs = m_workspaceFolders.isEmpty() ?
        QStringList{QCoreApplication::applicationDirPath() + QStringLiteral("/Files")} :
        m_workspaceFolders;

    QRegularExpression regex;
    if (m_chkRegex && m_chkRegex->isChecked()) {
        regex.setPattern(keyword);
        if (!m_chkCaseSensitive || !m_chkCaseSensitive->isChecked()) {
            regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        }
        if (!regex.isValid()) return;
    }

    Qt::CaseSensitivity cs = (m_chkCaseSensitive && m_chkCaseSensitive->isChecked()) ?
        Qt::CaseSensitive : Qt::CaseInsensitive;

    for (const QString& searchDir : searchDirs) {
        QDir dir(searchDir);
        if (!dir.exists()) continue;

        QDirIterator it(searchDir, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString filePath = it.next();
            QFileInfo fi(filePath);
            QString suffix = fi.suffix().toLower();
            if (suffix == QStringLiteral("exe") || suffix == QStringLiteral("dll") ||
                suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
                suffix == QStringLiteral("ico") || suffix == QStringLiteral("zip"))
                continue;

            if (!matchesFileFilter(fi.fileName())) continue;

            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QTextStream in(&file);
            QString content = in.readAll();
            file.close();

            QString newContent = content;
            int replaced = 0;

            if (m_chkRegex && m_chkRegex->isChecked()) {
                int offset = 0;
                QRegularExpressionMatch match;
                while ((match = regex.match(newContent, offset)).hasMatch()) {
                    newContent.replace(match.capturedStart(), match.capturedLength(), replacement);
                    offset = match.capturedStart() + replacement.length();
                    replaced++;
                }
            } else {
                int idx = 0;
                while ((idx = newContent.indexOf(keyword, idx, cs)) >= 0) {
                    newContent.replace(idx, keyword.length(), replacement);
                    idx += replacement.length();
                    replaced++;
                }
            }

            if (replaced > 0) {
                if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                    QTextStream out(&file);
                    out << newContent;
                    file.close();
                    totalReplaced += replaced;
                    fileCount++;
                }
            }
        }
    }

    m_searchResults->clear();
    m_searchResults->addItem(tr("已替换 %1 处，涉及 %2 个文件")
        .arg(totalReplaced).arg(fileCount));

    if (!keyword.isEmpty()) {
        onSearchTriggered();
    }
}

QList<SideBar::SymbolEntry> SideBar::scanFileSymbols(const QString& filePath) const
{
    // V1.9: 正则扫描单个文件的符号定义
    QList<SymbolEntry> result;

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
        return result;  // 不支持的类型
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return result;
    QTextStream in(&file);
    QStringList lines = in.readAll().split(QLatin1Char('\n'));
    file.close();

    for (int i = 0; i < lines.size(); ++i) {
        auto m = re.match(lines[i]);
        if (m.hasMatch()) {
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

            result.append({name, i, icon});
        }
    }

    return result;
}

void SideBar::performSymbolSearch(const QString& keyword)
{
    // V1.9: 全局符号搜索 — 遍历工作区所有文件，扫描符号定义
    m_searchResults->clear();

    QStringList searchDirs = m_workspaceFolders.isEmpty() ?
        QStringList{QCoreApplication::applicationDirPath() + QStringLiteral("/Files")} :
        m_workspaceFolders;

    Qt::CaseSensitivity cs = (m_chkCaseSensitive && m_chkCaseSensitive->isChecked()) ?
        Qt::CaseSensitive : Qt::CaseInsensitive;

    int totalFound = 0;

    for (const QString& searchDir : searchDirs) {
        QDirIterator it(searchDir, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString filePath = it.next();
            QFileInfo fi(filePath);
            QString suffix = fi.suffix().toLower();

            // 跳过二进制文件
            if (suffix == QStringLiteral("exe") || suffix == QStringLiteral("dll") ||
                suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
                suffix == QStringLiteral("ico") || suffix == QStringLiteral("zip"))
                continue;

            if (!matchesFileFilter(fi.fileName())) continue;

            // 扫描符号
            auto symbols = scanFileSymbols(filePath);
            for (const auto& sym : symbols) {
                // 匹配符号名
                bool match = keyword.isEmpty() ||
                    sym.name.contains(keyword, cs);
                if (!match) continue;

                // 添加到结果列表
                QString displayText = QStringLiteral("%1 %2  —  %3:%4")
                    .arg(sym.icon, sym.name, fi.fileName())
                    .arg(sym.line + 1);

                auto* item = new QListWidgetItem(displayText, m_searchResults);
                item->setToolTip(filePath);
                // 存储跳转信息：UserRole=filePath, UserRole+1=line(0-based), UserRole+2=类型
                item->setData(Qt::UserRole, filePath);
                item->setData(Qt::UserRole + 1, sym.line);
                item->setData(Qt::UserRole + 2, QStringLiteral("symbol"));  // 标记为符号搜索
                totalFound++;

                if (totalFound >= 500) {
                    m_searchResults->addItem(tr("... 结果过多，仅显示前 500 项"));
                    return;
                }
            }
        }
    }

    if (totalFound == 0) {
        m_searchResults->addItem(tr("未找到匹配符号"));
    } else {
        LOG_DEBUG("[SideBar] 符号搜索完成: " << totalFound << " 个结果");
    }
}

void SideBar::onSearchResultDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;
    QString filePath = item->data(Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    // V1.9: 统一跳转逻辑 — 打开文件并跳转到指定行
    int line = item->data(Qt::UserRole + 1).toInt();  // 0-based
    QString resultType = item->data(Qt::UserRole + 2).toString();

    if (resultType == QStringLiteral("symbol") || resultType == QStringLiteral("text")) {
        // 符号搜索或文本搜索结果：打开文件并跳转到行
        emit fileOpenRequested(filePath);
        emit outlineSymbolClicked(filePath, line, 0);
    } else {
        // 兼容旧结果（无类型标记）：仅打开文件
        emit fileOpenRequested(filePath);
    }
}

// ============================================================
// M15: 任务面板槽函数
// ============================================================

void SideBar::refreshTaskTree()
{
    m_taskTree->clear();

    auto& tm = TaskManager::instance();
    QStringList groups = {QStringLiteral("build"), QStringLiteral("run"),
                         QStringLiteral("test"), QStringLiteral("lint"), QStringLiteral("format")};

    // 分组图标映射
    QMap<QString, QString> groupIcons = {
        {QStringLiteral("build"),  QString::fromUtf8("\xF0\x9F\x94\xA8")},   // 🚨
        {QStringLiteral("run"),    QString::fromUtf8("\xE2\x96\xBA")},       // ▶
        {QStringLiteral("test"),   QString::fromUtf8("\xE2\x9C\x94")},       // ✔
        {QStringLiteral("lint"),   QString::fromUtf8("\xF0\x9F\x94\x8D")},   // 🔍
        {QStringLiteral("format"), QString::fromUtf8("\xE2\x9C\xA8")}        // ✨
    };

    for (const QString& grp : groups) {
        QList<TaskItem> tasks = tm.tasksByGroup(grp);
        if (tasks.isEmpty()) continue;

        // 创建分组节点
        auto* groupItem = new QTreeWidgetItem(m_taskTree);
        groupItem->setText(0, groupIcons.value(grp) + QStringLiteral(" ") + (grp == QStringLiteral("build") ? tr("构建")
                              : grp == QStringLiteral("run")    ? tr("运行")
                              : grp == QStringLiteral("test")   ? tr("测试")
                              : grp == QStringLiteral("lint")   ? tr("检查")
                              : tr("格式化")));
        groupItem->setExpanded(true);

        for (const TaskItem& t : tasks) {
            auto* taskItem = new QTreeWidgetItem(groupItem);

            // 状态前缀：运行中/成功/失败
            QString prefix;
            if (tm.isTaskRunning(t.label)) {
                prefix = QString::fromUtf8("\xE2\x8F\xB0");  // ⟳ 运行中
            } else if (t.exitCode == 0 && t.lastRun.isValid()) {
                prefix = QString::fromUtf8("\xE2\x9C\x93");      // ✓ 成功
            } else if (t.exitCode != 0 && t.lastRun.isValid()) {
                prefix = QString::fromUtf8("\xE2\x9C\x97");      // ✗ 失败
            } else {
                prefix = QStringLiteral("  ");
            }

            taskItem->setText(0, prefix + QStringLiteral(" ") + t.label);
            taskItem->setData(0, Qt::UserRole, t.label);
            taskItem->setData(0, Qt::UserRole + 1, grp);
        }
    }
}

void SideBar::onTaskItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;

    QString label = item->data(0, Qt::UserRole).toString();
    if (!label.isEmpty()) {
        TaskManager::instance().runTask(label);
    }
}

void SideBar::onTaskItemContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_taskTree->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QString label = item->data(0, Qt::UserRole).toString();

    QAction* actRun = menu.addAction(tr("▶ 运行任务"));
    connect(actRun, &QAction::triggered, this, [this, label]() {
        if (!label.isEmpty()) TaskManager::instance().runTask(label);
    });

    menu.addSeparator();

    QAction* actCopyCmd = menu.addAction(tr("复制命令"));
    connect(actCopyCmd, &QAction::triggered, this, [this, label]() {
        if (!label.isEmpty()) {
            TaskItem t = TaskManager::instance().task(label);
            QApplication::clipboard()->setText(t.command + QStringLiteral(" ") + t.args.join(QLatin1Char(' ')));
        }
    });

    menu.exec(m_taskTree->mapToGlobal(pos));
}

void SideBar::onRunTaskClicked()
{
    // 运行当前选中的任务
    QTreeWidgetItem* item = m_taskTree->currentItem();
    if (item) {
        QString label = item->data(0, Qt::UserRole).toString();
        if (!label.isEmpty()) {
            TaskManager::instance().runTask(label);
            return;
        }
    }
    // 如果没有选中，提示
    m_taskOutputView->appendPlainText(tr("> 请先在上方选择一个任务"));
}

void SideBar::onStopAllTasksClicked()
{
    TaskManager::instance().stopAll();
    m_taskOutputView->appendPlainText(tr("> 已停止所有正在运行的任务"));
}

void SideBar::onConfigureTasksClicked()
{
    // 打开或保存 tasks.json
    QString tasksPath = m_workDir + QStringLiteral("/.vscode/tasks.json");

    // 尝试加载已有配置
    if (QFile::exists(tasksPath)) {
        TaskManager::instance().loadTasksJson(tasksPath);
        refreshTaskTree();
        m_taskOutputView->appendPlainText(tr("> 已加载: %1").arg(tasksPath));
    } else {
        // 首次使用，创建默认配置
        QDir().mkpath(QFileInfo(tasksPath).absolutePath());
        TaskManager::instance().saveTasksJson(tasksPath);
        m_taskOutputView->appendPlainText(tr("> 已创建默认 tasks.json: %1").arg(tasksPath));
    }
}

void SideBar::onTaskStarted(const QString& label)
{
    m_taskOutputView->appendPlainText(QStringLiteral("> ▶ %1 ...").arg(label));
    refreshTaskTree();  // 更新状态图标
}

void SideBar::onTaskFinished(const QString& label, int exitCode, const QString& output)
{
    QString icon = (exitCode == 0) ? QString::fromUtf8("\xE2\x9C\x93") : QString::fromUtf8("\xE2\x9C\x97");
    m_taskOutputView->appendPlainText(QStringLiteral("%1 [%2] 退出码: %3").arg(icon).arg(label).arg(exitCode));

    // 显示最后几行输出（如果有）
    QStringList lines = output.split(QLatin1Char('\n'));
    if (lines.size() > 5) {
        m_taskOutputView->appendPlainText(tr("  (最后5行输出):"));
        for (int i = qMax(0, lines.size() - 5); i < lines.size(); ++i) {
            m_taskOutputView->appendPlainText(QStringLiteral("  | ") + lines[i].trimmed());
        }
    }

    refreshTaskTree();  // 更新状态图标
}

void SideBar::onTaskOutput(const QString& label, const QString& output)
{
    // 实时追加输出到面板
    QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (!line.trimmed().isEmpty()) {
            m_taskOutputView->appendPlainText(QStringLiteral("[%1] %2").arg(label, line.trimmed()));
        }
    }

    // 自动滚动到底部
    QTextCursor cursor = m_taskOutputView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_taskOutputView->setTextCursor(cursor);
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
// V1.9: 大纲面板（符号导航）
// ============================================================

QString SideBar::symbolIcon(int kind) const
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

void SideBar::extractSymbolPosition(const QVariantMap& sym, int& line, int& col) const
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

void SideBar::populateOutlineTreeFromList(QTreeWidgetItem* parent, const QList<QVariantMap>& symbols)
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

void SideBar::populateOutlineTree(QTreeWidgetItem* parent, const QVariantList& symbols)
{
    // 兼容旧接口：将 QVariantList 转为 QList<QVariantMap> 调用新接口
    QList<QVariantMap> maps;
    for (const QVariant& v : symbols) {
        maps.append(v.toMap());
    }
    populateOutlineTreeFromList(parent, maps);
}

void SideBar::updateOutline(const QString& filePath, const QList<QVariantMap>& symbols)
{
    if (!m_outlineTree) return;

    m_outlineFilePath = filePath;
    m_outlineTree->clear();

    if (symbols.isEmpty()) {
        if (m_outlineHint) {
            m_outlineHint->setText(tr("未获取到符号\n\n可能原因：\n• 当前文件无 LSP 支持\n• 文件为空"));
            m_outlineHint->show();
        }
        return;
    }

    // 填充大纲树
    populateOutlineTreeFromList(nullptr, symbols);
    m_outlineTree->expandToDepth(1);

    if (m_outlineHint) m_outlineHint->hide();

    LOG_DEBUG("[SideBar] 大纲更新: " << symbols.size() << " 个顶层符号, file=" << filePath.toStdString());
}

void SideBar::clearOutline()
{
    if (m_outlineTree) m_outlineTree->clear();
    m_outlineFilePath.clear();
    if (m_outlineHint) {
        m_outlineHint->setText(tr("打开文件后显示符号大纲\n\n支持：\n• LSP 符号（精确）\n• 正则扫描（离线 fallback）"));
        m_outlineHint->show();
    }
}

void SideBar::updateOutlineFromText(const QString& filePath, const QString& content)
{
    // V1.9: 离线正则扫描 fallback（无 LSP 时使用）
    if (!m_outlineTree) return;

    m_outlineFilePath = filePath;
    m_outlineTree->clear();

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
        if (m_outlineHint) {
            m_outlineHint->setText(tr("该文件类型不支持离线大纲\n\n支持：\n• C/C++ (.cpp/.h)\n• Python (.py)\n• JS/TS (.js/.ts)\n• Markdown (.md)"));
            m_outlineHint->show();
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
        if (m_outlineHint) {
            m_outlineHint->setText(tr("未扫描到符号\n\n（离线正则扫描，结果可能不完整）"));
            m_outlineHint->show();
        }
        return;
    }

    // 扁平添加（离线模式不构建层级）
    for (const auto& e : entries) {
        auto* item = new QTreeWidgetItem(m_outlineTree);
        item->setText(0, e.first);
        item->setToolTip(0, tr("行 %1").arg(e.second + 1));
        item->setData(0, Qt::UserRole, e.second);  // 行号
        item->setData(0, Qt::UserRole + 1, 0);     // 列号
    }

    if (m_outlineHint) m_outlineHint->hide();
}

void SideBar::onOutlineItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;

    int line = item->data(0, Qt::UserRole).toInt();
    int col = item->data(0, Qt::UserRole + 1).toInt();

    if (m_outlineFilePath.isEmpty()) return;

    // 发射跳转信号（行列均为 0-based）
    emit outlineSymbolClicked(m_outlineFilePath, line, col);
}
