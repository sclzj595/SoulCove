#include "ui/shell/Widget.h"
#include "ui/editor/MyTextEdit.h"
#include "ui/editor/TextCompleter.h"

#include "core/config/ConfigManager.h"
#include "core/fileio/FileOperator.h"
#include "core/config/ThemeManager.h"
#include "core/shortcut/ShortcutFilter.h"
#include "factory/UIFactory.h"
#include "controller/EditorActions.h"
#include "controller/FileController.h"
#include "ui/settings/SettingsPage.h"
#include "ui/tools/DiffViewer.h"
#include "ui/tools/RegexTester.h"
#include "ui/editor/FindReplaceBar.h"
#include "ui/editor/EditorSplitView.h"
#include "core/vcs/MergeConflictResolver.h"
#include "core/vcs/GitManager.h"
#include "core/snippet/SnippetManager.h"
#include "ui/remote/SshConfigPanel.h"
#include "core/editor/HeaderSymbolScanner.h"
#include "Logger.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QInputDialog>
#include "ui/dialog/ModernDialog.h"
#include <QPushButton>
#include <QAbstractButton>
#include <QShortcut>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QCloseEvent>
#include <QEvent>
#include <QProcess>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QToolTip>
#include <QDialog>
#include <QListWidget>
#include <QStyle>
#include "core/base/ScreenGuard.h"
#include <QButtonGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QTextCursor>
#include <QTextDocument>
#include <QDir>
#include <QRegularExpression>

// ========== 构造 / 析构 ==========

Widget::Widget(QWidget *parent)
    : FramelessWindow(parent), m_currentTextEdit(nullptr),
      m_settingsPage(nullptr)
{
    // 0. 安装屏幕安全守卫 — 防止跨屏/高DPI时GDI崩溃
    auto* screenGuard = new ScreenGuard(this);
    screenGuard->installOn(this);

    // 1. 初始化配置管理器（单例）
    loadConfig();

    // 1.5 初始化主题管理器，应用当前主题
    auto& tm = ThemeManager::instance();
    QString savedTheme = ConfigManager::instance().theme();
    if (!savedTheme.isEmpty() && tm.themeKeys().contains(savedTheme))
        tm.switchTheme(savedTheme);  // switchTheme已修复：始终应用QSS
    else
        tm.switchTheme(QStringLiteral("purple"));  // 默认主题

    // 2. 构建VSCode三栏UI
    createUi();

    // 2.5 启用拖放
    setAcceptDrops(true);

    // 3. 设置标题栏（用于拖拽判定）
    setTitleBarWidget(m_titleBar);

    // 4. 初始化文件操作器
    m_fileOperator = new FileOperator(this);
    m_fileOperator->attachObserver(this);

    // T18: 初始化文件外部修改监听器
    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &Widget::onFileChangedExternally);

    // 设置 ContentReader/ContentWriter 回调（通过接口解耦）
    m_fileOperator->setContentReader([this]() -> QString {
        return m_currentTextEdit ? m_currentTextEdit->toPlainText() : QString();
    });
    m_fileOperator->setContentWriter([this](const QString& content) {
        if (m_currentTextEdit) m_currentTextEdit->setPlainText(content);
    });

    // 5. 提前创建补全器（解决时序问题：文件加载时文本变更信号早于懒加载）
    m_currentTextEdit = nullptr;
    auto& cfg = ConfigManager::instance();
    if (cfg.showCompletion()) {
        auto* completerImpl = new TextCompleter(this);
        completerImpl->setWindowFlags(completerImpl->windowFlags() | Qt::WindowStaysOnTopHint);
        m_completer = completerImpl;
    } else {
        m_completer = nullptr;
    }

    // 5.5 获取配置引用（后续自动保存等需要用到）
    auto& config = ConfigManager::instance();

    // 5.6 连接标签页信号
    connect(m_tabBar, &EditorTabBar::currentEditorChanged,
            this, &Widget::onCurrentEditorChanged);
    connect(m_tabBar, &EditorTabBar::tabCountChanged,
            this, &Widget::onTabCountChanged);
    connect(m_tabBar, &EditorTabBar::allTabsClosed,
            this, &Widget::onAllTabsClosed);
    connect(m_tabBar, &EditorTabBar::customTabDestroyed,
            this, [this](const QString& title) {
        if (title == tr("设置")) m_settingsPage = nullptr;
        if (title == tr("SSH 配置")) m_sshConfigPanel = nullptr;
        // 设置页关闭后，如果没有其他标签，恢复显示欢迎页
        if (m_welcomePage && m_tabBar && m_tabBar->tabCount() == 0) {
            m_welcomePage->show();
        }
    });
    connect(m_tabBar, &EditorTabBar::saveRequested,
            this, [this](MyTextEdit* editor) {
        // 切换到该编辑器并执行保存
        if (editor) {
            m_currentTextEdit = editor;
            on_btnSave_clicked();
        }
    });

    // 7. 初始化 LSP 语言服务器管理器（门面模式，多语言客户端生命周期+信号路由）
    m_lspManager = new LspManager(this);
    connect(m_lspManager, &LspManager::completionsReady,
            this, &Widget::onLspCompletionsReady);
    connect(m_lspManager, &LspManager::diagnosticsReady,
            this, &Widget::onLspDiagnosticsReady);
    connect(m_lspManager, &LspManager::definitionReady,
            this, &Widget::onLspDefinitionReady);
    connect(m_lspManager, &LspManager::hoverReady,
            this, &Widget::onLspHoverReady);
    connect(m_lspManager, &LspManager::referencesReady,
            this, &Widget::onLspReferencesReady);
    connect(m_lspManager, &LspManager::symbolsReady,
            this, &Widget::onLspSymbolsReady);
    connect(m_lspManager, &LspManager::serverError,
            this, &Widget::onLspServerError);
    // LSP 服务器不可用时弹窗提示（仅提示一次，避免刷屏）
    connect(m_lspManager, &LspManager::serverNotAvailable,
            this, [this](const QString& langId) {
                static QSet<QString> warned;
                if (warned.contains(langId)) return;
                warned.insert(langId);
                QString msg = tr("未检测到 %1 语言服务器，LSP 智能功能（跳转定义/"
                                 "悬停提示/查找引用）将不可用。\n\n"
                                 "请前往「设置 → LSP」手动配置服务器路径，"
                                 "或安装对应语言服务器（如 clangd）后重启。").arg(langId);
                ModernDialog::information(this, tr("LSP 不可用"), msg);
            });

    // 8. 侧边栏文件双击打开
    connect(m_sideBar, &SideBar::fileOpenRequested,
            this, &Widget::onFileOpenFromSidebar);
    connect(m_sideBar, &SideBar::fileCreateRequested,
            this, &Widget::onSidebarCreateFile);
    connect(m_sideBar, &SideBar::fileDeleteRequested,
            this, &Widget::onSidebarDeleteFile);
    connect(m_sideBar, &SideBar::fileRenameRequested,
            this, &Widget::onSidebarRenameFile);
    connect(m_sideBar, &SideBar::openInFolderRequested,
            this, &Widget::onSidebarOpenInFolder);
    // V1.9: 新建文件夹 + 拖拽移动
    connect(m_sideBar, &SideBar::folderCreateRequested,
            this, &Widget::onSidebarCreateFolder);
    connect(m_sideBar, &SideBar::fileMoveRequested,
            this, &Widget::onSidebarMoveFile);
    // V1.9: 大纲符号跳转
    connect(m_sideBar, &SideBar::outlineSymbolClicked,
            this, &Widget::onOutlineSymbolClicked);
    // V1.9: 添加文件夹到工作区
    connect(m_sideBar, &SideBar::addFolderToWorkspaceRequested,
            this, &Widget::onAddFolderToWorkspace);
    // 侧边栏终端按钮 → 切换终端面板
    connect(m_sideBar, &SideBar::terminalToggleRequested,
            this, &Widget::onToggleTerminal);
    // 侧边栏「打开文件夹」按钮 → 统一走 Widget 槽（修复孤儿信号）
    connect(m_sideBar, &SideBar::openFolderRequested,
            this, [this]() {
                LOG_INFO("[Widget] 侧边栏 openFolderRequested 信号已接收");
                // 侧边栏 onExplorerOpenFolder 已自行设置工作目录并刷新文件列表
                // 此处统一处理 UI 状态：隐藏欢迎页、同步标题栏等
                if (m_welcomePage) m_welcomePage->hide();
                // P0-2: 同步工作区根目录到 LSP 管理器，使 clangd 使用正确的项目根目录
                if (m_lspManager && m_sideBar) {
                    m_lspManager->setWorkspaceRoot(m_sideBar->currentWorkDir());
                }
            });

    // Git 面板：获取 SideBar 内嵌的 GitPanel 并连接信号
    m_gitPanel = m_sideBar->gitPanelWidget();
    if (m_gitPanel) {
        connect(m_gitPanel, &GitPanel::fileDiffRequested, this, [this](const QString& filePath) {
            // 打开该文件的 diff 视图
            QString diffText = GitManager::instance().diff(filePath);
            if (diffText.isEmpty()) {
                ModernDialog::information(this, tr("Diff"), tr("文件没有更改或不在版本控制中"));
                return;
            }
            auto* dv = new DiffViewer();
            dv->setDiffContent(tr("（原始版本）"), diffText,
                               FileController::fileName(filePath), tr("当前更改"));
            connect(dv, &DiffViewer::diffClosed, this, [this]() {
                if (m_tabBar) m_tabBar->closeCurrentTab();
            });
            m_tabBar->addCustomTab(dv, tr("Diff: ") + FileController::fileName(filePath), true);
        });
    }

    // 9. 标题栏工具按钮信号槽
    connect(m_titleBar->newButton(),  &QPushButton::clicked, this, &Widget::on_btnNew_clicked);
    connect(m_titleBar->openButton(), &QPushButton::clicked, this, &Widget::on_btnOpen_clicked);
    connect(m_titleBar->saveButton(), &QPushButton::clicked, this, &Widget::on_btnSave_clicked);
    connect(m_titleBar->settingsButton(), &QPushButton::clicked, this, &Widget::onSettingsClicked);
    connect(m_comboBoxEncoding, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Widget::onCurrentIndexChanged);

    // 10. 标题栏窗口控制
    connect(m_titleBar, &TitleBar::minimizeRequested, this, &Widget::onMinimizeRequested);
    connect(m_titleBar, &TitleBar::maximizeRequested, this, &Widget::onMaximizeRequested);
    connect(m_titleBar, &TitleBar::closeRequested, this, &Widget::onCloseRequested);

    // 10.5 标题栏右键菜单
    connect(m_titleBar, &TitleBar::openFolderRequested, this, &Widget::onOpenFolderRequested);
    connect(m_titleBar, &TitleBar::openFileRequested, this, &Widget::on_btnOpen_clicked);
    connect(m_titleBar, &TitleBar::newFileRequested, this, &Widget::on_btnNew_clicked);
    connect(m_titleBar, &TitleBar::saveRequested, this, &Widget::on_btnSave_clicked);
    connect(m_titleBar, &TitleBar::refreshRequested, this, &Widget::onRefreshRequested);
    connect(m_titleBar, &TitleBar::quitRequested, this, &Widget::onQuitRequested);

    // 11. 快捷键（通过 ShortcutFilter 统一管理，Command+Filter+Observer 模式）
    registerShortcutCommands();

    // 11.5 ShortcutFilter 上下文自动切换 (Strategy Pattern)
    // 编辑器焦点 → "editor"  终端焦点 → "terminal"  其他 → "global"
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        QString ctx;
        if (!now) {
            ctx = QStringLiteral("global");
        } else if (qobject_cast<MyTextEdit*>(now)) {
            ctx = QStringLiteral("editor");
        } else if (now->objectName().contains(QStringLiteral("terminal"))
                   || now->inherits("QPlainTextEdit")) {
            ctx = QStringLiteral("terminal");
        } else {
            ctx = QStringLiteral("global");
        }
        ShortcutFilter::instance().setActiveContext(ctx);
    });

    // 12. 窗口状态记忆恢复
    restoreWindowState();

    // 13. 自动保存定时器（根据配置启用）
    if (config.autoSave()) {
        m_autoSaveTimer = new QTimer(this);
        m_autoSaveTimer->setInterval(30000);  // 30秒自动保存
        connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
            if (m_currentTextEdit && m_currentTextEdit->isModified() && m_tabBar) {
                QString path = m_tabBar->currentFilePath();
                if (!path.isEmpty()) on_btnSave_clicked();
            }
        });
        m_autoSaveTimer->start();
    } else {
        m_autoSaveTimer = nullptr;
    }

    // 14. 主题切换 → 刷新所有编辑器行号区 + 全局UI
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &Widget::onThemeChanged);

    // 15. 初始化命令面板
    setupCommandPalette();
}

Widget::~Widget()
{
    saveConfig();
    saveWindowState();
}

// ========== IObserver 观察者接口实现 ==========

void Widget::onUpdate(const QString& event, const QVariant& data)
{
    if (event == "fileOpened") {
        updateTitleForCurrentTab();
        LOG_DEBUG("[Observer] 文件已打开:" << data.toString());
    }
    else if (event == "fileSaved") {
        if (m_tabBar && m_currentTextEdit)
            m_tabBar->setCurrentModified(false);
        updateTitleForCurrentTab();
        LOG_DEBUG("[Observer] 文件已保存");
    }
    else if (event == "encodingChanged") {
        LOG_DEBUG("[Observer] 编码变更:" << data.toString());
    }
}

// ========== UI 构建（VSCode 三栏布局）==========

void Widget::createUi()
{
    auto& config = ConfigManager::instance();

    // === 主垂直布局 ===
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    // ┌─ 第1层：自定义标题栏（含工具按钮+窗口控制）──┐
    m_titleBar = new TitleBar(this);
    m_mainLayout->addWidget(m_titleBar);

    // ┌─ 第2层：中间区域 [SideBar | (EditorTabBar + Terminal)] ───┐
    // 使用水平 QSplitter 实现侧边栏宽度可拖拽调整（VSCode 风格）
    m_hSplitter = new QSplitter(Qt::Horizontal, this);
    m_hSplitter->setObjectName(QStringLiteral("hSplitter"));
    m_hSplitter->setChildrenCollapsible(false);  // 不允许折叠到0

    // 左侧资源栏
    m_sideBar = new SideBar(this);
    // SideBar 自身已设 min/maxWidth，这里不再覆盖
    m_hSplitter->addWidget(m_sideBar);

    // 右侧：编辑区 + 终端 垂直分割
    m_vSplitter = new QSplitter(Qt::Vertical, this);
    m_vSplitter->setObjectName(QStringLiteral("vSplitter"));
    m_vSplitter->setChildrenCollapsible(false);

    // 编辑器标签页栏
    m_tabBar = new EditorTabBar(this);

    // V1.9: 编辑器分栏分割器（m_tabBar | m_splitView）
    // 默认水平方向，m_splitView 隐藏时只显示 m_tabBar
    m_editorSplitter = new QSplitter(Qt::Horizontal, this);
    m_editorSplitter->setObjectName(QStringLiteral("editorSplitter"));
    m_editorSplitter->setChildrenCollapsible(false);
    m_editorSplitter->addWidget(m_tabBar);

    m_splitView = new EditorSplitView(this);
    m_splitView->hide();
    m_editorSplitter->addWidget(m_splitView);
    // 默认全给 m_tabBar，分栏隐藏
    m_editorSplitter->setSizes({500, 0});
    m_editorSplitter->setStretchFactor(0, 1);
    m_editorSplitter->setStretchFactor(1, 1);

    // 分栏关闭信号
    connect(m_splitView, &EditorSplitView::closeRequested,
            this, &Widget::onCloseSplitView);

    m_vSplitter->addWidget(m_editorSplitter);

    // 查找替换面板（V1.9，默认隐藏，Ctrl+F/Ctrl+H 触发）
    m_findReplaceBar = new FindReplaceBar(this);
    m_findReplaceBar->hide();
    m_vSplitter->addWidget(m_findReplaceBar);

    // 欢迎页（无标签时显示，放在编辑区位置）
    m_welcomePage = createWelcomePage();
    m_vSplitter->addWidget(m_welcomePage);

    // 终端面板（VSCode Panel模式：面板标题栏 + 终端内容区）
    m_terminalPanel = createTerminalPanel();
    m_terminalPanel->hide();
    m_vSplitter->addWidget(m_terminalPanel);

    // 默认分割比例：标签栏(35) : 查找栏(隐藏0) : 欢迎页(占满) : 终端(隐藏)
    m_vSplitter->setSizes({35, 0, 500, 0});
    m_vSplitter->setStretchFactor(0, 0);
    m_vSplitter->setStretchFactor(1, 0);
    m_vSplitter->setStretchFactor(2, 1);
    m_vSplitter->setStretchFactor(3, 0);

    m_hSplitter->addWidget(m_vSplitter);   // 编辑区加入水平分割器

    // 默认侧边栏宽度比例：侧边栏 260 : 编辑区 其余（允许拖拽到 160~600）
    m_hSplitter->setSizes({260, 700});
    m_hSplitter->setStretchFactor(0, 0);
    m_hSplitter->setStretchFactor(1, 1);

    m_mainLayout->addWidget(m_hSplitter, 1);

    // 设置窗口最小尺寸，确保小窗口时仍可操作
    setMinimumSize(680, 480);

    // ┌─ 第5层：底部状态栏 ──────────────────────┐
    m_statusBar = UIFactory::createStatusBar(this);
    m_statusBarLayout = new QHBoxLayout(m_statusBar);
    m_statusBarLayout->setContentsMargins(8, 0, 8, 0);
    m_statusBarLayout->setSpacing(4);

    // === 左侧：Git分支 / 问题数 ===
    auto* labelBranch = UIFactory::createStatusLabel(m_statusBar, tr("main"));
    labelBranch->setObjectName(QStringLiteral("statusBranch"));
    labelBranch->setCursor(Qt::PointingHandCursor);

    auto* labelProblems = UIFactory::createStatusLabel(m_statusBar, tr("⚠ 0  ✕ 0"));
    labelProblems->setObjectName(QStringLiteral("statusProblems"));

    // 中间弹簧（推开右侧）
    m_statusBarLayout->addWidget(labelBranch);
    m_statusBarLayout->addWidget(labelProblems);
    m_statusBarLayout->addItem(new QSpacerItem(40, 1, QSizePolicy::Expanding, QSizePolicy::Minimum));

    // === 右侧：修改状态 | 行列号 | 编码 | 换行符 | 语言类型 ===
    m_labelModState     = UIFactory::createStatusLabel(m_statusBar, QString());
    m_labelPosition     = UIFactory::createStatusLabel(m_statusBar, tr("Ln 1, Col 1"));

    // 编码选择器（现代化样式）
    m_comboBoxEncoding  = UIFactory::createEncodingComboBox(m_statusBar);
    m_comboBoxEncoding->setObjectName(QStringLiteral("statusEncodingCombo"));

    // 换行符指示器
    auto* labelEol       = UIFactory::createStatusLabel(m_statusBar, tr("CRLF"));
    labelEol->setObjectName(QStringLiteral("statusEol"));
    labelEol->setCursor(Qt::PointingHandCursor);

    // 语言类型指示器
    auto* labelLang      = UIFactory::createStatusLabel(m_statusBar, tr("纯文本"));
    labelLang->setObjectName(QStringLiteral("statusLang"));
    labelLang->setCursor(Qt::PointingHandCursor);

    // 空格指示器
    auto* labelSpaces     = UIFactory::createStatusLabel(m_statusBar, tr("空格: 4"));
    labelSpaces->setObjectName(QStringLiteral("statusSpaces"));

    m_statusBarLayout->addWidget(m_labelModState);
    m_statusBarLayout->addWidget(m_labelPosition);
    m_statusBarLayout->addWidget(m_comboBoxEncoding);
    m_statusBarLayout->addWidget(labelEol);
    m_statusBarLayout->addWidget(labelLang);
    m_statusBarLayout->addWidget(labelSpaces);

    m_mainLayout->addWidget(m_statusBar);

    // === 窗口属性 ===
    setObjectName(QStringLiteral("mainWindow"));
    resize(1100, 700);   // VSCode 默认尺寸偏宽
}

// ========== 欢迎页创建 ==========

QWidget* Widget::createWelcomePage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("welcomePage"));

    auto* layout = new QVBoxLayout(page);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(16);
    layout->setContentsMargins(40, 40, 40, 40);

    // === 应用图标（QSS 柔和水印效果）===
    auto* iconLabel = new QLabel(page);
    iconLabel->setObjectName(QStringLiteral("welcomeIcon"));
    QPixmap appIcon(QStringLiteral(":/app_icon"));
    iconLabel->setPixmap(appIcon.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setAlignment(Qt::AlignCenter);
    // QSS: 半透明 + 柔和色调
    iconLabel->setStyleSheet(
        QStringLiteral(
            "QLabel#welcomeIcon {"
            "   opacity: 0.35;"
            "}"
            )
    );
    layout->addWidget(iconLabel);

    // === 标题文字 ===
    auto* titleLabel = new QLabel(tr("scNotebook"), page);
    titleLabel->setObjectName(QStringLiteral("welcomeTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(28);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    // === 副标题 ===
    auto* subtitleLabel = new QLabel(tr("现代化代码编辑器"), page);
    subtitleLabel->setObjectName(QStringLiteral("welcomeSubtitle"));
    subtitleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitleLabel);

    layout->addSpacing(20);

    // === 操作按钮行 ===
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);
    btnLayout->setAlignment(Qt::AlignCenter);

    // 打开文件按钮
    auto* btnOpenFile = new QPushButton(tr("打开文件"), page);
    btnOpenFile->setObjectName(QStringLiteral("welcomeBtn"));
    btnOpenFile->setFixedSize(140, 36);
    btnOpenFile->setCursor(Qt::PointingHandCursor);
    connect(btnOpenFile, &QPushButton::clicked, this, &Widget::on_btnOpen_clicked);
    btnLayout->addWidget(btnOpenFile);

    // 打开文件夹按钮
    auto* btnOpenFolder = new QPushButton(tr("打开文件夹"), page);
    btnOpenFolder->setObjectName(QStringLiteral("welcomeBtn"));
    btnOpenFolder->setFixedSize(140, 36);
    btnOpenFolder->setCursor(Qt::PointingHandCursor);
    connect(btnOpenFolder, &QPushButton::clicked, this, [this]() {
        LOG_INFO("[Welcome] 欢迎页「打开文件夹」按钮点击事件已捕获");
        onOpenFolderRequested();
    });
    btnLayout->addWidget(btnOpenFolder);

    // 新建文件按钮
    auto* btnNewFile = new QPushButton(tr("新建文件"), page);
    btnNewFile->setObjectName(QStringLiteral("welcomeBtn"));
    btnNewFile->setFixedSize(140, 36);
    btnNewFile->setCursor(Qt::PointingHandCursor);
    connect(btnNewFile, &QPushButton::clicked, this, &Widget::on_btnNew_clicked);
    btnLayout->addWidget(btnNewFile);

    layout->addLayout(btnLayout);

    layout->addSpacing(24);

    // === 快捷键提示 ===
    auto* tipsLabel = new QLabel(
        tr("快捷键:  Ctrl+O 打开文件   Ctrl+N 新建文件   "
           "Ctrl+` 切换终端   Ctrl+Shift+P 命令面板"),
        page);
    tipsLabel->setObjectName(QStringLiteral("welcomeTips"));
    tipsLabel->setAlignment(Qt::AlignCenter);
    tipsLabel->setWordWrap(true);
    layout->addWidget(tipsLabel);

    // 最近提示
    auto* recentLabel = new QLabel(
        tr("提示: 拖拽文件到窗口可直接打开 | 侧边栏双击文件可编辑"),
        page);
    recentLabel->setObjectName(QStringLiteral("welcomeRecent"));
    recentLabel->setAlignment(Qt::AlignCenter);
    recentLabel->setWordWrap(true);
    layout->addWidget(recentLabel);

    return page;
}

// ========== 终端面板创建（VSCode Panel 模式）==========

QWidget* Widget::createTerminalPanel()
{
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("terminalPanel"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // === 面板标题栏（VSCode 风格：标签 + 关闭按钮）===
    auto* headerBar = new QWidget(panel);
    headerBar->setObjectName(QStringLiteral("panelHeaderBar"));
    headerBar->setFixedHeight(32);

    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(8, 0, 4, 0);
    headerLayout->setSpacing(0);

    // 面板标签（终端 / SSH / 问题 / 输出 / 调试控制台）
    auto* tabTerminal = new QPushButton(tr("终端"), headerBar);
    tabTerminal->setObjectName(QStringLiteral("panelTab"));
    tabTerminal->setCheckable(true);
    tabTerminal->setChecked(true);   // 默认选中终端
    tabTerminal->setCursor(Qt::PointingHandCursor);
    tabTerminal->setFixedHeight(28);

    auto* tabSsh = new QPushButton(tr("SSH"), headerBar);
    tabSsh->setObjectName(QStringLiteral("panelTab"));
    tabSsh->setCheckable(true);
    tabSsh->setCursor(Qt::PointingHandCursor);
    tabSsh->setFixedHeight(28);

    auto* tabProblems = new QPushButton(tr("问题"), headerBar);
    tabProblems->setObjectName(QStringLiteral("panelTab"));
    tabProblems->setCheckable(true);
    tabProblems->setCursor(Qt::PointingHandCursor);
    tabProblems->setFixedHeight(28);

    auto* tabOutput = new QPushButton(tr("输出"), headerBar);
    tabOutput->setObjectName(QStringLiteral("panelTab"));
    tabOutput->setCheckable(true);
    tabOutput->setCursor(Qt::PointingHandCursor);
    tabOutput->setFixedHeight(28);

    // 标签互斥组
    auto* tabGroup = new QButtonGroup(headerBar);
    tabGroup->addButton(tabTerminal, 0);
    tabGroup->addButton(tabSsh, 1);
    tabGroup->addButton(tabProblems, 2);
    tabGroup->addButton(tabOutput, 3);

    headerLayout->addWidget(tabTerminal);
    headerLayout->addWidget(tabSsh);
    headerLayout->addWidget(tabProblems);
    headerLayout->addWidget(tabOutput);
    headerLayout->addStretch();

    // 关闭面板按钮
    auto* btnClosePanel = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"), headerBar);  // ✕
    btnClosePanel->setFixedSize(24, 24);
    btnClosePanel->setCursor(Qt::PointingHandCursor);
    btnClosePanel->setToolTip(tr("关闭面板 (Ctrl+`)"));
    btnClosePanel->setObjectName(QStringLiteral("panelCloseBtn"));
    connect(btnClosePanel, &QPushButton::clicked, this, &Widget::onToggleTerminal);

    headerLayout->addWidget(btnClosePanel);

    layout->addWidget(headerBar);

    // === 内容堆栈（根据标签切换显示不同内容）===
    auto* contentStack = new QStackedWidget(panel);
    contentStack->setObjectName(QStringLiteral("panelContentStack"));

    // --- 终端内容区 ---
    m_terminal = new EmbeddedTerminal(contentStack);
    int terminalIdx = contentStack->addWidget(m_terminal);

    // --- SSH 远程终端内容区 ---
    m_sshTerminal = new SshTerminalWidget(contentStack);
    int sshIdx = contentStack->addWidget(m_sshTerminal);
    // SSH 终端请求配置时，在编辑器标签页中打开配置面板
    connect(m_sshTerminal, &SshTerminalWidget::configDialogRequested,
            this, &Widget::onSshConfigClicked);

    // --- 问题内容区（占位，后续可扩展）---
    auto* problemsPage = new QWidget(contentStack);
    problemsPage->setObjectName(QStringLiteral("panelPlaceholder"));
    auto* problemsLayout = new QVBoxLayout(problemsPage);
    problemsLayout->setAlignment(Qt::AlignCenter);
    auto* problemsLabel = new QLabel(tr("暂无问题"), problemsPage);
    problemsLabel->setObjectName(QStringLiteral("placeholderText"));
    problemsLayout->addWidget(problemsLabel);
    int problemsIdx = contentStack->addWidget(problemsPage);

    // --- 输出内容区（占位）---
    auto* outputPage = new QWidget(contentStack);
    outputPage->setObjectName(QStringLiteral("panelPlaceholder"));
    auto* outputLayout = new QVBoxLayout(outputPage);
    outputLayout->setAlignment(Qt::AlignCenter);
    auto* outputLabel = new QLabel(tr("输出将显示在此处"), outputPage);
    outputLabel->setObjectName(QStringLiteral("placeholderText"));
    outputLayout->addWidget(outputLabel);
    int outputIdx = contentStack->addWidget(outputPage);

    layout->addWidget(contentStack, 1);  // stretch=1 占满剩余空间

    // 标签切换 → 切换内容
    connect(tabGroup, QOverload<int>::of(&QButtonGroup::idClicked), this,
            [contentStack](int id) { contentStack->setCurrentIndex(id); });

    return panel;
}

// ========== 辅助方法 ==========

// ================================================================
// 快捷键注册 (Command + Filter + Observer 设计模式)
//
// 设计模式：
//   Command Pattern  — 每个快捷键封装为 IShortcutCommand
//   Filter Pattern   — ShortcutFilter::eventFilter 统一拦截分发
//   Observer Pattern — ShortcutFilter 监听 ShortcutManager 配置变更
//   Singleton Pattern— ShortcutFilter::instance() 全局唯一
//   Strategy Pattern — "editor" / "terminal" / "global" 上下文切换
// ================================================================

void Widget::registerShortcutCommands()
{
    auto& filter = ShortcutFilter::instance();

    // ===== 文件操作 ("global") =====
    filter.registerCommand(make_command(
        QStringLiteral("file.open"), tr("打开文件"), tr("文件"),
        QKeySequence(Qt::CTRL | Qt::Key_O), QStringLiteral("global"),
        [this]{ on_btnOpen_clicked(); }));

    filter.registerCommand(make_command(
        QStringLiteral("file.save"), tr("保存文件"), tr("文件"),
        QKeySequence(Qt::CTRL | Qt::Key_S), QStringLiteral("global"),
        [this]{ saveCurrentFileDirect(); }));

    filter.registerCommand(make_command(
        QStringLiteral("file.new"), tr("新建文件"), tr("文件"),
        QKeySequence(Qt::CTRL | Qt::Key_N), QStringLiteral("global"),
        [this]{ on_btnNew_clicked(); }));

    filter.registerCommand(make_command(
        QStringLiteral("file.closeTab"), tr("关闭标签页"), tr("文件"),
        QKeySequence(Qt::CTRL | Qt::Key_W), QStringLiteral("global"),
        [this]{ if (m_tabBar) m_tabBar->closeCurrentTab(); }));

    // ===== 编辑操作 ("editor") =====
    filter.registerCommand(make_command(
        QStringLiteral("edit.format"), tr("格式化文档"), tr("编辑"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), QStringLiteral("editor"),
        [this]{ onFormatDocument(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    filter.registerCommand(make_command(
        QStringLiteral("edit.find"), tr("查找"), tr("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_F), QStringLiteral("editor"),
        [this]{ onFindRequested(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    filter.registerCommand(make_command(
        QStringLiteral("edit.replace"), tr("替换"), tr("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_H), QStringLiteral("editor"),
        [this]{ onReplaceRequested(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    // Doxygen 注释生成
    filter.registerCommand(make_command(
        QStringLiteral("edit.doxygen"), tr("生成注释"), tr("编辑"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), QStringLiteral("editor"),
        [this]{
            auto* ed = qobject_cast<MyTextEdit*>(
                m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
            if (ed) ed->insertDoxygenComment();
        },
        [this]{ return m_currentTextEdit != nullptr; }));

    // 切换行注释 Ctrl+/
    filter.registerCommand(make_command(
        QStringLiteral("edit.comment"), tr("切换行注释"), tr("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_Slash), QStringLiteral("editor"),
        [this]{ onToggleLineComment(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    // 复制文件路径 Ctrl+Shift+C
    filter.registerCommand(make_command(
        QStringLiteral("edit.copyPath"), tr("复制文件路径"), tr("编辑"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), QStringLiteral("editor"),
        [this]{ onCopyFilePath(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    // ===== 视图操作 ("global") =====
    filter.registerCommand(make_command(
        QStringLiteral("view.zoomIn"), tr("放大字体"), tr("视图"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal),
        QStringLiteral("global"),
        [this]{ fontUp(); }));

    filter.registerCommand(make_command(
        QStringLiteral("view.zoomOut"), tr("缩小字体"), tr("视图"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus),
        QStringLiteral("global"),
        [this]{ fontDown(); }));

    // ===== 终端操作 ("global") =====
    filter.registerCommand(make_command(
        QStringLiteral("terminal.toggle"), tr("切换终端面板"), tr("终端"),
        QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft),
        QStringLiteral("global"),
        [this]{ onToggleTerminal(); }));

    filter.registerCommand(make_command(
        QStringLiteral("terminal.ssh"), tr("SSH 连接"), tr("终端"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft),
        QStringLiteral("global"),
        [this]{
            if (!m_terminalVisible) onToggleTerminal();
            auto* panel = findChild<QWidget*>("terminalPanel");
            if (panel) {
                auto* tb = panel->findChild<QButtonGroup*>();
                if (tb) tb->button(1)->click();
            }
            onSshConfigClicked();
        }));

    // ===== 全局命令 ====
    filter.registerCommand(make_command(
        QStringLiteral("command.palette"), tr("命令面板"), tr("全局"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),
        QStringLiteral("global"),
        [this]{ onToggleCommandPalette(); }));

    // ===== LSP 代码导航 (L15/L17) — 多语言通用，LspManager 按后缀路由 =====
    filter.registerCommand(make_command(
        QStringLiteral("lsp.gotoDefinition"), tr("跳转到定义"), tr("LSP"),
        QKeySequence(Qt::Key_F12),
        QStringLiteral("editor"),
        [this]{ onLspGotoDefinition(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    filter.registerCommand(make_command(
        QStringLiteral("lsp.findReferences"), tr("查找所有引用"), tr("LSP"),
        QKeySequence(Qt::SHIFT | Qt::Key_F12),
        QStringLiteral("editor"),
        [this]{ onLspFindReferences(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    // V1.9: 编辑器分栏快捷键
    filter.registerCommand(make_command(
        QStringLiteral("view.splitEditor"), tr("切换水平分栏"), tr("视图"),
        QKeySequence(Qt::CTRL | Qt::Key_Backslash),
        QStringLiteral("global"),
        [this]{ onToggleSplitEditor(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    filter.registerCommand(make_command(
        QStringLiteral("view.splitVertical"), tr("切换垂直分栏"), tr("视图"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backslash),
        QStringLiteral("global"),
        [this]{ onToggleVerticalSplit(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    // V1.9: 合并冲突解决
    filter.registerCommand(make_command(
        QStringLiteral("git.resolveConflicts"), tr("解决合并冲突"), tr("Git"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M),
        QStringLiteral("editor"),
        [this]{ onResolveMergeConflicts(); },
        [this]{ return m_currentTextEdit != nullptr; }));

    // 安装到全局 (qApp)，拦截所有按键事件
    filter.installGlobal();

    LOG_INFO("[Widget] 快捷键命令已注册并通过 ShortcutFilter 全局拦截");
}

/// @brief 绑定当前编辑器的所有信号槽（光标位置、修改状态、补全等）
void Widget::bindCurrentEditor(MyTextEdit* editor)
{
    LOG_DEBUG("[Widget] bindCurrentEditor 被调用, editor =" << (void*)editor);
    if (!editor) return;

    // RAII: 先断开旧 editor 的所有连接，防止连接累积
    if (m_currentTextEdit && m_currentTextEdit != editor) {
        auto* old = dynamic_cast<MyTextEdit*>(m_currentTextEdit);
        if (old) {
            disconnect(old, nullptr, this, nullptr);
            if (old->document())
                disconnect(old->document(), nullptr, this, nullptr);
        }
    }

    m_currentTextEdit = editor;  // 隐式转换为 IEditorEdit*

    // 光标位置更新 + 补全器光标跟踪（合并为单连接，避免 Qt::UniqueConnection
    // 对同一 (sender, signal, receiver) 的 lambda 重复连接报警告）
    connect(editor, &MyTextEdit::cursorPositionChangedSignal,
            this, [this]() {
        if (m_completer) m_completer->handleCursorMovement();
        onCursorPositionChanged();
    }, Qt::UniqueConnection);

    // 文本修改状态同步到标签页 + FileOperator + 状态栏
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool changed) {
        if (m_fileOperator) m_fileOperator->setModified(changed);
        if (m_tabBar) m_tabBar->setCurrentModified(changed);
        if (m_labelModState) {
            m_labelModState->setText(changed ? tr("● 已修改") : QString());
        }
        updateTitleForCurrentTab();
    }, Qt::UniqueConnection);

    // 补全器绑定到当前编辑器
    if (m_completer) {
        m_completer->bindEditor(editor);
        // 4-arg 形式：以 this 为 context，确保 disconnect(old, nullptr, this, nullptr) 能断开
        connect(editor, &MyTextEdit::textChangedForCompletion, this, [this]() {
            if (m_completer) m_completer->updateCompletionList();
        }, Qt::UniqueConnection);
    }

    // Ctrl+S 保存请求（编辑器层直接发出，绕过 QShortcut 焦点问题）
    connect(editor, &MyTextEdit::requestSave, this, &Widget::saveCurrentFileDirect, Qt::UniqueConnection);

    // 右键菜单 / 快捷键增强动作
    connect(editor, &MyTextEdit::formatDocumentRequested, this, &Widget::onFormatDocument, Qt::UniqueConnection);
    connect(editor, &MyTextEdit::findRequested,           this, &Widget::onFindRequested, Qt::UniqueConnection);
    connect(editor, &MyTextEdit::replaceRequested,        this, &Widget::onReplaceRequested, Qt::UniqueConnection);

    // 右键菜单新增动作
    connect(editor, &MyTextEdit::copyFilePathRequested,    this, &Widget::onCopyFilePath, Qt::UniqueConnection);
    connect(editor, &MyTextEdit::openInFolderRequested,    this, &Widget::onOpenInFolder, Qt::UniqueConnection);
    connect(editor, &MyTextEdit::toggleLineCommentRequested, this, &Widget::onToggleLineComment, Qt::UniqueConnection);
    connect(editor, &MyTextEdit::toUpperCaseRequested,    this, &Widget::onToUpperCase, Qt::UniqueConnection);
    connect(editor, &MyTextEdit::toLowerCaseRequested,    this, &Widget::onToLowerCase, Qt::UniqueConnection);

    // 注：字体大小变化的全局同步由 ConfigManager::configChanged 统一处理
    // （fontZoomIn/fontZoomOut 已写入 ConfigManager，configChanged 信号触发 allEditors() 同步）
    // SettingsPage 也监听 configChanged 更新 SpinBox，实现双向联动

    // === LSP 信号连接（MyTextEdit → LspManager → LspClient）===
    if (m_lspManager) {
        // 补全请求（Ctrl+Space 触发，MyTextEdit 发射信号带光标位置）
        connect(editor, &MyTextEdit::lspCompletionRequested,
                this, [this](int line, int col) {
            if (!m_lspManager || !m_tabBar) return;
            QString path = m_tabBar->currentFilePath();
            if (!path.isEmpty()) {
                m_lspManager->requestCompletion(path, line, col);
            }
        }, Qt::UniqueConnection);

        // L16: 鼠标悬停请求（500ms 防抖，MyTextEdit 发射信号带光标位置）
        connect(editor, &MyTextEdit::lspHoverRequested,
                this, [this](int line, int col) {
            if (!m_lspManager || !m_tabBar) return;
            QString path = m_tabBar->currentFilePath();
            if (!path.isEmpty() && m_lspManager->hasServerForFile(path)) {
                m_lspManager->requestHover(path, line, col);
            }
        }, Qt::UniqueConnection);

        // Ctrl+左键单击跳转定义（与 F12 等效，复用 onLspGotoDefinition 逻辑）
        connect(editor, &MyTextEdit::lspGotoDefinitionRequested,
                this, [this]() {
            if (m_currentTextEdit) onLspGotoDefinition();
        }, Qt::UniqueConnection);

        // 文档变更同步（防抖 300ms，避免每次按键都发送 didChange）
        // 监听 QTextDocument::contentsChange 而非 MyTextEdit 信号，保持 MyTextEdit 不依赖 LSP
        connect(editor->document(), &QTextDocument::contentsChange,
                this, [this, editor](int, int, int) {
            if (!m_lspManager || !m_tabBar) return;
            QString path = m_tabBar->currentFilePath();
            if (path.isEmpty() || !m_lspManager->hasServerForFile(path)) return;

            // 防抖：每个编辑器一个 QTimer，300ms 内只发送最后一次变更
            static QHash<MyTextEdit*, QTimer*> debounceTimers;
            if (!debounceTimers.contains(editor)) {
                QTimer* t = new QTimer(this);
                t->setSingleShot(true);
                connect(t, &QTimer::timeout, this, [this, editor, t]() {
                    if (!m_tabBar || !editor) return;
                    QString p = m_tabBar->currentFilePath();
                    if (!p.isEmpty()) {
                        m_lspManager->documentChanged(p, editor->toPlainText());
                        // L14: didChange 后延迟请求 documentSymbol，更新语义高亮
                        // 额外 200ms 延迟给服务器处理 didChange 的时间
                        QTimer::singleShot(200, this, [this, p]() {
                            if (m_lspManager && m_lspManager->hasServerForFile(p)) {
                                m_lspManager->requestSymbols(p);
                            }
                        });
                    }
                });
                debounceTimers[editor] = t;
            }
            debounceTimers[editor]->start(300);
        }, Qt::UniqueConnection);
    }

    // 初始单词列表
    QTimer::singleShot(50, editor, &MyTextEdit::updateWordList);
}

void Widget::updateTitleForCurrentTab()
{
    if (!m_tabBar || !m_titleBar) return;

    const TabData* tabData = m_tabBar->currentTabData();
    if (!tabData) {
        m_titleBar->setTitle(QStringLiteral("scNotebook"));
        return;
    }

    QString title = tabData->displayName;
    if (tabData->isModified)
        title += " *";
    m_titleBar->setTitle(title);
}

// ========== 配置管理 ==========

void Widget::loadConfig()
{
    ConfigManager::instance().loadAll();
}

void Widget::saveConfig()
{
    ConfigManager::instance().saveAll();
}

// ========== 字体缩放 ==========

void Widget::fontUp()
{
    if (!m_currentTextEdit) return;
    int size = m_currentTextEdit->fontSize();
    if (size > 0) {
        m_currentTextEdit->setFontSize(size + 1);
        ConfigManager::instance().setFontSize(size + 1);
    }
}

void Widget::fontDown()
{
    if (!m_currentTextEdit) return;
    int size = m_currentTextEdit->fontSize();
    if (size > 1) {
        m_currentTextEdit->setFontSize(size - 1);
        ConfigManager::instance().setFontSize(size - 1);
    }
}

// ========== 窗口状态记忆 ==========

void Widget::restoreWindowState()
{
    auto& config = ConfigManager::instance();
    QString geoStr = config.windowGeometry();
    if (!geoStr.isEmpty()) {
        restoreGeometry(QByteArray::fromBase64(geoStr.toUtf8()));
    }
    // 恢复最大化状态
    if (config.windowMaximized()) {
        showMaximized();
        if (m_titleBar) m_titleBar->updateMaximizeIcon(true);
    }
}

void Widget::saveWindowState()
{
    auto& config = ConfigManager::instance();
    QByteArray geo = saveGeometry();
    config.setWindowGeometry(QString(geo.toBase64()));
    config.setWindowMaximized(isMaximized());
}

// ========== 槽函数：文件操作 ==========

void Widget::on_btnNew_clicked()
{
    // 检查当前标签是否有未保存修改
    if (m_tabBar && m_tabBar->isCurrentModified()) {
        int result = ModernDialog::confirm(this, tr("scNotebook"), tr("是否保存当前文件的更改？"));
        if (result == ModernDialog::ROLE_ACCEPT) {
            on_btnSave_clicked();
        } else if (result == ModernDialog::ROLE_REJECT) {
            return;
        }
    }

    // 新建标签页
    if (m_tabBar) m_tabBar->addNewTab();
}

void Widget::on_btnOpen_clicked()
{
    QString filename = QFileDialog::getOpenFileName(
        this, tr("打开文件"),
        QCoreApplication::applicationDirPath(),
        tr("文本文件 (*.txt *.md *.cpp *.h *.py *.js *.html *.css *.json *.yaml *.ini);;"
           "图片文件 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.ico *.svg);;"
           "所有文件 (*)")
    );
    if (filename.isEmpty()) return;

    // 通过 FileController 统一读取（自动编码检测）
    QString content = FileController::readFile(filename);
    m_tabBar->openFileTab(filename, content);
}

void Widget::on_btnSave_clicked()
{
    if (!m_currentTextEdit || !m_tabBar || !m_fileOperator) return;

    // 点击保存按钮 → 弹出确认弹窗（用户明确操作，需确认）
    QString currentPath = m_tabBar->currentFilePath();
    QString fileName = currentPath.isEmpty() ? tr("未命名文件") : FileController::fileName(currentPath);

    int result = ModernDialog::confirm(this, tr("scNotebook"), tr("是否保存当前文件的更改？"));
    if (result == ModernDialog::ROLE_REJECT) {
        return;  // 取消
    }
    bool doSave = (result == ModernDialog::ROLE_ACCEPT);
    if (doSave) {
        saveCurrentFileDirect();
    } else {
        // 不保存 → 清除修改标记
        m_tabBar->setCurrentModified(false);
        m_currentTextEdit->setModified(false);
        if (m_labelModState) m_labelModState->setText(QString());
        updateTitleForCurrentTab();
    }
}

void Widget::saveCurrentFileDirect()
{
    // Ctrl+S / 命令面板保存 → 直接静默保存（不弹窗）
    LOG_DEBUG("[Widget] saveCurrentFileDirect 开始");

    // 安全兜底：如果缓存的 m_currentTextEdit 为空，尝试从 tabBar 获取当前编辑器
    if (!m_currentTextEdit && m_tabBar) {
        m_currentTextEdit = m_tabBar->currentEditor();
        LOG_DEBUG("[Widget] 从 tabBar 补获编辑器:" << (void*)m_currentTextEdit);
    }

    if (!m_currentTextEdit) { LOG_DEBUG("[Widget] 保存失败: m_currentTextEdit 为空"); return; }
    if (!m_tabBar) { LOG_DEBUG("[Widget] 保存失败: m_tabBar 为空"); return; }
    if (!m_fileOperator) { LOG_DEBUG("[Widget] 保存失败: m_fileOperator 为空"); return; }

    QString currentPath = m_tabBar->currentFilePath();
    int currentIndex = m_tabBar->currentIndex();
    LOG_DEBUG("[Widget] 保存路径:" << currentPath << "当前标签索引:" << currentIndex);

    if (currentPath.isEmpty()) {
        // 没有路径 → 弹出另存为对话框（首次保存必须选位置）
        LOG_DEBUG("[Widget] 路径为空，弹出另存为对话框");
        QString filename = QFileDialog::getSaveFileName(
            this, tr("另存为"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/Files/untitled.txt"),
            tr("文本文件 (*.txt *.md);;所有文件 (*)")
        );
        if (filename.isEmpty()) return;

        m_fileOperator->setEncoding(m_comboBoxEncoding->currentText());

        // 通过 FileController 统一写入
        FileController::writeFile(filename, m_currentTextEdit->toPlainText(),
                                  m_comboBoxEncoding->currentText());

        // 更新标签页路径
        m_tabBar->setCurrentFilePath(filename);
        m_tabBar->setCurrentModified(false);
        m_currentTextEdit->setModified(false);
        if (m_labelModState) m_labelModState->setText(QString());
        updateTitleForCurrentTab();

        // 新文件保存后刷新侧边栏文件列表
        if (m_sideBar) m_sideBar->refreshFileList();
    } else {
        // 直接保存已有路径文件
        LOG_DEBUG("[Widget] 直接保存文件:" << currentPath);
        // T18: 抑制文件监听（内部保存不应触发 reload 提示）
        m_suppressFileWatch = true;
        if (FileController::writeFile(currentPath, m_currentTextEdit->toPlainText(),
                                      m_comboBoxEncoding->currentText())) {
            LOG_DEBUG("[Widget] 文件写入成功");
        }

        m_tabBar->setCurrentModified(false);
        m_currentTextEdit->setModified(false);
        if (m_labelModState) m_labelModState->setText(QString());
        updateTitleForCurrentTab();
        // T18: 恢复文件监听 + 重新添加路径（保存会移除 watcher 中的路径）
        if (m_fileWatcher && !m_fileWatcher->files().contains(currentPath))
            m_fileWatcher->addPath(currentPath);
        QTimer::singleShot(300, this, [this]() { m_suppressFileWatch = false; });
    }

    // 保存后刷新 Git 状态
    if (m_gitPanel) m_gitPanel->refresh();

    // LSP：通知语言服务器文件已保存
    if (m_lspManager && !currentPath.isEmpty()) {
        m_lspManager->documentSaved(currentPath);
    }

    // 自定义头文件符号高亮：保存后重新扫描（用户可能新增/删除了 #include/import）
    // 异步执行，避免阻塞保存流程
    if (!currentPath.isEmpty()) {
        QString savedContent = m_currentTextEdit->toPlainText();
        QTimer::singleShot(0, this, [this, currentPath, savedContent]() {
            if (!m_tabBar) return;
            MyTextEdit* ed = static_cast<MyTextEdit*>(m_tabBar->currentEditor());
            if (!ed) return;
            if (m_tabBar->currentFilePath() != currentPath) return;

            QList<QPair<QString, QString>> externalSymbols =
                HeaderSymbolScanner::scanForExternalSymbols(currentPath, savedContent);
            // 无论是否有符号都调用：有符号则设置，无符号则清除旧规则
            ed->setExternalSymbols(externalSymbols);
        });
    }

    LOG_DEBUG("[Widget] saveCurrentFileDirect 结束");
}

void Widget::onCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (m_fileOperator && m_fileOperator->hasOpenFile())
        m_fileOperator->setEncoding(m_comboBoxEncoding->currentText());
}

// ========== 槽函数：标签页联动 ==========

void Widget::onCurrentEditorChanged(MyTextEdit* editor)
{
    LOG_DEBUG("[Widget] onCurrentEditorChanged 触发, editor =" << (void*)editor);
    // editor 可能为 nullptr（图片预览/SQLite浏览器/Markdown 等特殊标签）
    // 欢迎页显隐由 onTabCountChanged 统一管理，此处不再处理
    if (!editor) {
        m_currentTextEdit = nullptr;
        if (m_sideBar) m_sideBar->clearOutline();  // V1.9: 清空大纲
        return;
    }

    bindCurrentEditor(editor);
    // Tab→Sidebar双向同步：切换Tab时高亮侧边栏对应文件
    if (m_tabBar && m_sideBar) {
        QString filePath = m_tabBar->currentFilePath();
        if (!filePath.isEmpty()) {
            m_sideBar->selectFileByPath(filePath);
        }

        // T18: 更新文件监听 — 监听当前标签页的文件
        if (m_fileWatcher) {
            // 清除旧监听
            if (!m_fileWatcher->files().isEmpty())
                m_fileWatcher->removePaths(m_fileWatcher->files());
            // 添加新文件
            if (!filePath.isEmpty() && FileController::exists(filePath))
                m_fileWatcher->addPath(filePath);
        }
    }

    // V1.9: 刷新大纲面板（LSP 优先，离线 fallback）
    refreshOutlineForCurrentEditor();

    // V1.9: 若分栏视图可见，同步源编辑器
    if (m_splitView && m_splitView->isVisible()) {
        m_splitView->setSourceEditor(editor);
    }
}

void Widget::onTabCountChanged(int count)
{
    LOG_DEBUG("[Widget] 标签页数量变更:" << count);
    // VSCode 模式：统一管理欢迎页显隐（单一入口，覆盖所有场景）
    // 有标签 → 隐藏欢迎页；无标签 → 显示欢迎页
    if (m_welcomePage) {
        m_welcomePage->setVisible(count == 0);
    }
}

void Widget::onAllTabsClosed()
{
    LOG_DEBUG("[Widget] 所有标签已关闭");
    m_currentTextEdit = nullptr;
    // VSCode风格：所有标签关闭后不自动创建新标签，显示欢迎页
    if (m_welcomePage) m_welcomePage->show();
    // T18: 清除文件监听
    if (m_fileWatcher && !m_fileWatcher->files().isEmpty())
        m_fileWatcher->removePaths(m_fileWatcher->files());
    // V1.9: 清空大纲面板
    if (m_sideBar) m_sideBar->clearOutline();
    // V1.9: 关闭分栏视图
    if (m_splitView && m_splitView->isVisible()) {
        onCloseSplitView();
    }
}

// ========== T18: 文件外部修改监听 ==========

void Widget::onFileChangedExternally(const QString& path)
{
    // 抑制内部保存触发的信号
    if (m_suppressFileWatch) return;

    // 文件被删除
    if (!FileController::exists(path)) {
        LOG_DEBUG("[Widget] 文件被外部删除:" << path);
        return;
    }

    // 只处理当前标签页的文件
    if (m_tabBar && m_tabBar->currentFilePath() != path) return;

    LOG_DEBUG("[Widget] 检测到文件外部修改:" << path);

    // 弹窗询问用户是否重新加载
    auto result = ModernDialog::question(
        this, tr("文件已修改"),
        tr("文件 \"%1\" 已被外部程序修改。\n是否重新加载？").arg(FileController::fileName(path))
    );

    if (result == ModernDialog::ROLE_ACCEPT) {
        // 重新加载文件
        if (m_fileOperator) {
            m_suppressFileWatch = true;
            m_fileOperator->openFile(path);
            // 重新添加监听（openFile 可能触发信号）
            QTimer::singleShot(500, this, [this, path]() {
                if (m_fileWatcher && !m_fileWatcher->files().contains(path))
                    m_fileWatcher->addPath(path);
                m_suppressFileWatch = false;
            });
        }
    } else {
        // 用户选择忽略 — 重新添加监听以便下次修改再提示
        if (m_fileWatcher && !m_fileWatcher->files().contains(path))
            m_fileWatcher->addPath(path);
    }
}

void Widget::onFileOpenFromSidebar(const QString& filePath)
{
    LOG_DEBUG("[Widget] 侧边栏打开文件:" << filePath);
    QString content = FileController::readFile(filePath);
    if (content.isNull() && !FileController::exists(filePath)) {
        LOG_DEBUG("[Widget] 文件打开失败:" << filePath);
        return;
    }
    m_tabBar->openFileTab(filePath, content);

    // 自定义头文件符号高亮：扫描 #include/import 引入的本地文件，提取符号名
    // 在标签页打开后立即扫描，结果传给当前编辑器的高亮器
    // 使用 QTimer::singleShot(0) 确保 editor 已完成 enableSyntaxHighlighting 后再设置
    QTimer::singleShot(0, this, [this, filePath, content]() {
        if (!m_tabBar) return;
        MyTextEdit* ed = static_cast<MyTextEdit*>(m_tabBar->currentEditor());
        if (!ed) return;
        // 仅当当前标签页对应刚打开的文件时才应用（防止快速切换标签页错位）
        if (m_tabBar->currentFilePath() != filePath) return;

        QList<QPair<QString, QString>> externalSymbols =
            HeaderSymbolScanner::scanForExternalSymbols(filePath, content);
        if (!externalSymbols.isEmpty()) {
            ed->setExternalSymbols(externalSymbols);
            LOG_DEBUG("[Widget] 外部符号高亮: " << externalSymbols.size()
                      << " 个符号, file=" << filePath.toStdString());
        }
        // 空结果时不调用 setExternalSymbols（避免无意义的 rehighlight）
    });

    // LSP：文件打开时通知语言服务器（按 autoStart 配置决定是否启动）
    if (m_lspManager && ConfigManager::instance().lspAutoStart()) {
        m_lspManager->openFile(filePath, content);
        // L14: 延迟请求 documentSymbol — 服务器需要时间完成 initialize + didOpen 处理
        // 500ms 后请求语义符号，触发语义高亮
        QTimer::singleShot(500, this, [this, filePath]() {
            if (m_lspManager && m_lspManager->hasServerForFile(filePath)) {
                m_lspManager->requestSymbols(filePath);
            }
        });
    }
}

void Widget::onSidebarCreateFile()
{
    // 在Files目录下新建文件
    QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/Files");
    QString filename = QFileDialog::getSaveFileName(
        this, tr("新建文件"), dir + QStringLiteral("/untitled.txt"),
        tr("文本文件 (*.txt *.md);;所有文件 (*)")
    );
    if (filename.isEmpty()) return;

    if (FileController::createFile(filename)) {
        if (m_sideBar) m_sideBar->refreshFileList();
        // 自动打开新建的文件
        onFileOpenFromSidebar(filename);
    }
}

void Widget::onSidebarDeleteFile(const QString& filePath)
{
    int result = ModernDialog::question(this, tr("确认删除"),
        tr("确定要删除 \"%1\" 吗？").arg(FileController::fileName(filePath)));
    if (result == ModernDialog::ROLE_ACCEPT) {
        if (FileController::deleteFile(filePath)) {
            if (m_sideBar) m_sideBar->refreshFileList();
        }
    }
}

void Widget::onSidebarRenameFile(const QString& filePath)
{
    QString currentName = FileController::fileName(filePath);
    bool ok = false;
    QString newName = ModernDialog::getText(
        this, tr("重命名"), tr("新文件名："), currentName, &ok);
    if (newName.isEmpty() || newName == currentName) return;

    QString newPath = FileController::absolutePath(filePath) +
                      QStringLiteral("/") + newName;
    if (FileController::renameFile(filePath, newPath)) {
        if (m_sideBar) m_sideBar->refreshFileList();
    }
}

void Widget::onSidebarOpenInFolder(const QString& filePath)
{
    QString dir = FileController::absolutePath(filePath);
    QProcess::startDetached(QStringLiteral("explorer"), {dir});
}

void Widget::onSidebarCreateFolder()
{
    // V1.9: 在工作目录下新建文件夹
    QString baseDir = m_sideBar ? m_sideBar->currentWorkDir() : QString();
    if (baseDir.isEmpty()) {
        baseDir = QCoreApplication::applicationDirPath() + QStringLiteral("/Files");
        QDir().mkpath(baseDir);
    }

    bool ok = false;
    QString folderName = ModernDialog::getText(
        this, tr("新建文件夹"), tr("文件夹名称："),
        QStringLiteral("newFolder"), &ok);
    if (folderName.isEmpty()) return;

    // 简单校验：禁止非法字符
    if (folderName.contains(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")))) {
        ModernDialog::warning(this, tr("新建文件夹"),
            tr("文件夹名包含非法字符"));
        return;
    }

    QString newPath = baseDir + QStringLiteral("/") + folderName;
    if (QDir().mkpath(newPath)) {
        if (m_sideBar) m_sideBar->refreshFileList();
    } else {
        ModernDialog::warning(this, tr("新建文件夹"),
            tr("无法创建文件夹：") + newPath);
    }
}

void Widget::onSidebarMoveFile(const QString& sourcePath, const QString& targetDir)
{
    // V1.9: 拖拽移动文件到目标文件夹
    QString sourceName = FileController::fileName(sourcePath);
    QString targetPath = targetDir + QStringLiteral("/") + sourceName;

    // 同路径无需移动
    if (FileController::absoluteFilePath(sourcePath) ==
        FileController::absoluteFilePath(targetPath)) {
        return;
    }

    // 目标已存在则提示
    if (FileController::exists(targetPath)) {
        int ret = ModernDialog::question(this, tr("确认覆盖"),
            tr("目标已存在 \"%1\"，是否覆盖？").arg(sourceName));
        if (ret != ModernDialog::ROLE_ACCEPT) return;
    }

    // 通过 FileController 统一移动（内部自动处理覆盖与跨盘符 fallback）
    bool ok = FileController::moveFile(sourcePath, targetPath, true);

    if (ok) {
        // 若该文件已打开在编辑器中，更新其路径
        if (m_tabBar) {
            int idx = m_tabBar->findTabByFilePath(sourcePath);
            if (idx >= 0) {
                m_tabBar->updateTabFilePath(idx, targetPath);
            }
        }
        if (m_sideBar) m_sideBar->refreshFileList();
    } else {
        ModernDialog::warning(this, tr("移动失败"),
            tr("无法移动文件 \"%1\" 到 \"%2\"").arg(sourceName, targetDir));
    }
}

void Widget::onOutlineSymbolClicked(const QString& filePath, int line, int col)
{
    // V1.9: 大纲符号点击 → 跳转到指定位置
    if (filePath.isEmpty() || !m_tabBar) return;

    // 若目标文件与当前文件不同，先打开目标文件
    QString currentPath = m_tabBar->currentFilePath();
    if (currentPath != filePath) {
        if (FileController::exists(filePath)) {
            onFileOpenFromSidebar(filePath);
        } else {
            LOG_DEBUG("[Widget] 大纲跳转：目标文件不存在 " << filePath);
            return;
        }
    }

    if (!m_currentTextEdit) return;
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
    if (!ed) return;

    // 定位光标到目标行/列（LSP 行列从 0 开始）
    QTextCursor cursor = ed->textCursor();
    QTextBlock block = ed->document()->firstBlock();
    for (int i = 0; i < line && block.isValid(); ++i) {
        block = block.next();
    }
    if (block.isValid()) {
        cursor.setPosition(block.position() + qMax(0, col));
        ed->setTextCursor(cursor);
        ed->setFocus();
        // 滚动到目标行（居中显示）
        int scrollPos = line * ed->fontMetrics().lineSpacing();
        ed->verticalScrollBar()->setValue(qMax(0, scrollPos - ed->height() / 3));
    }
}

void Widget::refreshOutlineForCurrentEditor()
{
    // V1.9: 刷新当前编辑器的大纲
    // 优先使用 LSP 符号（精确），无 LSP 时使用离线正则扫描
    if (!m_sideBar || !m_tabBar) return;

    QString filePath = m_tabBar->currentFilePath();
    if (filePath.isEmpty()) {
        m_sideBar->clearOutline();
        return;
    }

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) {
        m_sideBar->clearOutline();
        return;
    }

    // 若有 LSP 服务器，请求符号（结果通过 onLspSymbolsReady 异步返回）
    if (m_lspManager && m_lspManager->hasServerForFile(filePath) &&
        m_lspManager->isServerInitialized(filePath)) {
        m_lspManager->requestSymbols(filePath);
        // 异步：onLspSymbolsReady 会调用 m_sideBar->updateOutline
    } else {
        // 无 LSP：使用离线正则扫描
        QString content = ed->toPlainText();
        m_sideBar->updateOutlineFromText(filePath, content);
    }
}

// ============================================================
// V1.9: 编辑器分栏
// ============================================================

void Widget::onToggleSplitEditor()
{
    // Ctrl+\ 切换水平分栏
    if (m_splitOrientation != Qt::Horizontal) {
        m_splitOrientation = Qt::Horizontal;
        m_editorSplitter->setOrientation(Qt::Horizontal);
    }

    if (m_splitView->isVisible()) {
        onCloseSplitView();
    } else {
        // 显示分栏：共享当前编辑器的 document
        MyTextEdit* ed = qobject_cast<MyTextEdit*>(
            m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
        if (!ed) return;

        m_splitView->setSourceEditor(ed);
        m_splitView->show();
        // 均分宽度
        m_editorSplitter->setSizes({500, 500});
    }
}

void Widget::onToggleVerticalSplit()
{
    // Ctrl+Shift+\ 切换垂直分栏
    if (m_splitOrientation != Qt::Vertical) {
        m_splitOrientation = Qt::Vertical;
        m_editorSplitter->setOrientation(Qt::Vertical);
    }

    if (m_splitView->isVisible()) {
        onCloseSplitView();
    } else {
        MyTextEdit* ed = qobject_cast<MyTextEdit*>(
            m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
        if (!ed) return;

        m_splitView->setSourceEditor(ed);
        m_splitView->show();
        // 均分高度
        m_editorSplitter->setSizes({300, 300});
    }
}

void Widget::onCloseSplitView()
{
    if (!m_splitView->isVisible()) return;

    m_splitView->setSourceEditor(nullptr);
    m_splitView->hide();
    // 全部空间还给主编辑器
    m_editorSplitter->setSizes({500, 0});
}

void Widget::onSplitOrientationChanged()
{
    // 切换分栏方向（水平 ↔ 垂直）
    if (m_splitOrientation == Qt::Horizontal) {
        m_splitOrientation = Qt::Vertical;
    } else {
        m_splitOrientation = Qt::Horizontal;
    }
    m_editorSplitter->setOrientation(m_splitOrientation);
}

void Widget::onAddFolderToWorkspace()
{
    // V1.9: 添加文件夹到工作区
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("添加文件夹到工作区"),
        m_sideBar ? m_sideBar->currentWorkDir() : QString());

    if (dir.isEmpty()) return;

    if (m_sideBar && m_sideBar->addWorkspaceFolder(dir)) {
        LOG_INFO("[Widget] 已添加文件夹到工作区: " << dir.toStdString());
        // P0-2: 如果 LSP 尚未设置工作区根目录，用第一个文件夹初始化
        if (m_lspManager && m_lspManager->workspaceRoot().isEmpty()) {
            m_lspManager->setWorkspaceRoot(dir);
        }
    } else {
        ModernDialog::information(this, tr("添加文件夹"),
            tr("该文件夹已在工作区中"));
    }
}

void Widget::onResolveMergeConflicts()
{
    // V1.9: 解决当前文件的 Git 合并冲突
    if (!m_currentTextEdit) {
        ModernDialog::warning(this, tr("合并冲突解决"),
            tr("请先打开包含冲突的文件"));
        return;
    }

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
    if (!ed) return;

    QString content = ed->toPlainText();

    // 检测冲突
    if (!MergeConflictResolver::hasConflicts(content)) {
        ModernDialog::information(this, tr("合并冲突解决"),
            tr("当前文件未检测到合并冲突标记"));
        return;
    }

    auto conflicts = MergeConflictResolver::detectConflicts(content);
    if (conflicts.isEmpty()) {
        ModernDialog::information(this, tr("合并冲突解决"),
            tr("未检测到有效的冲突区域"));
        return;
    }

    // 弹出选择对话框（使用 QInputDialog 下拉选择）
    QStringList options = {
        tr("接受当前 (Ours) — 保留 HEAD 的代码"),
        tr("接受传入 (Theirs) — 保留传入分支的代码"),
        tr("接受两者 (Ours + Theirs)"),
        tr("接受两者 (Theirs + Ours)")
    };

    QString message = tr("检测到 %1 个冲突区域\n\n"
                         "第一个冲突（行 %2）:\n"
                         "  当前分支:\n%3\n"
                         "  传入分支 (%4):\n%5\n\n"
                         "请选择解决方案（将应用于所有冲突）:")
        .arg(conflicts.size())
        .arg(conflicts.first().startLine + 1)
        .arg(conflicts.first().oursText.left(200))
        .arg(conflicts.first().branchName)
        .arg(conflicts.first().theirsText.left(200));

    bool ok = false;
    QString selected = QInputDialog::getItem(
        this, tr("合并冲突解决"), message, options, 0, false, &ok);

    if (!ok || selected.isEmpty()) return;

    MergeConflictResolver::Resolution resolution;
    int choice = options.indexOf(selected);
    switch (choice) {
    case 0: resolution = MergeConflictResolver::Resolution::AcceptOurs; break;
    case 1: resolution = MergeConflictResolver::Resolution::AcceptTheirs; break;
    case 2: resolution = MergeConflictResolver::Resolution::AcceptBoth; break;
    case 3: resolution = MergeConflictResolver::Resolution::AcceptBothReversed; break;
    default: return;
    }

    // 解决所有冲突
    QString resolved = MergeConflictResolver::resolveAllConflicts(content, resolution);

    // 更新编辑器内容
    QTextCursor cursor = ed->textCursor();
    cursor.select(QTextCursor::Document);
    cursor.insertText(resolved);

    LOG_INFO("[Widget] 已解决 " << conflicts.size() << " 个合并冲突，方案: "
             << MergeConflictResolver::resolutionName(resolution).toStdString());

    ModernDialog::information(this, tr("合并冲突解决"),
        tr("已解决 %1 个冲突区域\n方案：%2\n\n请保存文件以完成解决。")
            .arg(conflicts.size())
            .arg(MergeConflictResolver::resolutionName(resolution)));
}

// ========== 槽函数：光标位置 ==========

void Widget::onCursorPositionChanged()
{
    if (!m_currentTextEdit) return;

    QTextCursor cursor = m_currentTextEdit->textCursor();
    m_labelPosition->setText(tr("Ln %1, Col %2")
                                .arg(cursor.blockNumber() + 1)
                                .arg(cursor.columnNumber() + 1));
}

// ========== 槽函数：窗口控制 ==========

void Widget::onMinimizeRequested() { showMinimized(); }

void Widget::onMaximizeRequested()
{
    if (isMaximized()) {
        showNormal();
        m_titleBar->updateMaximizeIcon(false);
    } else {
        showMaximized();
        m_titleBar->updateMaximizeIcon(true);
    }
}

void Widget::onCloseRequested()
{
    close();  // 触发 closeEvent
}

void Widget::onThemeChanged()
{
    // 主题切换后全链路刷新所有UI组件
    if (m_tabBar) {
        m_tabBar->refreshAllEditors();
    }
    if (m_sideBar) {
        m_sideBar->refreshFileList();
    }

    // 刷新所有编辑器的语法高亮配色
    if (m_tabBar) {
        auto editors = m_tabBar->findChildren<MyTextEdit*>();
        for (auto* ed : editors) {
            ed->updateSyntaxHighlightColors();
        }
    }

    // 注意：全局控件 unpolish/polish 刷新已在 ThemeManager::applyTheme 中完成
    // 此处不再重复遍历，避免双重刷新导致性能浪费和闪烁
}

void Widget::onToggleTerminal()
{
    if (!m_terminal || !m_terminalPanel || !m_vSplitter) return;

    m_terminalVisible = !m_terminalVisible;

    // 同步侧边栏终端按钮状态
    if (m_sideBar) m_sideBar->setTerminalButtonChecked(m_terminalVisible);

    if (m_terminalVisible) {
        // 显示面板：隐藏欢迎页，调整分割比例
        if (m_welcomePage) m_welcomePage->hide();
        m_terminalPanel->show();
        // 设置工作目录（优先使用侧边栏的工作目录）
        QString workDir = m_sideBar ? m_sideBar->currentWorkDir() : QString();
        if (!workDir.isEmpty()) {
            m_terminal->setWorkingDirectory(workDir);
        }
        m_terminal->startSession();
        int h = height() - 36 - 24; // 减去标题栏和状态栏
        // 4 个子控件：editorSplitter(0) : findReplaceBar(1,隐藏) : welcomePage(2,已隐藏) : terminalPanel(3)
        m_vSplitter->setSizes({35, 0, static_cast<int>(h * 0.65), static_cast<int>(h * 0.35)});
    } else {
        // 隐藏面板
        m_terminal->terminateSession();
        m_terminalPanel->hide();
        bool hasTabs = m_tabBar && m_tabBar->tabCount() > 0;
        if (m_welcomePage) m_welcomePage->setVisible(!hasTabs);
        // 4 个子控件：findReplaceBar(1)保持0，欢迎页(2)无标签时占满，终端(3)收起为0
        m_vSplitter->setSizes({35, 0, hasTabs ? 0 : 500, 0});
    }
}

// ========== 窗口关闭事件 ==========

void Widget::closeEvent(QCloseEvent* event)
{
    // 逐个检查标签页是否有未保存修改
    if (m_tabBar) {
        while (m_tabBar->tabCount() > 0) {
            if (!m_tabBar->closeCurrentTab()) {
                event->ignore(); // 用户取消
                return;
            }
        }
    }

    saveWindowState();
    event->accept();
}

void Widget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        // 窗口状态变化时同步最大化按钮图标
        if (m_titleBar) {
            m_titleBar->updateMaximizeIcon(isMaximized());
        }
    }
    FramelessWindow::changeEvent(event);
}

// ========== 拖放事件：拖拽文件到编辑区打开 ==========

void Widget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void Widget::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        QString filePath = url.toLocalFile();
        if (filePath.isEmpty()) continue;

        QFileInfo fi(filePath);
        if (!fi.isFile()) continue;

        QString content = FileController::readFile(filePath);
        m_tabBar->openFileTab(filePath, content);
    }
}

// ========== 槽函数：设置页面 ==========

void Widget::onSettingsClicked()
{
    // 创建设置页面（单例复用）
    if (!m_settingsPage) {
        m_settingsPage = new SettingsPage();

        // 主题切换
        connect(m_settingsPage, &SettingsPage::themeChanged, this, [](const QString& key) {
            ThemeManager::instance().switchTheme(key);
            ConfigManager::instance().setTheme(key);
        });

        // 字体大小变更 → 写入配置（由 onConfigChanged 全局同步到所有编辑器）
        connect(m_settingsPage, &SettingsPage::fontSizeChanged, this, [this](int size) {
            ConfigManager::instance().setFontSize(size);
        });

        // 监听配置变更 — 全局同步字体大小到所有编辑器（仅连接一次，避免 UniqueConnection 警告）
        connect(&ConfigManager::instance(), &ConfigManager::configChanged,
                this, [this](const QString& key, const QVariant& value) {
            if (key == QStringLiteral("Display/fontSize")) {
                int size = value.toInt();
                if (size <= 0) return;  // 修复：非法字体大小兜底
                if (m_tabBar) {
                    for (MyTextEdit* ed : m_tabBar->allEditors()) {
                        if (ed && ed->fontSize() != size) {
                            QSignalBlocker blocker(ed);  // 阻止递归信号
                            ed->setFontSize(size);
                        }
                    }
                }
            }
        });
    }

    // 打开设置时隐藏欢迎页（设置全屏覆盖编辑区）
    if (m_welcomePage) m_welcomePage->hide();

    // 在标签栏中打开（复用现有标签）
    m_tabBar->addCustomTab(m_settingsPage, tr("设置"), true);
}

// ========== 槽函数：SSH配置面板 ==========

void Widget::onSshConfigClicked()
{
    // 创建SSH配置面板（单例复用，对标设置页面模式）
    if (!m_sshConfigPanel) {
        m_sshConfigPanel = new SshConfigPanel();

        // 连接成功 → 传给 SSH 终端建立连接
        connect(m_sshConfigPanel, &SshConfigPanel::connectRequested,
                this, [this](const SshConnectionConfig& config) {
            if (m_sshTerminal && m_sshTerminal->connectToHost(config)) {
                // 连接成功后关闭配置标签页
                if (m_tabBar) {
                    int idx = m_tabBar->findCustomTabIndex(tr("SSH 配置"));
                    if (idx >= 0) m_tabBar->closeTab(idx);
                }
            }
        });
    }

    // 隐藏欢迎页
    if (m_welcomePage) m_welcomePage->hide();

    // 在编辑器标签栏中打开（对标设置页面）
    m_tabBar->addCustomTab(m_sshConfigPanel, tr("SSH 配置"), true);
}

void Widget::onOpenFolderRequested()
{
    LOG_INFO("[Widget] onOpenFolderRequested 触发，准备打开文件夹选择对话框");
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("打开文件夹"),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (dir.isEmpty()) {
        LOG_DEBUG("[Widget] 文件夹选择已取消");
        return;
    }
    LOG_INFO("[Widget] 已选择文件夹: " << dir.toStdString());
    if (m_sideBar) {
        m_sideBar->setWorkDirectory(dir);
        // 选择文件夹后隐藏欢迎页（与侧边栏按钮行为一致）
        if (m_welcomePage) m_welcomePage->hide();
        // P0-2: 同步工作区根目录到 LSP 管理器，使 clangd 使用正确的项目根目录
        if (m_lspManager) {
            m_lspManager->setWorkspaceRoot(dir);
        }
    } else {
        LOG_WARN("[Widget] m_sideBar 为空，无法设置工作目录");
    }
}

void Widget::onRefreshRequested()
{
    if (m_sideBar) {
        m_sideBar->refreshFileList();
    }
}

void Widget::onQuitRequested()
{
    close();
}

// ========== 命令面板相关 ==========

void Widget::setupCommandPalette()
{
    // 创建命令面板实例
    m_commandPalette = new CommandPalette(this);

    // 注册命令（至少15个）
    // --- 文件类 ---
    m_commandPalette->registerCommand({"file.open",     tr("打开文件"),      "Ctrl+O",       tr("文件")});
    m_commandPalette->registerCommand({"file.new",      tr("新建文件"),      "Ctrl+N",       tr("文件")});
    m_commandPalette->registerCommand({"file.save",     tr("保存"),          "Ctrl+S",       tr("文件")});
    m_commandPalette->registerCommand({"file.saveAs",   tr("另存为"),        QString(),      tr("文件")});

    // --- 编辑类 ---
    m_commandPalette->registerCommand({"edit.undo",     tr("撤销"),          "Ctrl+Z",       tr("编辑")});
    m_commandPalette->registerCommand({"edit.redo",     tr("重做"),          "Ctrl+Y",       tr("编辑")});
    m_commandPalette->registerCommand({"edit.find",     tr("查找"),          "Ctrl+F",       tr("编辑")});
    m_commandPalette->registerCommand({"edit.replace",  tr("替换"),          "Ctrl+H",       tr("编辑")});
    m_commandPalette->registerCommand({"edit.format",   tr("格式化文档"),    "Ctrl+Shift+I", tr("编辑")});
    m_commandPalette->registerCommand({"edit.doxygen",  tr("生成注释"),      "Ctrl+Shift+D", tr("编辑")});
    m_commandPalette->registerCommand({"edit.comment",  tr("切换行注释"),    "Ctrl+/",       tr("编辑")});
    m_commandPalette->registerCommand({"edit.copyPath", tr("复制文件路径"),  "Ctrl+Shift+C", tr("编辑")});
    m_commandPalette->registerCommand({"edit.upper",    tr("转换为大写"),    QString(),      tr("编辑")});
    m_commandPalette->registerCommand({"edit.lower",    tr("转换为小写"),    QString(),      tr("编辑")});
    m_commandPalette->registerCommand({"edit.openFolder", tr("在文件管理器中打开"), QString(), tr("编辑")});

    // --- 视图类 ---
    m_commandPalette->registerCommand({"view.toggleSidebar",   tr("切换侧边栏"),   QString(),      tr("视图")});
    m_commandPalette->registerCommand({"view.toggleTerminal",  tr("切换终端"),     "Ctrl+`",       tr("视图")});
    m_commandPalette->registerCommand({"view.toggleWelcome",   tr("欢迎页"),       QString(),      tr("视图")});
    m_commandPalette->registerCommand({"view.diffCompare",     tr("文件对比"),     QString(),      tr("视图")});

    // --- 终端类 ---
    m_commandPalette->registerCommand({"term.new",         tr("新建终端"),     QString(),      tr("终端")});
    m_commandPalette->registerCommand({"term.clear",       tr("清屏"),         QString(),      tr("终端")});
    m_commandPalette->registerCommand({"term.switchType",  tr("切换Shell"),    QString(),      tr("终端")});

    // --- 设置类 ---
    m_commandPalette->registerCommand({"settings.open",   tr("打开设置"),     QString(),      tr("设置")});

    // --- LSP 类 ---
    m_commandPalette->registerCommand({"lsp.gotoDefinition",  tr("跳转到定义"),   "F12",          tr("LSP")});
    m_commandPalette->registerCommand({"lsp.findReferences",  tr("查找所有引用"), "Shift+F12",    tr("LSP")});

    // --- 其他 ---
    m_commandPalette->registerCommand({"theme.next",      tr("切换下一主题"), QString(),      tr("其他")});
    m_commandPalette->registerCommand({"exit",            tr("退出"),         QString(),      tr("其他")});

    // --- M9: 正则表达式测试器 ---
    m_commandPalette->registerCommand({"tools.regexTester",  tr("正则测试器"),   QString(),      tr("工具")});

    // --- M14: 代码片段 ---
    m_commandPalette->registerCommand({"snippet.insert:",   tr("插入片段..."),  QString(),      tr("工具")});
    m_commandPalette->registerCommand({"snippet.manage",    tr("管理片段"),     QString(),      tr("工具")});

    // 连接信号
    connect(m_commandPalette, &CommandPalette::commandTriggered,
            this, &Widget::onCommandTriggered);

    // 注册命令处理器到 CommandRegistry（哈希表替代 if-else 链）
    registerCommands();
}

void Widget::registerCommands()
{
    // === 文件类 ===
    m_commandRegistry.registerCommand(QStringLiteral("file.open"),    [this]{ on_btnOpen_clicked(); });
    m_commandRegistry.registerCommand(QStringLiteral("file.new"),     [this]{ on_btnNew_clicked(); });
    m_commandRegistry.registerCommand(QStringLiteral("file.save"),    [this]{ saveCurrentFileDirect(); });
    m_commandRegistry.registerCommand(QStringLiteral("file.saveAs"),  [this]{
        if (!m_currentTextEdit) return;
        if (m_tabBar) m_tabBar->setCurrentFilePath(QString());
        on_btnSave_clicked();
    });

    // === 编辑类 ===
    m_commandRegistry.registerCommand(QStringLiteral("edit.undo"), [this]{
        QKeyEvent* e = new QKeyEvent(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QCoreApplication::postEvent(m_currentTextEdit ? m_currentTextEdit->asWidget() : this, e);
    });
    m_commandRegistry.registerCommand(QStringLiteral("edit.redo"), [this]{
        QKeyEvent* e = new QKeyEvent(QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier);
        QCoreApplication::postEvent(m_currentTextEdit ? m_currentTextEdit->asWidget() : this, e);
    });
    m_commandRegistry.registerCommand(QStringLiteral("edit.find"),       [this]{ onFindRequested(); });
    m_commandRegistry.registerCommand(QStringLiteral("edit.replace"),    [this]{ onReplaceRequested(); });
    m_commandRegistry.registerCommand(QStringLiteral("edit.format"),     [this]{ onFormatDocument(); });
    m_commandRegistry.registerCommand(QStringLiteral("edit.doxygen"), [this]{
        auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
        if (ed) ed->insertDoxygenComment();
    });
    m_commandRegistry.registerCommand(QStringLiteral("edit.comment"),    [this]{ onToggleLineComment(); });
    m_commandRegistry.registerCommand(QStringLiteral("edit.copyPath"),   [this]{ onCopyFilePath(); });
    m_commandRegistry.registerCommand(QStringLiteral("edit.upper"),      [this]{ onToUpperCase(); });
    m_commandRegistry.registerCommand(QStringLiteral("edit.lower"),      [this]{ onToLowerCase(); });
    m_commandRegistry.registerCommand(QStringLiteral("edit.openFolder"), [this]{ onOpenInFolder(); });

    // === 视图类 ===
    m_commandRegistry.registerCommand(QStringLiteral("view.toggleSidebar"), [this]{
        if (m_sideBar) m_sideBar->setVisible(!m_sideBar->isVisible());
    });
    m_commandRegistry.registerCommand(QStringLiteral("view.toggleTerminal"), [this]{ onToggleTerminal(); });
    m_commandRegistry.registerCommand(QStringLiteral("view.toggleWelcome"), [this]{
        if (m_welcomePage) m_welcomePage->setVisible(!m_welcomePage->isVisible());
    });
    m_commandRegistry.registerCommand(QStringLiteral("view.diffCompare"), [this]{
        QString path1 = QFileDialog::getOpenFileName(this, tr("选择原始文件"));
        if (path1.isEmpty()) return;
        QString path2 = QFileDialog::getOpenFileName(this, tr("选择修改后文件"));
        if (path2.isEmpty()) return;
        openDiffView(path1, path2);
    });

    // === 终端类 ===
    m_commandRegistry.registerCommand(QStringLiteral("term.new"),    [this]{ if (m_terminal) m_terminal->startSession(); });
    m_commandRegistry.registerCommand(QStringLiteral("term.clear"),  [this]{ if (m_terminal) m_terminal->executeCommand(QStringLiteral("cls")); });
    m_commandRegistry.registerCommand(QStringLiteral("term.switchType"), []{ LOG_DEBUG("[CommandPalette] 切换Shell类型（待实现）"); });

    // === 设置类 ===
    m_commandRegistry.registerCommand(QStringLiteral("settings.open"), [this]{ onSettingsClicked(); });

    // === 主题/退出 ===
    m_commandRegistry.registerCommand(QStringLiteral("theme.next"), [this]{
        auto& tm = ThemeManager::instance();
        QStringList keys = tm.themeKeys();
        int idx = keys.indexOf(ConfigManager::instance().theme());
        int nextIdx = (idx + 1) % keys.size();
        tm.switchTheme(keys[nextIdx]);
        ConfigManager::instance().setTheme(keys[nextIdx]);
    });
    m_commandRegistry.registerCommand(QStringLiteral("exit"), [this]{ close(); });

    // === 工具类 ===
    m_commandRegistry.registerCommand(QStringLiteral("tools.regexTester"), [this]{
        auto* regexTester = new RegexTester();
        if (m_welcomePage) m_welcomePage->hide();
        m_tabBar->addCustomTab(regexTester, tr("正则测试器"), true);
    });

    // === 代码片段 ===
    m_commandRegistry.registerCommand(QStringLiteral("snippet.manage"), [this]{
        auto& sm = SnippetManager::instance();
        QList<CodeSnippet> snippets = sm.allSnippets();
        QStringList items;
        for (const CodeSnippet& s : snippets) {
            items.append(QStringLiteral("[%1] %2 - %3").arg(s.language, s.prefix, s.name));
        }
        bool ok = false;
        QString selected = ModernDialog::getItem(this, tr("代码片段"),
            tr("选择一个片段（%1 个可用）:").arg(snippets.size()), items, 0, &ok);
        if (ok && !selected.isEmpty()) {
            int idx = items.indexOf(selected);
            if (idx >= 0 && idx < snippets.size()) {
                QString expanded = sm.expandSnippet(snippets[idx]);
                if (m_currentTextEdit) m_currentTextEdit->textCursor().insertText(expanded);
            }
        }
    });
    m_commandRegistry.registerPrefixCommand(QStringLiteral("snippet.insert:"), [this](const QString& keyword){
        auto& sm = SnippetManager::instance();
        QList<CodeSnippet> results = sm.search(keyword);
        if (results.isEmpty()) {
            ModernDialog::information(this, tr("代码片段"),
                tr("未找到匹配 \"%1\" 的片段").arg(keyword));
            return;
        }
        QStringList items;
        for (const CodeSnippet& s : results) {
            items.append(QStringLiteral("%1 [%2] %3").arg(s.name, s.language, s.prefix));
        }
        bool ok = false;
        QString selected = ModernDialog::getItem(this, tr("插入代码片段"),
            tr("选择要插入的片段:"), items, 0, &ok);
        if (ok && !selected.isEmpty()) {
            int idx = items.indexOf(selected);
            if (idx >= 0 && idx < results.size()) {
                QString expanded = sm.expandSnippet(results[idx]);
                if (m_currentTextEdit) m_currentTextEdit->textCursor().insertText(expanded);
            }
        }
    });

    LOG_INFO("[Widget] CommandRegistry 已注册" << m_commandRegistry.commandIds().size() << "个命令");
}

void Widget::onToggleCommandPalette()
{
    if (!m_commandPalette) return;

    if (m_commandPalette->isVisible()) {
        m_commandPalette->hidePalette();
    } else {
        m_commandPalette->showPalette();
    }
}

void Widget::onCommandTriggered(const QString& commandId)
{
    LOG_DEBUG("[CommandPalette] 命令触发:" << commandId);
    m_commandRegistry.execute(commandId);
}

// ========== M4: 代码格式化 ==========

void Widget::onFormatDocument()
{
    if (!m_currentTextEdit || !m_tabBar) return;
    EditorActions::formatDocument(m_currentTextEdit, m_tabBar->currentFilePath());
}

// ====================================================================
// 查找 / 替换
// ====================================================================

void Widget::onFindRequested()
{
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed || !m_findReplaceBar) return;

    m_findReplaceBar->setEditor(ed);
    m_findReplaceBar->showFind();
}

void Widget::onReplaceRequested()
{
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed || !m_findReplaceBar) return;

    m_findReplaceBar->setEditor(ed);
    m_findReplaceBar->showReplace();
}

// ========== 右键菜单新增动作 ==========

void Widget::onCopyFilePath()
{
    if (!m_tabBar) return;
    QString path = m_tabBar->currentFilePath();
    if (path.isEmpty()) {
        ModernDialog::information(this, tr("复制文件路径"),
            tr("当前文件尚未保存，无路径可复制。"));
        return;
    }
    QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    LOG_INFO("[Widget] 已复制文件路径到剪贴板: " << path.toStdString());
}

void Widget::onOpenInFolder()
{
    if (!m_tabBar) return;
    QString path = m_tabBar->currentFilePath();
    if (path.isEmpty()) {
        ModernDialog::information(this, tr("在文件管理器中打开"),
            tr("当前文件尚未保存，无目录可打开。"));
        return;
    }
    QString dir = FileController::absolutePath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void Widget::onToggleLineComment()
{
    if (!m_currentTextEdit) return;
    QString path = m_tabBar ? m_tabBar->currentFilePath() : QString();
    EditorActions::toggleLineComment(m_currentTextEdit, path);
}

void Widget::onToUpperCase()
{
    EditorActions::toUpperCase(m_currentTextEdit);
}

void Widget::onToLowerCase()
{
    EditorActions::toLowerCase(m_currentTextEdit);
}

// ========== LSP 语言服务器响应槽 ==========

void Widget::onLspCompletionsReady(const QString& filePath, const QList<LspCompletionItem>& items)
{
    // LSP 补全结果 → 注入 TextCompleter 候选列表（优先于本地词典显示）
    LOG_DEBUG("[Widget] LSP 补全结果: " << items.size() << " 项, file=" << filePath.toStdString());

    auto* completerImpl = dynamic_cast<TextCompleter*>(m_completer);
    if (completerImpl) {
        completerImpl->setLspCompletionItems(items);
    }
}

void Widget::onLspDiagnosticsReady(const QString& filePath, const QList<LspDiagnostic>& diagnostics)
{
    // LSP 诊断 → 转换为编辑器覆盖层格式 → MyTextEdit::setDiagnostics
    LOG_DEBUG("[Widget] LSP 诊断: " << diagnostics.size() << " 条, file=" << filePath.toStdString());

    if (!m_tabBar) return;

    // 类型转换：LspDiagnostic → LspDiagnosticOverlay
    // 注意：枚举值不同（LspDiagnostic 从 0 开始，Overlay 从 1 开始）
    QList<LspDiagnosticOverlay> overlays;
    overlays.reserve(diagnostics.size());
    for (const LspDiagnostic& d : diagnostics) {
        LspDiagnosticOverlay o;
        o.startLine = d.line;
        o.startCol = d.column;
        o.endLine = d.endLine;
        o.endCol = d.endColumn;
        // LspDiagnostic::Error=0 → Overlay::Error=1, Warning=1→2, Information=2→3, Hint=3→4
        o.severity = static_cast<LspDiagnosticOverlay::Severity>(static_cast<int>(d.severity) + 1);
        o.message = d.message;
        if (!d.source.isEmpty())
            o.message = QStringLiteral("[%1] %2").arg(d.source, d.message);
        overlays.append(o);
    }

    // 修复 P2-1: 遍历所有打开的标签页，将诊断分发到匹配 filePath 的编辑器
    // clangd 后台索引会推送非当前标签页的诊断，旧逻辑只更新当前标签页导致后台 tab 诊断丢失
    int count = m_tabBar->tabCount();
    for (int i = 0; i < count; ++i) {
        const TabData* td = m_tabBar->tabDataAt(i);
        if (!td || !td->editor) continue;
        // 路径标准化比较（避免大小写/斜杠差异导致匹配失败）
        if (QDir::toNativeSeparators(td->filePath) ==
            QDir::toNativeSeparators(filePath)) {
            td->editor->setDiagnostics(overlays);
            return;  // 文件路径唯一，匹配到即返回
        }
    }

    // 未找到匹配的标签页（可能是头文件等未打开的诊断推送），静默忽略
}

void Widget::onLspDefinitionReady(const QString& filePath, const QString& uri, int line, int col)
{
    // L15: 跳转定义响应 — 解析 URI → 文件路径 → 打开文件 → 定位光标
    LOG_DEBUG("[Widget] LSP 跳转定义: uri=" << uri.toStdString()
              << " line=" << line << " col=" << col);

    if (uri.isEmpty()) return;

    // file:// URI → 本地文件路径
    QString targetPath = uri;
    if (targetPath.startsWith(QStringLiteral("file:///"))) {
        // Windows: file:///C:/path → C:/path
        targetPath = QUrl(uri).toLocalFile();
    } else if (targetPath.startsWith(QStringLiteral("file://"))) {
        targetPath = QUrl(uri).toLocalFile();
    }

    if (targetPath.isEmpty()) return;

    // 如果目标文件与当前文件不同，打开目标文件
    QString currentPath = m_tabBar ? m_tabBar->currentFilePath() : QString();
    if (currentPath != targetPath) {
        // 通过侧边栏打开文件路径（复用现有文件打开逻辑）
        if (FileController::exists(targetPath)) {
            onFileOpenFromSidebar(targetPath);
        } else {
            LOG_DEBUG("[Widget] 跳转目标文件不存在: " << targetPath);
            return;
        }
    }

    // 定位光标到目标行/列（LSP 行列从 0 开始，编辑器从 1 开始）
    if (!m_currentTextEdit) return;
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
    if (!ed) return;

    QTextCursor cursor = ed->textCursor();
    int blockPos = 0;
    QTextBlock block = ed->document()->firstBlock();
    for (int i = 0; i < line && block.isValid(); ++i) {
        blockPos = block.position() + block.length();
        block = block.next();
    }
    if (block.isValid()) {
        cursor.setPosition(block.position() + qMax(0, col));
        ed->setTextCursor(cursor);
        ed->setFocus();
        // 居中显示目标行
        cursor.movePosition(QTextCursor::StartOfBlock);
        ed->setTextCursor(cursor);
        // 滚动到目标行
        int scrollPos = line * ed->fontMetrics().lineSpacing();
        ed->verticalScrollBar()->setValue(qMax(0, scrollPos - ed->height() / 3));
    }
}

void Widget::onLspHoverReady(const QString& filePath, const QString& documentation)
{
    // L16: 悬停文档响应 — 在光标位置显示 QToolTip
    LOG_DEBUG("[Widget] LSP 悬停文档: " << documentation.left(80).toStdString()
              << " file=" << filePath.toStdString());

    if (documentation.isEmpty()) return;

    if (!m_tabBar || m_tabBar->currentFilePath() != filePath) return;

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;

    // 在光标位置显示工具提示
    QPoint cursorPos = ed->cursorRect().bottomLeft();
    cursorPos = ed->mapToGlobal(cursorPos);
    QToolTip::showText(cursorPos, documentation, ed);
}

void Widget::onLspReferencesReady(const QString& filePath, const QList<QVariantMap>& references)
{
    // L17: 引用查找响应 — 弹出引用列表对话框
    LOG_DEBUG("[Widget] LSP 引用结果: " << references.size() << " 处, file=" << filePath.toStdString());

    if (references.isEmpty()) {
        LOG_INFO("[Widget] 未找到引用");
        return;
    }

    // 构建引用列表对话框（简单 QListDialog 风格）
    QDialog dlg(this);
    dlg.setWindowTitle(tr("查找引用 (%1 处)").arg(references.size()));
    dlg.setMinimumSize(600, 400);

    auto* layout = new QVBoxLayout(&dlg);
    auto* listWidget = new QListWidget(&dlg);
    listWidget->setAlternatingRowColors(true);

    // 解析每个引用位置
    for (const QVariantMap& ref : references) {
        QString refUri = ref.value(QStringLiteral("uri")).toString();
        QVariantMap range = ref.value(QStringLiteral("range")).toMap();
        QVariantMap start = range.value(QStringLiteral("start")).toMap();
        int refLine = start.value(QStringLiteral("line")).toInt() + 1;  // 转为 1-based
        int refCol = start.value(QStringLiteral("character")).toInt() + 1;

        // URI → 文件路径
        QString refPath = QUrl(refUri).toLocalFile();
        QString fileName = FileController::fileName(refPath);

        // 显示格式: 文件名:行号:列号  —  完整路径
        QString display = QStringLiteral("%1:%2:%3  —  %4")
            .arg(fileName).arg(refLine).arg(refCol).arg(refPath);
        QListWidgetItem* item = new QListWidgetItem(display, listWidget);
        item->setData(Qt::UserRole, refPath);
        item->setData(Qt::UserRole + 1, refLine);
        item->setData(Qt::UserRole + 2, refCol);
    }

    layout->addWidget(listWidget);

    auto* btnLayout = new QHBoxLayout();
    auto* btnClose = new QPushButton(tr("关闭"), &dlg);
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    layout->addLayout(btnLayout);

    // 双击跳转
    connect(listWidget, &QListWidget::itemDoubleClicked, this, [this, &dlg](QListWidgetItem* item) {
        QString path = item->data(Qt::UserRole).toString();
        int line = item->data(Qt::UserRole + 1).toInt();
        int col = item->data(Qt::UserRole + 2).toInt();

        if (FileController::exists(path)) {
            onFileOpenFromSidebar(path);
            // 跳转到目标位置
            if (m_currentTextEdit) {
                MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
                if (ed) {
                    QTextCursor cursor = ed->textCursor();
                    QTextBlock block = ed->document()->firstBlock();
                    for (int i = 0; i < line - 1 && block.isValid(); ++i) {
                        block = block.next();
                    }
                    if (block.isValid()) {
                        cursor.setPosition(block.position() + qMax(0, col - 1));
                        ed->setTextCursor(cursor);
                        ed->setFocus();
                    }
                }
            }
        }
        dlg.accept();
    });

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

void Widget::onLspSymbolsReady(const QString& filePath, const QList<QVariantMap>& symbols)
{
    // L12-L14: LSP 文档符号 → 语义高亮
    // 解析 documentSymbol 响应，将符号信息传递给当前编辑器的高亮器
    LOG_DEBUG("[Widget] LSP 文档符号: " << symbols.size() << " 个, file=" << filePath.toStdString());

    // 只更新当前标签页对应的编辑器（如果文件路径匹配）
    if (!m_tabBar) return;
    QString currentPath = m_tabBar->currentFilePath();
    if (currentPath != filePath) return;

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;

    // 转发符号列表给编辑器 → CodeSyntaxHighlighter 解析并重高亮
    ed->setSemanticSymbols(symbols);

    // V1.9: 同时更新侧边大纲面板
    if (m_sideBar) {
        m_sideBar->updateOutline(filePath, symbols);
    }
}

void Widget::onLspServerError(const QString& filePath, const QString& error)
{
    LOG_ERROR("[Widget] LSP 服务器错误: " << error.toStdString()
              << " file=" << filePath.toStdString());
}

// ============================================================
// L15/L17: LSP 代码导航触发
// ============================================================

void Widget::onLspGotoDefinition()
{
    // F12 跳转定义 — 获取光标位置，请求 LSP definition
    if (!m_lspManager || !m_tabBar) return;

    QString path = m_tabBar->currentFilePath();
    if (path.isEmpty() || !m_lspManager->hasServerForFile(path)) {
        LOG_DEBUG("[Widget] F12 跳转定义: 当前文件无 LSP 服务器");
        return;
    }

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;

    // LSP 行列从 0 开始，编辑器从 1 开始
    int line = ed->currentLine() - 1;
    int col = ed->currentColumn() - 1;
    m_lspManager->requestDefinition(path, line, col);
}

void Widget::onLspFindReferences()
{
    // Shift+F12 查找引用 — 获取光标位置，请求 LSP references
    if (!m_lspManager || !m_tabBar) return;

    QString path = m_tabBar->currentFilePath();
    if (path.isEmpty() || !m_lspManager->hasServerForFile(path)) {
        LOG_DEBUG("[Widget] Shift+F12 查找引用: 当前文件无 LSP 服务器");
        return;
    }

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;

    int line = ed->currentLine() - 1;
    int col = ed->currentColumn() - 1;
    m_lspManager->requestReferences(path, line, col);
}

// ========== M5: Diff 视图 ==========

void Widget::openDiffView(const QString& path1, const QString& path2)
{
    // 通过 FileController 统一读取两个文件内容
    QString text1 = FileController::readFile(path1);
    if (text1.isNull())
        text1 = tr("（无法读取: %1）").arg(path1);

    QString text2 = FileController::readFile(path2);
    if (text2.isNull())
        text2 = tr("（无法读取: %1）").arg(path2);

    // 创建 DiffViewer 并在标签页中打开
    auto* diffViewer = new DiffViewer();
    diffViewer->setDiffContent(text1, text2,
                               FileController::fileName(path1),
                               FileController::fileName(path2));

    // 连接关闭信号
    connect(diffViewer, &DiffViewer::diffClosed, this, [this]() {
        if (m_tabBar) {
            m_tabBar->closeCurrentTab();
        }
    });

    // 在标签栏中打开
    m_tabBar->addCustomTab(diffViewer,
                           tr("对比: %1 ↔ %2")
                               .arg(FileController::fileName(path1))
                               .arg(FileController::fileName(path2)),
                           true);
}
