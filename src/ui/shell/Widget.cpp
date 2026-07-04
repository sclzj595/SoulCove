#include "ui/shell/Widget.h"
#include "ui/editor/MyTextEdit.h"
#include "ui/editor/TextCompleter.h"
#include "ui/editor/HoverPopup.h"  // H1: Markdown 悬停预览弹窗
#include "ui/editor/DefinitionPreviewPopup.h"  // C03-6: 定义预览弹窗

#include "core/config/ConfigManager.h"
#include "core/fileio/FileOperator.h"
#include "core/config/ThemeManager.h"
#include "core/i18n/I18nManager.h"  // P3-M05: 国际化管理器
#include "core/workspace/WorkspaceManager.h"
#include "core/shortcut/ShortcutFilter.h"
#include "factory/UIFactory.h"
#include "controller/EditorActions.h"
#include "controller/FileController.h"
#include "controller/LspCoordinator.h"
#include "controller/IdleTabTracker.h"  // R4: 闲置标签页追踪器
#include "ui/settings/SettingsPage.h"
#include "ui/tools/DiffViewer.h"
#include "ui/tools/RegexTester.h"
#include "ui/editor/FindReplaceBar.h"
#include "ui/editor/EditorSplitView.h"
#include "ui/sidebar/ExplorerPanel.h"
#include "core/vcs/MergeConflictResolver.h"
#include "core/vcs/GitManager.h"
#include "core/snippet/SnippetManager.h"
#include "ui/snippet/SnippetManagerDialog.h"
#include "ui/remote/SshConfigPanel.h"
#include "core/remote/SshSessionManager.h"      // P3-M01 子项4: 已保存会话列表
#include "core/remote/SshClient.h"              // P3-M01 子项4: 建立连接挂载工作区
#include "core/remote/SftpClient.h"             // P3-M01 子项4: SFTP 同步
#include "core/remote/RemoteWorkspaceManager.h" // P3-M01 子项4: 远程工作区挂载管理器
#include "core/editor/HeaderSymbolScanner.h"
#include "core/editor/EditorConfigParser.h"  // P3-M03 子项4: .editorconfig 解析
#include "core/debug/PerformanceMonitor.h"  // C02-4 性能监控
#include "core/debug/DebugManager.h"        // P3-M04 子项3: GDB MI 调试管理器
#include "ui/debug/DebugView.h"             // P3-M04 子项3: 调试视图面板
#include "ui/sidebar/TasksPanel.h"          // P3-M04 子项1: 任务面板（jumpToLocationRequested 信号）
#include "Logger.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>           // P3-M01 子项4: QInputDialog::getText 的 EchoMode 参数
#include "ui/dialog/ModernDialog.h"
#include <QPushButton>
#include <QAbstractButton>
#include <QShortcut>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QCloseEvent>
#include <QEvent>
#include <QProcess>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QFile>
#include <QTextBlock>
#include <QCursor>
#include <QMouseEvent>   // P3-M03 子项1: 事件过滤器中 static_cast<QMouseEvent*>
#include <QMenu>         // P3-M03 子项1: EOL 切换菜单
#include <QAction>       // P3-M03 子项1: EOL 切换菜单项
#include <QToolTip>
#include <QDialog>
#include <QListWidget>
#include <QPlainTextEdit>  // C02-4 性能监控报告显示
#include <QStyle>
#include "core/base/ScreenGuard.h"
#include <QButtonGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QTextCursor>
#include <QTextDocument>
#include <QDir>
#include <QRegularExpression>
#include <QDateTime>
#include <QLocale>               // P3-M05: 本地化时间格式
#include <QFontDatabase>
#include <QtConcurrent>          // P2-H03 子项3: QtConcurrent::run 异步 blame
#include <QFutureWatcher>        // P2-H03 子项3: 监听 blame 异步结果

// ========== 构造 / 析构 ==========

Widget::Widget(const ProductConfig& config, QWidget *parent)
    : FramelessWindow(parent), m_currentTextEdit(nullptr),
      m_settingsPage(nullptr), m_productConfig(config)
{
    // 0. 安装屏幕安全守卫 — 防止跨屏/高DPI时GDI崩溃
    auto* screenGuard = new ScreenGuard(this);
    screenGuard->installOn(this);

    // 0.5 显式设置窗口图标 — 确保 Windows 任务栏/标题栏/DWM 正确显示产品图标
    //    （QApplication::setWindowIcon 在某些 DWM 无边框场景下不生效，需在主窗口显式设置）
    setWindowIcon(QIcon(QStringLiteral(":/app_icon")));

    // 1. 初始化配置管理器（单例）
    loadConfig();

    // 附加: DirectWrite 字体兜底 — 设置应用默认字体为现代 TrueType 字体，
    // 避免 Qt DirectWrite 引擎尝试加载旧版光栅字体（Fixedsys/Modern 等）导致加载失败日志和渲染抖动
    {
        // 按优先级排列的 TrueType 等宽字体回退链（均为 DirectWrite 兼容）
        QStringList preferredFamilies = {
            QStringLiteral("Consolas"),
            QStringLiteral("Cascadia Code"),
            QStringLiteral("Cascadia Mono"),
            QStringLiteral("JetBrains Mono"),
            QStringLiteral("Source Code Pro"),
            QStringLiteral("Courier New")
        };
        // 过滤出系统已安装的字体，确保回退链不包含不存在的字体
        QStringList available;
        const QStringList sysFamilies = QFontDatabase::families();
        for (const QString& fam : preferredFamilies) {
            if (sysFamilies.contains(fam)) {
                available << fam;
            }
        }
        if (available.isEmpty()) {
            available << QStringLiteral("Consolas");  // 最终兜底
        }

        QFont appFont;
        appFont.setFamilies(available);
        appFont.setStyleHint(QFont::Monospace);     // 等宽提示，避免回退到光栅字体
        appFont.setStyleStrategy(QFont::PreferAntialias);  // 优先抗锯齿（TrueType）
        appFont.setPointSize(ConfigManager::instance().fontSize());
        // P3-M05: 根据 uiScaleFactor 缩放字体（100/125/150%）
        // 在 setPointSize 之后再乘以缩放因子，影响所有派生字体
        int scale = ConfigManager::instance().uiScaleFactor();
        if (scale != 100 && scale > 0) {
            appFont.setPointSizeF(appFont.pointSizeF() * scale / 100.0);
        }
        QApplication::setFont(appFont);  // 设为应用全局默认字体
    }

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
    if (m_productConfig.completion && cfg.showCompletion()) {
        auto* completerImpl = new TextCompleter(this);
        completerImpl->setWindowFlags(completerImpl->windowFlags() | Qt::WindowStaysOnTopHint);
        m_completer = completerImpl;
    } else {
        m_completer = nullptr;
    }

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
    // P0-4: 标签关闭时通知 LSP 发送 didClose，释放 clangd 文档内存
    connect(m_tabBar, &EditorTabBar::fileClosed,
            this, [this](const QString& filePath) {
        if (m_lspCoordinator) {
            m_lspCoordinator->closeFile(filePath);
            LOG_DEBUG("[Widget] LSP didClose: " << filePath.toStdString());
        }
    });
    // R4: 闲置检测已提取到 IdleTabTracker，在 LSP 协调器创建后初始化

    // 7. 初始化 LSP 协调器（拥有 LspManager，下沉信号路由逻辑）
    //    根据产品线配置条件化创建 — notebook/editor 不需要 LSP
    if (m_productConfig.lsp) {
        m_lspCoordinator = new LspCoordinator(this);
        // 依赖注入：协调器需要 tabBar 查找编辑器、completer 注入补全候选
        // 注：m_tabBar 和 m_completer 在后续步骤创建，此处先延迟到创建完成后注入
        // UI 级响应信号 → Widget 处理（跳转/悬停/引用/大纲/错误）
        connect(m_lspCoordinator, &LspCoordinator::definitionReady,
                this, &Widget::onLspDefinitionReady);
        connect(m_lspCoordinator, &LspCoordinator::hoverReady,
                this, &Widget::onLspHoverReady);
        connect(m_lspCoordinator, &LspCoordinator::referencesReady,
                this, &Widget::onLspReferencesReady);
        connect(m_lspCoordinator, &LspCoordinator::symbolsReady,
                this, &Widget::onLspSymbolsReady);
        connect(m_lspCoordinator, &LspCoordinator::serverError,
                this, &Widget::onLspServerError);
        // LSP 服务器不可用时弹窗提示（仅提示一次，避免刷屏）
        connect(m_lspCoordinator, &LspCoordinator::serverNotAvailable,
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
        // R3: lspStateChanged 已下沉到 LspCoordinator 内部路由
        // Widget 不再连接此信号，消除硬编码语言匹配逻辑（开闭原则）
    }

    // R4: 创建闲置标签页追踪器（观察者模式，解耦 EditorTabBar 与 LSP 生命周期）
    // 仅在有 LSP 的产品线创建，追踪非当前标签闲置时间，超时自动 didClose
    if (m_productConfig.lsp && m_lspCoordinator) {
        m_idleTabTracker = new IdleTabTracker(m_tabBar, this);
        // 闲置超时 → LSP didClose 释放内存
        connect(m_idleTabTracker, &IdleTabTracker::fileIdle,
                this, [this](const QString& filePath) {
            if (m_lspCoordinator) {
                m_lspCoordinator->closeFile(filePath);
                m_idleTabTracker->markAsReleased(filePath);
            }
        });
        // 闲置标签重新激活 → LSP didOpen 重新打开文档
        connect(m_idleTabTracker, &IdleTabTracker::fileReactivated,
                this, [this](const QString& filePath, const QString& content) {
            if (m_lspCoordinator && ConfigManager::instance().lspAutoStart()) {
                m_lspCoordinator->openFile(filePath, content);
                m_idleTabTracker->markAsReactivated(filePath);
                // 重新请求文档符号以恢复语义高亮
                QTimer::singleShot(500, this, [this, filePath]() {
                    if (m_lspCoordinator && m_lspCoordinator->hasServerForFile(filePath) &&
                        m_lspCoordinator->isServerInitialized(filePath)) {
                        m_lspCoordinator->requestSymbols(filePath);
                    }
                });
            }
        });
    }

    // H1: 创建 Markdown 悬停预览弹窗（替代 QToolTip 原始文本输出）
    // 仅在有 LSP 的产品线创建，富文本渲染 LSP 返回的 markdown 文档
    if (m_productConfig.lsp) {
        m_hoverPopup = new HoverPopup(this);
        // C03-6: 创建定义预览弹窗（Ctrl+Alt+Click 触发，悬浮显示定义代码片段）
        m_definitionPreview = new DefinitionPreviewPopup(this);
    }

    // 8. 侧边栏信号连接（根据产品线配置条件化 — notebook 无侧边栏）
    if (m_sideBar) {
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
                    // P0-2: 同步工作区根目录到 LSP 协调器，使 clangd 使用正确的项目根目录
                    if (m_lspCoordinator && m_sideBar) {
                        m_lspCoordinator->setWorkspaceRoot(m_sideBar->currentWorkDir());
                    }
                });

        // Git 面板：获取 SideBar 内嵌的 GitPanel 并连接信号
        m_gitPanel = m_sideBar->gitPanelWidget();

        // P3-M04 子项1: 连接 TasksPanel 双击输出行的错误跳转信号
        TasksPanel* tasksPanel = m_sideBar->tasksPanel();
        if (tasksPanel) {
            connect(tasksPanel, &TasksPanel::jumpToLocationRequested,
                    this, &Widget::onJumpToLocation);
        }
    }

    // P3-M04 子项3: 创建 DebugManager 实例（IDE 专属）
    if (m_productConfig.terminal) {
        m_debugManager = new DebugManager(this);
        if (m_debugView) {
            m_debugView->setDebugManager(m_debugManager);
            // DebugView 请求开始调试 → 弹文件选择
            connect(m_debugView, &DebugView::startDebugRequested,
                    this, &Widget::onDebugStartRequested);
            // 调试器命中断点 → 跳转到对应文件:行
            connect(m_debugView, &DebugView::jumpToLocationRequested,
                    this, &Widget::onDebugBreakpointHit);
            // DebugManager 状态变更 → DebugView 自动通过 setDebugManager 连接处理
        }
    }
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

        // P2-H03 子项2: 历史面板双击提交 → 打开该提交与父提交的 diff
        connect(m_gitPanel, &GitPanel::commitDiffRequested,
                this, [this](const QString& commitHash,
                              const QString& commitHashShort,
                              const QString& commitMessage) {
            QString wsRoot = gitWorkspaceRoot();
            QString diffText = GitManager::instance().commitDiff(wsRoot, commitHash);
            if (diffText.isEmpty()) {
                ModernDialog::information(this, tr("Diff"),
                    tr("无法获取提交 %1 的 diff").arg(commitHashShort));
                return;
            }
            auto* dv = new DiffViewer();
            // git show 输出已含 diff，作为「修改后」版本展示
            dv->setDiffContent(tr("（父提交）"), diffText,
                               tr("提交 %1").arg(commitHashShort), commitMessage);
            m_tabBar->addCustomTab(dv, tr("Diff: %1").arg(commitHashShort), true);
        });
    }

    // P2-H03 子项1: Git 状态栏实时刷新 — 监听 repoChanged 信号 + 5s 周期定时器
    connect(&GitManager::instance(), &GitManager::repoChanged,
            this, [this]() { updateGitStatusBar(); });
    m_gitStatusBarTimer = new QTimer(this);
    m_gitStatusBarTimer->setInterval(5000);  // 5 秒周期刷新（防止外部 git 命令修改状态）
    connect(m_gitStatusBarTimer, &QTimer::timeout, this, &Widget::updateGitStatusBar);
    m_gitStatusBarTimer->start();
    QTimer::singleShot(0, this, [this]() { updateGitStatusBar(); });  // 启动后立即刷新一次

    // P2-H03 子项3: 初始化 blame 异步监听器
    m_blameWatcher = new QFutureWatcher<QList<GitBlameLine>>(this);
    connect(m_blameWatcher, &QFutureWatcher<QList<GitBlameLine>>::finished,
            this, &Widget::onBlameFinished);

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
    // P2-H04: 工作区持久化菜单
    connect(m_titleBar, &TitleBar::saveWorkspaceRequested, this, &Widget::onSaveWorkspaceRequested);
    connect(m_titleBar, &TitleBar::openWorkspaceRequested, this, &Widget::onOpenWorkspaceRequested);
    // P3-M01 子项4: 文件菜单「挂载远程工作区」
    connect(m_titleBar, &TitleBar::mountRemoteWorkspaceRequested,
            this, &Widget::onMountRemoteWorkspaceRequested);

    // P3-M05: 视图菜单语言切换 — 调用 I18nManager 切换 + 持久化 + 提示重启
    connect(m_titleBar, &TitleBar::languageChangeRequested,
            this, [this](const QString& langCode) {
        // 持久化新语言（含 "system"）
        ConfigManager::instance().setLanguage(langCode);
        // 立即切换翻译器与 QLocale（已构造 UI 仍需重启完全生效）
        I18nManager::instance().switchLanguage(langCode);
        // 提示用户重启应用以完全应用新语言
        ModernDialog::information(
            this,
            tr("提示"),
            tr("语言已切换，需要重启应用以完全生效")
        );
    });

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
    if (ConfigManager::instance().autoSave()) {
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

    // 16. C03-5: 恢复持久化的导航栈（跨会话保留跳转历史）
    // 注：上限 50 条，与现有 push 时截断逻辑一致；无效路径在使用时由 navigateBack/Forward 兜底过滤
    {
        QList<QPair<QString, QPair<int,int>>> saved =
            ConfigManager::instance().loadNavigationStack();
        for (const auto& e : saved) {
            m_navStack.append({ e.first, e.second.first, e.second.second });
        }
        if (m_navStack.size() > 50) {
            m_navStack = m_navStack.mid(m_navStack.size() - 50);
        }
    }

    // 17. P2-H04: 启动时提示恢复最近工作区（延迟到事件循环，避免阻塞构造）
    // 仅在有侧边栏（文件树）的产品线生效，notebook 无文件树跳过
    if (m_sideBar) {
        QTimer::singleShot(0, this, [this]() { promptRestoreLastWorkspace(); });
    }
}

Widget::~Widget()
{
    // V2.1 M2/M3: 保存侧边栏状态（析构时也有可能被调用）
    if (m_sideBar) {
        m_sideBar->savePanelStates();
    }
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

    // 左侧资源栏（根据产品线配置条件化创建）
    if (m_productConfig.fileTree) {
        m_sideBar = new SideBar(this);
        // SideBar 自身已设 min/maxWidth，这里不再覆盖
        m_hSplitter->addWidget(m_sideBar);
    } else {
        m_sideBar = nullptr;
    }

    // 右侧：编辑区 + 终端 垂直分割
    m_vSplitter = new QSplitter(Qt::Vertical, this);
    m_vSplitter->setObjectName(QStringLiteral("vSplitter"));
    m_vSplitter->setChildrenCollapsible(false);

    // 编辑器标签页栏
    m_tabBar = new EditorTabBar(this);

    // 依赖注入：LspCoordinator 需要 tabBar 查找编辑器、completer 注入补全候选
    // （m_lspCoordinator 在前序步骤已创建，此处补齐依赖）
    if (m_lspCoordinator) {
        m_lspCoordinator->setTabBar(m_tabBar);
        if (auto* completerImpl = dynamic_cast<TextCompleter*>(m_completer)) {
            m_lspCoordinator->setCompleter(completerImpl);
        }
    }

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

    // V2.1: 大纲面板已迁移至 SideBar/ExplorerPanel 内部（VSCode 风格）
    // 此处仅保留防抖定时器（文本变更 200ms 后刷新大纲）
    if (m_productConfig.outline) {
        m_outlineDebounceTimer = new QTimer(this);
        m_outlineDebounceTimer->setSingleShot(true);
        m_outlineDebounceTimer->setInterval(200);
        connect(m_outlineDebounceTimer, &QTimer::timeout,
                this, &Widget::onOutlineRefreshDebounced);
    }

    // 查找替换面板（V1.9，默认隐藏，Ctrl+F/Ctrl+H 触发）
    m_findReplaceBar = new FindReplaceBar(this);
    m_findReplaceBar->hide();
    m_vSplitter->addWidget(m_findReplaceBar);

    // 欢迎页（无标签时显示，放在编辑区位置）
    m_welcomePage = createWelcomePage();
    m_vSplitter->addWidget(m_welcomePage);

    // 终端面板（VSCode Panel模式：面板标题栏 + 终端内容区）
    // 根据产品线配置条件化创建 — notebook/editor 不需要终端
    if (m_productConfig.terminal) {
        m_terminalPanel = createTerminalPanel();
        m_terminalPanel->hide();
        m_vSplitter->addWidget(m_terminalPanel);
    } else {
        m_terminalPanel = nullptr;
    }

    // P3-M04 子项3: 调试面板（与终端面板同级，IDE 专属，默认隐藏）
    // 通过 F5 / Ctrl+Shift+D 等调试快捷键触发显示
    if (m_productConfig.terminal) {
        m_debugPanel = createDebugPanel();
        m_debugPanel->hide();
        m_vSplitter->addWidget(m_debugPanel);
    }

    // 默认分割比例：标签栏(35) : 查找栏(隐藏0) : 欢迎页(占满) : 终端(隐藏) : 调试(隐藏)
    // 索引：[0]editorSplitter [1]findReplaceBar [2]welcomePage [3]terminalPanel [4]debugPanel
    // V2.1: 大纲已迁移至 SideBar/ExplorerPanel，不再占用 m_vSplitter 空间
    m_vSplitter->setSizes({35, 0, 500, 0, 0});
    m_vSplitter->setStretchFactor(0, 0);
    m_vSplitter->setStretchFactor(1, 0);
    m_vSplitter->setStretchFactor(2, 1);
    m_vSplitter->setStretchFactor(3, 0);
    m_vSplitter->setStretchFactor(4, 0);

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
    m_labelBranch = UIFactory::createStatusLabel(m_statusBar, tr("main"));
    m_labelBranch->setObjectName(QStringLiteral("statusBranch"));
    m_labelBranch->setCursor(Qt::PointingHandCursor);
    m_labelBranch->setToolTip(tr("当前 Git 分支与修改文件数"));

    auto* labelProblems = UIFactory::createStatusLabel(m_statusBar, tr("⚠ 0  ✕ 0"));
    labelProblems->setObjectName(QStringLiteral("statusProblems"));

    // 中间弹簧（推开右侧）
    m_statusBarLayout->addWidget(m_labelBranch);
    m_statusBarLayout->addWidget(labelProblems);
    m_statusBarLayout->addItem(new QSpacerItem(40, 1, QSizePolicy::Expanding, QSizePolicy::Minimum));

    // === 右侧：修改状态 | 行列号 | 编码 | 换行符 | 语言类型 ===
    m_labelModState     = UIFactory::createStatusLabel(m_statusBar, QString());
    m_labelPosition     = UIFactory::createStatusLabel(m_statusBar, tr("Ln 1, Col 1"));

    // 编码选择器（现代化样式）
    m_comboBoxEncoding  = UIFactory::createEncodingComboBox(m_statusBar);
    m_comboBoxEncoding->setObjectName(QStringLiteral("statusEncodingCombo"));

    // 换行符指示器（P3-M03 子项1: 改为可点击 QLabel + 下拉菜单选择 LF/CRLF/CR）
    m_labelEol       = UIFactory::createStatusLabel(m_statusBar, tr("LF"));
    m_labelEol->setObjectName(QStringLiteral("statusEol"));
    m_labelEol->setCursor(Qt::PointingHandCursor);
    m_labelEol->setToolTip(tr("点击切换行尾序列 (LF / CRLF / CR)"));
    // P3-M03 子项1: 鼠标按下时弹出 EOL 选择菜单
    m_labelEol->installEventFilter(this);
    // 兼容性：通过 QWidget::mousePressEvent 不可靠（QLabel 默认不转发），改用事件过滤器

    // 语言类型指示器
    auto* labelLang      = UIFactory::createStatusLabel(m_statusBar, tr("纯文本"));
    labelLang->setObjectName(QStringLiteral("statusLang"));
    labelLang->setCursor(Qt::PointingHandCursor);

    // 空格指示器
    m_labelSpaces     = UIFactory::createStatusLabel(m_statusBar, tr("空格: 4"));
    m_labelSpaces->setObjectName(QStringLiteral("statusSpaces"));

    // P3-M05: 状态栏时钟（本地化格式）— 显示当前时间，每秒刷新
    m_labelClock     = UIFactory::createStatusLabel(m_statusBar, QString());
    m_labelClock->setObjectName(QStringLiteral("statusClock"));
    m_labelClock->setToolTip(tr("当前时间"));
    // 立即填充一次，避免首次显示空白
    m_labelClock->setText(QLocale().toString(
        QDateTime::currentDateTime(), QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    // 时钟刷新定时器：1 秒间隔，秒级对齐
    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, [this]() {
        if (m_labelClock) {
            m_labelClock->setText(QLocale().toString(
                QDateTime::currentDateTime(), QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        }
    });
    m_clockTimer->start();

    m_statusBarLayout->addWidget(m_labelModState);
    m_statusBarLayout->addWidget(m_labelPosition);
    m_statusBarLayout->addWidget(m_comboBoxEncoding);
    m_statusBarLayout->addWidget(m_labelEol);
    m_statusBarLayout->addWidget(labelLang);
    m_statusBarLayout->addWidget(m_labelSpaces);
    m_statusBarLayout->addWidget(m_labelClock);

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
    // 修复：空值校验，避免 QPixmap::scaled: Pixmap is a null pixmap 警告
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
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
    auto* titleLabel = new QLabel(tr("SoulCove"), page);
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

// ========== P3-M04 子项3: 调试面板 ==========

QWidget* Widget::createDebugPanel()
{
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("debugPanel"));

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

    auto* tabDebug = new QPushButton(tr("调试控制台"), headerBar);
    tabDebug->setObjectName(QStringLiteral("panelTab"));
    tabDebug->setCheckable(true);
    tabDebug->setChecked(true);
    tabDebug->setCursor(Qt::PointingHandCursor);
    tabDebug->setFixedHeight(28);
    headerLayout->addWidget(tabDebug);
    headerLayout->addStretch();

    // 关闭面板按钮
    auto* btnClosePanel = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"), headerBar);  // ✕
    btnClosePanel->setFixedSize(24, 24);
    btnClosePanel->setCursor(Qt::PointingHandCursor);
    btnClosePanel->setToolTip(tr("关闭面板 (Ctrl+Shift+D)"));
    btnClosePanel->setObjectName(QStringLiteral("panelCloseBtn"));
    connect(btnClosePanel, &QPushButton::clicked, this, &Widget::onToggleDebugPanel);

    headerLayout->addWidget(btnClosePanel);

    layout->addWidget(headerBar);

    // === 调试视图内容区 ===
    m_debugView = new DebugView(panel);
    layout->addWidget(m_debugView, 1);  // stretch=1 占满剩余空间

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

    // J2: 导航回退 — Ctrl+← 返回上一处光标位置（跳转定义后回退）
    filter.registerCommand(make_command(
        QStringLiteral("navigation.goBack"), tr("返回上一处位置"), tr("LSP"),
        QKeySequence(Qt::CTRL | Qt::Key_Left),
        QStringLiteral("editor"),
        [this]{ navigateBack(); },
        [this]{ return !m_navStack.isEmpty(); }));

    // P0 C03: 导航前进 — Ctrl+→ 前进到下一处光标位置（回退后可前进）
    filter.registerCommand(make_command(
        QStringLiteral("navigation.goForward"), tr("前进到下一处位置"), tr("LSP"),
        QKeySequence(Qt::CTRL | Qt::Key_Right),
        QStringLiteral("editor"),
        [this]{ navigateForward(); },
        [this]{ return !m_navForwardStack.isEmpty(); }));

    // P0 C03: 跳转实现 — Ctrl+F12 请求 LSP textDocument/implementation
    filter.registerCommand(make_command(
        QStringLiteral("editor.gotoImplementation"), tr("跳转到实现"), tr("LSP"),
        QKeySequence(Qt::CTRL | Qt::Key_F12),
        QStringLiteral("editor"),
        [this]{ onLspGotoImplementation(); },
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

    // Bug4: F11 全屏编辑模式切换 — 无边框全屏 + 隐藏标题栏，再次按 F11 恢复
    // P3-M04 子项3: 调试会话活跃时让出 F11 给「单步进入」（避免冲突）
    filter.registerCommand(make_command(
        QStringLiteral("view.toggleFullScreen"), tr("切换全屏"), tr("视图"),
        QKeySequence(Qt::Key_F11),
        QStringLiteral("global"),
        [this]{ onToggleFullScreen(); },
        [this]{ return !m_debugManager || !m_debugManager->isActive(); }));

    // C02-4: 性能监控面板 — Alt+Shift+P 弹出统计报告（调试模式）
    // 注：Ctrl+Shift+P 已被命令面板占用，改用 Alt+Shift+P
    filter.registerCommand(make_command(
        QStringLiteral("debug.performanceMonitor"), tr("性能监控面板"), tr("调试"),
        QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_P),
        QStringLiteral("global"),
        [this]{ onShowPerformanceMonitor(); }));

    // === P3-M04 子项3: 调试快捷键 ===
    // F5: 开始调试 / 继续（按当前状态自动切换）
    filter.registerCommand(make_command(
        QStringLiteral("debug.startContinue"), tr("开始调试 / 继续"), tr("调试"),
        QKeySequence(Qt::Key_F5),
        QStringLiteral("global"),
        [this]{ onDebugStart(); }));

    // F10: 单步跳过
    filter.registerCommand(make_command(
        QStringLiteral("debug.stepOver"), tr("单步跳过"), tr("调试"),
        QKeySequence(Qt::Key_F10),
        QStringLiteral("global"),
        [this]{ onDebugStepOver(); },
        [this]{ return m_debugManager && m_debugManager->isActive(); }));

    // F11: 单步进入（注意：F11 已被「全屏编辑模式」占用，调试优先级更高，
    //      但为避免破坏全屏体验，调试仅在被调试会话活跃时拦截 F11）
    filter.registerCommand(make_command(
        QStringLiteral("debug.stepInto"), tr("单步进入"), tr("调试"),
        QKeySequence(Qt::Key_F11),
        QStringLiteral("global"),
        [this]{ onDebugStepInto(); },
        [this]{ return m_debugManager && m_debugManager->isActive(); }));

    // Shift+F11: 单步跳出
    filter.registerCommand(make_command(
        QStringLiteral("debug.stepOut"), tr("单步跳出"), tr("调试"),
        QKeySequence(Qt::SHIFT | Qt::Key_F11),
        QStringLiteral("global"),
        [this]{ onDebugStepOut(); },
        [this]{ return m_debugManager && m_debugManager->isActive(); }));

    // Shift+F5: 停止调试
    filter.registerCommand(make_command(
        QStringLiteral("debug.stop"), tr("停止调试"), tr("调试"),
        QKeySequence(Qt::SHIFT | Qt::Key_F5),
        QStringLiteral("global"),
        [this]{ onDebugStop(); },
        [this]{ return m_debugManager && m_debugManager->isActive(); }));

    // Ctrl+Shift+D: 切换调试面板
    filter.registerCommand(make_command(
        QStringLiteral("debug.togglePanel"), tr("切换调试面板"), tr("调试"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
        QStringLiteral("global"),
        [this]{ onToggleDebugPanel(); }));

    // 安装到全局 (qApp)，拦截所有按键事件
    filter.installGlobal();

    LOG_INFO("[Widget] 快捷键命令已注册并通过 ShortcutFilter 全局拦截");
}

/// @brief 绑定当前编辑器的所有信号槽（光标位置、修改状态、补全等）
void Widget::bindCurrentEditor(MyTextEdit* editor)
{
    LOG_DEBUG("[Widget] bindCurrentEditor 被调用, editor =" << (void*)editor);
    if (!editor) return;

    // RAII: 始终先断开当前 editor 的所有连接（即使同一编辑器，防止重复绑定累积）。
    // 修复：原条件 m_currentTextEdit != editor 导致切回同一标签时不断开，
    //      后续 lambda + Qt::UniqueConnection 触发 Qt 警告（lambda 不支持 unique）。
    if (m_currentTextEdit) {
        auto* old = dynamic_cast<MyTextEdit*>(m_currentTextEdit);
        if (old) {
            disconnect(old, nullptr, this, nullptr);
            if (old->document())
                disconnect(old->document(), nullptr, this, nullptr);
        }
    }

    m_currentTextEdit = editor;  // 隐式转换为 IEditorEdit*

    // 光标位置更新 + 补全器光标跟踪
    // 注：lambda 连接不使用 Qt::UniqueConnection（Qt 对 lambda 无法判定唯一性，会报警告），
    //     唯一性由上方 disconnect 保证。
    connect(editor, &MyTextEdit::cursorPositionChangedSignal,
            this, [this]() {
        if (m_completer) m_completer->handleCursorMovement();
        onCursorPositionChanged();
    });

    // 文本修改状态同步到标签页 + FileOperator + 状态栏
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool changed) {
        if (m_fileOperator) m_fileOperator->setModified(changed);
        if (m_tabBar) m_tabBar->setCurrentModified(changed);
        if (m_labelModState) {
            m_labelModState->setText(changed ? tr("● 已修改") : QString());
        }
        updateTitleForCurrentTab();
    });

    // 补全器绑定到当前编辑器
    if (m_completer) {
        m_completer->bindEditor(editor);
        // 4-arg 形式：以 this 为 context，确保 disconnect(old, nullptr, this, nullptr) 能断开
        connect(editor, &MyTextEdit::textChangedForCompletion, this, [this]() {
            if (m_completer) m_completer->updateCompletionList();
        });
    }

    // Ctrl+S 保存请求（编辑器层直接发出，绕过 QShortcut 焦点问题）
    // 成员函数指针连接可安全使用 UniqueConnection
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

    // P2-H01: 选中代码 → 终端执行（右键菜单「在终端运行」）
    connect(editor, &MyTextEdit::runInTerminalRequested,  this, &Widget::onRunInTerminal, Qt::UniqueConnection);

    // P3-M04 子项3: 编辑器行号栏断点切换 → 同步到 DebugManager
    connect(editor, &MyTextEdit::breakpointToggled, this, &Widget::onBreakpointToggled, Qt::UniqueConnection);

    // 注：字体大小变化的全局同步由 ConfigManager::configChanged 统一处理
    // （fontZoomIn/fontZoomOut 已写入 ConfigManager，configChanged 信号触发 allEditors() 同步）
    // SettingsPage 也监听 configChanged 更新 SpinBox，实现双向联动

    // === LSP 信号连接（MyTextEdit → LspManager → LspClient）===
    if (m_lspCoordinator) {
        // 补全请求（Ctrl+Space 触发，MyTextEdit 发射信号带光标位置）
        connect(editor, &MyTextEdit::lspCompletionRequested,
                this, [this](int line, int col) {
            if (!m_lspCoordinator || !m_tabBar) return;
            QString path = m_tabBar->currentFilePath();
            if (!path.isEmpty()) {
                m_lspCoordinator->requestCompletion(path, line, col);
            }
        });

        // L16: 鼠标悬停请求（300ms 防抖，MyTextEdit 发射信号带光标位置）
        connect(editor, &MyTextEdit::lspHoverRequested,
                this, [this](int line, int col) {
            if (!m_lspCoordinator || !m_tabBar) return;
            QString path = m_tabBar->currentFilePath();
            if (!path.isEmpty() && m_lspCoordinator->hasServerForFile(path)) {
                m_lspCoordinator->requestHover(path, line, col);
            }
        });

        // F1: 悬停中止（鼠标离开编辑器 / 按键 / 失焦）→ 立即隐藏弹窗
        connect(editor, &MyTextEdit::hoverAborted, this, [this]() {
            if (m_hoverPopup) m_hoverPopup->hideImmediately();
        });

        // Ctrl+左键单击跳转定义（与 F12 等效，复用 onLspGotoDefinition 逻辑）
        connect(editor, &MyTextEdit::lspGotoDefinitionRequested,
                this, [this]() {
            if (m_currentTextEdit) onLspGotoDefinition();
        });

        // C03-6: Ctrl+Alt+左键单击 → 请求定义预览（不跳转，悬浮显示）
        connect(editor, &MyTextEdit::definitionPreviewRequested,
                this, &Widget::onDefinitionPreviewRequested);

        // Bug1: Ctrl+左键单击 #include 头文件路径 → 打开对应头文件
        connect(editor, &MyTextEdit::includeOpenRequested,
                this, [this](const QString& includeText, bool isSystem) {
            openIncludeFile(includeText, isSystem);
        });

        // 文档变更同步（防抖 300ms，避免每次按键都发送 didChange）
        // 监听 QTextDocument::contentsChange 而非 MyTextEdit 信号，保持 MyTextEdit 不依赖 LSP
        // P0-3: 使用 QPointer 弱指针捕获，防止编辑器销毁后 QTimer 回调访问野指针崩溃
        // P2-2: 使用成员变量 m_debounceTimers 替代 static QHash，避免内存泄漏
        QPointer<MyTextEdit> weakEditor(editor);
        connect(editor->document(), &QTextDocument::contentsChange,
                this, [this, weakEditor](int, int, int) {
            if (!m_lspCoordinator || !m_tabBar) return;
            auto* editor = weakEditor.data();
            if (!editor) return;  // 编辑器已销毁
            QString path = m_tabBar->currentFilePath();
            if (path.isEmpty() || !m_lspCoordinator->hasServerForFile(path)) return;

            // P2-2: 防抖定时器存储在成员变量中，编辑器销毁时自动清理
            if (!m_debounceTimers.contains(editor)) {
                QTimer* t = new QTimer(this);
                t->setSingleShot(true);
                connect(t, &QTimer::timeout, this, [this, weakEditor, t]() {
                    auto* ed = weakEditor.data();
                    if (!m_tabBar || !ed) return;  // P0-3: 弱指针判空，编辑器已销毁则跳过
                    QString p = m_tabBar->currentFilePath();
                    if (!p.isEmpty()) {
                        m_lspCoordinator->documentChanged(p, ed->toPlainText());
                        // L14: didChange 后延迟请求 documentSymbol，更新语义高亮
                        // P2-2: 节流 — 距上次 requestSymbols 不足 1s 则跳过（避免快速编辑时频繁请求）
                        qint64 now = QDateTime::currentMSecsSinceEpoch();
                        if (now - m_lastSymbolsRequestMs < 1000) return;
                        m_lastSymbolsRequestMs = now;
                        QTimer::singleShot(200, this, [this, p]() {
                            if (m_lspCoordinator && m_lspCoordinator->hasServerForFile(p)) {
                                m_lspCoordinator->requestSymbols(p);
                            }
                        });
                    }
                });
                // P2-2: 编辑器销毁时清理定时器，避免内存泄漏
                connect(editor, &QObject::destroyed, this, [this, editor]() {
                    auto it = m_debounceTimers.find(editor);
                    if (it != m_debounceTimers.end()) {
                        it.value()->deleteLater();
                        m_debounceTimers.erase(it);
                    }
                });
                m_debounceTimers[editor] = t;
            }
            m_debounceTimers[editor]->start(300);
        });
    }

    // V2.0: 大纲防抖刷新 — 文本变更 200ms 后刷新大纲（LSP 优先，无 LSP 时离线扫描）
    // 独立于 LSP didChange 逻辑，确保无 LSP 文件也能实时刷新大纲
    if (m_outlineDebounceTimer) {
        connect(editor->document(), &QTextDocument::contentsChange,
                this, [this](int, int, int) {
            if (m_outlineDebounceTimer) {
                m_outlineDebounceTimer->start();  // 200ms 防抖（重复 start 仅重置计时）
            }
        });
    }

    // 初始单词列表
    QTimer::singleShot(50, editor, &MyTextEdit::updateWordList);
}

void Widget::updateTitleForCurrentTab()
{
    if (!m_tabBar || !m_titleBar) return;

    const TabData* tabData = m_tabBar->currentTabData();
    if (!tabData) {
        m_titleBar->setTitle(m_productConfig.productName);
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
        int result = ModernDialog::confirm(this, m_productConfig.productName, tr("是否保存当前文件的更改？"));
        if (result == ModernDialog::ROLE_ACCEPT) {
            on_btnSave_clicked();
        } else if (result == ModernDialog::ROLE_REJECT) {
            return;
        }
    }

    // 新建标签页
    if (m_tabBar) m_tabBar->addNewTab();

    // P3-M03 子项1: 新建文件使用默认 EOL（来自 ConfigManager）
    auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (ed) {
        QString defaultEol = ConfigManager::instance().defaultEol();
        if (defaultEol.isEmpty()) defaultEol = QStringLiteral("LF");
        ed->setEolMode(defaultEol);
    }
    refreshEolIndicator();
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

    int result = ModernDialog::confirm(this, m_productConfig.productName, tr("是否保存当前文件的更改？"));
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

        // P3-M03 子项1: 通过 FileController 统一写入，按当前 EOL 模式转换行尾
        QString currentEol;
        auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
        if (ed) currentEol = ed->eolMode();
        FileController::writeFile(filename, m_currentTextEdit->toPlainText(),
                                  m_comboBoxEncoding->currentText(), currentEol);

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
        // P3-M03 子项1: 按当前编辑器的 EOL 模式写入（统一行尾）
        QString currentEol;
        auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
        if (ed) currentEol = ed->eolMode();
        if (FileController::writeFile(currentPath, m_currentTextEdit->toPlainText(),
                                      m_comboBoxEncoding->currentText(), currentEol)) {
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

    // P2-H03 子项1: 保存后刷新状态栏分支/修改数
    updateGitStatusBar();
    // P2-H03 子项3: 保存后文件内容变化，重新加载 blame 标注
    requestGitBlameForCurrentFile();

    // LSP：通知语言服务器文件已保存
    if (m_lspCoordinator && !currentPath.isEmpty()) {
        m_lspCoordinator->documentSaved(currentPath);
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
    // F1: 切换标签页时立即隐藏悬停弹窗
    if (m_hoverPopup) m_hoverPopup->hideImmediately();
    // editor 可能为 nullptr（图片预览/SQLite浏览器/Markdown 等特殊标签）
    // 欢迎页显隐由 onTabCountChanged 统一管理，此处不再处理
    if (!editor) {
        m_currentTextEdit = nullptr;
        // V2.1: 清空大纲（委托给 SideBar → ExplorerPanel）
        if (m_sideBar) m_sideBar->clearOutline();
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

        // P2-H04: 记录当前活动文件到工作区管理器
        WorkspaceManager::instance().recordActiveFile(filePath);
    }

    // V1.9: 刷新大纲面板（LSP 优先，离线 fallback）
    refreshOutlineForCurrentEditor();

    // V1.9: 若分栏视图可见，同步源编辑器
    if (m_splitView && m_splitView->isVisible()) {
        m_splitView->setSourceEditor(editor);
    }

    // P2-H03 子项1: 切换标签页时刷新 Git 状态栏（分支名不变但保持实时性）
    updateGitStatusBar();
    // P2-H03 子项3: 若当前编辑器开启了 Git 标注，切换到新文件时重新加载 blame
    requestGitBlameForCurrentFile();

    // P3-M03 子项1: 切换标签页时刷新 EOL 指示器（每个编辑器独立保存 EOL 模式）
    refreshEolIndicator();
    // 同步当前编辑器 EOL 到 FileOperator（保存时按此设置统一行尾）
    if (m_fileOperator) {
        FileOperator* fo = dynamic_cast<FileOperator*>(m_fileOperator);
        if (fo) fo->setEolMode(editor->eolMode());
    }

    // P3-M03 子项4: 切换标签页时按当前文件路径刷新缩进指示器（应用 .editorconfig）
    if (m_labelSpaces && m_tabBar) {
        QString fp = m_tabBar->currentFilePath();
        if (!fp.isEmpty()) {
            applyEditorConfig(editor, fp);
        } else {
            // 未命名文件：回退到全局配置
            int ts = ConfigManager::instance().getValue("Editor/tabSize", 4).toInt();
            QString style = ConfigManager::instance().getValue(
                "Editor/indentStyle", QStringLiteral("spaces")).toString();
            m_labelSpaces->setText(tr("空格: %1").arg(style == QStringLiteral("tabs") ? 0 : ts));
        }
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
    // V2.1: 清空大纲（委托给 SideBar → ExplorerPanel）
    if (m_sideBar) m_sideBar->clearOutline();
    // V1.9: 关闭分栏视图
    if (m_splitView && m_splitView->isVisible()) {
        onCloseSplitView();
    }
    // C03-5: 所有标签关闭时清空导航栈（用户已无文件可回退，避免悬挂历史）
    // 注：单个标签关闭不清空，用户可能想从其他文件跳回
    clearNavigationStack();
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
    // P3-M03 子项1: 同时读取文件并检测原文件行尾类型
    QString detectedEol = QStringLiteral("LF");
    QString content = FileController::readFileWithEol(filePath, &detectedEol);
    if (content.isNull() && !FileController::exists(filePath)) {
        LOG_DEBUG("[Widget] 文件打开失败:" << filePath);
        return;
    }
    m_tabBar->openFileTab(filePath, content);

    // P3-M03 子项1: 应用检测到的 EOL 到当前编辑器 + 刷新状态栏
    if (m_currentTextEdit) {
        auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
        if (ed) ed->setEolMode(detectedEol);
    }
    refreshEolIndicator();

    // P3-M03 子项4: 应用 .editorconfig（覆盖文件原生 EOL 与缩进配置，遵循标准）
    if (m_currentTextEdit) {
        auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
        if (ed) applyEditorConfig(ed, filePath);
    }

    // P2-H04: 记录已打开文件到工作区管理器（供工作区持久化）
    WorkspaceManager::instance().recordOpenFile(filePath);

    // P2-H03 子项3: 文件打开后异步加载 Git blame（若当前编辑器开启了标注）
    // 注：新打开的编辑器默认关闭标注，此调用为 no-op；用户开启后由 onToggleGitBlame 触发
    QTimer::singleShot(0, this, [this]() { requestGitBlameForCurrentFile(); });

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
            // C02-4: 性能监控 — setExternalSymbols 触发 rehighlight（重操作）
            PerformanceMonitor::instance().startTrace(QStringLiteral("rehighlight"));
            ed->setExternalSymbols(externalSymbols);
            PerformanceMonitor::instance().endTrace(QStringLiteral("rehighlight"));
            LOG_DEBUG("[Widget] 外部符号高亮: " << externalSymbols.size()
                      << " 个符号, file=" << filePath.toStdString());
        }
        // 空结果时不调用 setExternalSymbols（避免无意义的 rehighlight）
    });

    // LSP：文件打开时通知语言服务器（按 autoStart 配置决定是否启动）
    if (m_lspCoordinator && ConfigManager::instance().lspAutoStart()) {
        m_lspCoordinator->openFile(filePath, content);
        // L14: 延迟请求 documentSymbol — 服务器需要时间完成 initialize + didOpen 处理
        // 500ms 后请求语义符号，触发语义高亮
        QTimer::singleShot(500, this, [this, filePath]() {
            if (m_lspCoordinator && m_lspCoordinator->hasServerForFile(filePath)) {
                m_lspCoordinator->requestSymbols(filePath);
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

void Widget::onOutlineSymbolClicked(const QString& filePath, int line, int col, int endLine, int endCol)
{
    // V2.1: 大纲符号点击 → 跳转到指定位置并选中符号文本（对标 VS Code）
    // 支持：基础变量/函数仅选中单行，结构体/类/枚举选中完整作用域代码块
    LOG_DEBUG("[Widget] onOutlineSymbolClicked: file=" << filePath.toStdString()
              << " line=" << line << " col=" << col
              << " endLine=" << endLine << " endCol=" << endCol);

    if (filePath.isEmpty() || !m_tabBar) {
        LOG_DEBUG("[Widget] 大纲跳转中止: filePath 为空或 m_tabBar 为空");
        return;
    }

    // 若目标文件与当前文件不同，先打开目标文件
    QString currentPath = m_tabBar->currentFilePath();
    if (currentPath != filePath) {
        if (FileController::exists(filePath)) {
            LOG_DEBUG("[Widget] 大纲跳转: 切换到目标文件 " << filePath.toStdString());
            onFileOpenFromSidebar(filePath);
        } else {
            LOG_DEBUG("[Widget] 大纲跳转：目标文件不存在 " << filePath.toStdString());
            return;
        }
    }

    if (!m_currentTextEdit) {
        // V2.1 兜底：m_currentTextEdit 可能因标签页切换/特殊标签页导致不同步
        // 从 tabBar 获取当前激活编辑器（与保存函数 on_btnSave_clicked 保持一致）
        if (m_tabBar) {
            m_currentTextEdit = m_tabBar->currentEditor();
            LOG_DEBUG("[Widget] 大纲跳转: 从 tabBar 补获编辑器:" << (void*)m_currentTextEdit);
        }
        if (!m_currentTextEdit) {
            LOG_DEBUG("[Widget] 大纲跳转中止: m_currentTextEdit 为空且 tabBar 无可用编辑器");
            return;
        }
    }
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
    if (!ed) {
        LOG_DEBUG("[Widget] 大纲跳转中止: 无法转换为 MyTextEdit");
        return;
    }

    // V2.1: 使用 QTextCursor 直接定位
    QTextCursor cursor = ed->textCursor();
    cursor.movePosition(QTextCursor::Start);

    // 向下移动 line 行（LSP 行号从 0 开始）
    if (line > 0) {
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line);
    }
    cursor.movePosition(QTextCursor::StartOfLine);

    // V2.1 M1 修复：使用 col/endCol 精确选中符号范围（对标 VSCode）
    // 起始位置：line 行 col 列；结束位置：endLine 行 endCol 列
    if (col > 0) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, col);
    }

    if (endLine >= 0 && endCol >= 0 &&
        (endLine > line || (endLine == line && endCol > col))) {
        // 选中符号完整范围（结构体/类/函数等）
        QTextCursor endCursor = cursor;
        if (endLine > line) {
            endCursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, endLine - line);
        }
        if (endCol > 0) {
            // 移动到结束列（相对于行首）
            endCursor.movePosition(QTextCursor::StartOfLine);
            endCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, endCol);
        }
        cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
        LOG_DEBUG("[Widget] 选中符号范围: line=" << line << " col=" << col
                  << " → endLine=" << endLine << " endCol=" << endCol);
    } else if (endLine >= 0 && endLine > line) {
        // V2.1: 兼容旧逻辑 — endCol 无效时选中到结束行行尾
        QTextCursor endCursor = cursor;
        endCursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, endLine - line);
        endCursor.movePosition(QTextCursor::EndOfLine);
        cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
        LOG_DEBUG("[Widget] 选中完整代码块: line=" << line << " → endLine=" << endLine);
    } else {
        // V2.1: 基础变量/函数 — 仅选中当前行
        QTextCursor endCursor = cursor;
        endCursor.movePosition(QTextCursor::EndOfLine);
        cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
    }

    ed->setTextCursor(cursor);
    ed->setFocus();

    // V2.1: 跳转后隐藏补全弹窗（避免光标移动触发补全框干扰阅读）
    if (ed->completer()) {
        ed->completer()->hideCompletion();
    }

    // V2.1 L3 修复：使用 ensureCursorVisible 居中显示，替代手动计算滚动位置
    // 原因：手动计算 line * lineSpacing 未考虑 word wrap（逻辑行可能跨多视觉行）
    ed->ensureCursorVisible();
    // 居中：先滚动到可见，再调整到视口中部
    int cursorY = ed->cursorRect().center().y();
    int viewportCenter = ed->viewport()->height() / 2;
    ed->verticalScrollBar()->setValue(
        ed->verticalScrollBar()->value() + cursorY - viewportCenter);

    LOG_DEBUG("[Widget] 大纲跳转完成: 已定位到行 " << (line + 1));
}

void Widget::refreshOutlineForCurrentEditor()
{
    // V2.1: 刷新当前编辑器的大纲（委托给 SideBar → ExplorerPanel 内嵌大纲区域）
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
    if (m_lspCoordinator && m_lspCoordinator->hasServerForFile(filePath) &&
        m_lspCoordinator->isServerInitialized(filePath)) {
        // V2.1 C3 修复：LSP 异步请求前立即同步大纲文件路径，清空残留符号
        // 防止 LSP 响应到达前用户点击残留节点跳转到错误文件
        m_sideBar->resetOutlineFilePath(filePath);
        m_lspCoordinator->requestSymbols(filePath);
        // 异步：onLspSymbolsReady 会调用 m_sideBar->updateOutlineForEditor
    } else {
        // 无 LSP：使用离线正则扫描
        QString content = ed->toPlainText();
        m_sideBar->updateOutlineFromText(filePath, content);
    }
}

void Widget::onOutlineRefreshDebounced()
{
    // V2.1: 文本变更 200ms 防抖后刷新大纲符号（委托给 SideBar）
    if (!m_sideBar || !m_tabBar) return;

    QString filePath = m_tabBar->currentFilePath();
    if (filePath.isEmpty()) return;

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;

    // LSP 优先（精确），无 LSP 时离线正则扫描
    if (m_lspCoordinator && m_lspCoordinator->hasServerForFile(filePath) &&
        m_lspCoordinator->isServerInitialized(filePath)) {
        m_lspCoordinator->requestSymbols(filePath);
    } else {
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
        if (m_lspCoordinator && m_lspCoordinator->workspaceRoot().isEmpty()) {
            m_lspCoordinator->setWorkspaceRoot(dir);
        }
        // P2-H04: 同步到工作区管理器（多文件夹工作区持久化）
        WorkspaceManager::instance().addFolder(dir);

        // P2-H04: 进入多文件夹模式后提示用户是否保存工作区
        // 仅当当前未关联工作区文件时提示（避免反复弹窗）
        if (WorkspaceManager::instance().workspaceFile().isEmpty()) {
            int result = ModernDialog::question(this, tr("保存工作区"),
                tr("已添加多个文件夹到工作区。是否现在保存工作区以便下次快速恢复？"));
            if (result == ModernDialog::ROLE_ACCEPT) {
                onSaveWorkspaceRequested();
            }
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

// ====================================================================
// P3-M03 子项1: EOL（行尾）切换实现
// ====================================================================

bool Widget::eventFilter(QObject* obj, QEvent* event)
{
    // 拦截状态栏 EOL label 的鼠标按下事件 → 弹出 EOL 选择菜单
    if (obj == m_labelEol && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            showEolMenu();
            return true;  // 事件已处理
        }
    }
    return FramelessWindow::eventFilter(obj, event);
}

void Widget::showEolMenu()
{
    if (!m_labelEol) return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("eolMenu"));

    QString currentEol;
    auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (ed) currentEol = ed->eolMode();

    // 三个选项 + 当前选中标记
    QAction* actLf   = menu.addAction(tr("LF   (Unix/macOS)"));
    QAction* actCrlf = menu.addAction(tr("CRLF (Windows)"));
    QAction* actCr   = menu.addAction(tr("CR   (Classic Mac)"));
    actLf->setCheckable(true);
    actCrlf->setCheckable(true);
    actCr->setCheckable(true);
    if (currentEol == QStringLiteral("LF"))        actLf->setChecked(true);
    else if (currentEol == QStringLiteral("CRLF")) actCrlf->setChecked(true);
    else if (currentEol == QStringLiteral("CR"))   actCr->setChecked(true);

    QAction* selected = menu.exec(m_labelEol->mapToGlobal(QPoint(0, m_labelEol->height())));
    if (selected == actLf)        switchEolMode(QStringLiteral("LF"));
    else if (selected == actCrlf) switchEolMode(QStringLiteral("CRLF"));
    else if (selected == actCr)   switchEolMode(QStringLiteral("CR"));
}

void Widget::switchEolMode(const QString& eol)
{
    auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;
    ed->setEolMode(eol);
    // 同步到 FileOperator（保存时按此设置统一行尾）
    if (m_fileOperator) {
        FileOperator* fo = dynamic_cast<FileOperator*>(m_fileOperator);
        if (fo) fo->setEolMode(eol);
    }
    refreshEolIndicator();
}

void Widget::refreshEolIndicator()
{
    if (!m_labelEol) return;
    auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    QString eol = ed ? ed->eolMode() : ConfigManager::instance().defaultEol();
    if (eol.isEmpty()) eol = QStringLiteral("LF");
    m_labelEol->setText(eol.toUpper());
}

// ====================================================================
// P3-M03 子项3: 列选择模式切换实现
// ====================================================================

void Widget::onToggleColumnSelectionMode()
{
    auto* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;
    bool newState = !ed->columnSelectionMode();
    ed->setColumnSelectionMode(newState);
    // 状态栏简要提示（通过 QLabel 的 tooltip 不可见，用 statusBar 短暂提示）
    if (newState) {
        m_labelPosition->setText(tr("列选模式: 开启 (Shift+Alt+拖拽选择)"));
    } else {
        onCursorPositionChanged();  // 恢复显示行列号
    }
}

// ====================================================================
// P3-M03 子项4: .editorconfig 应用实现
// ====================================================================

void Widget::applyEditorConfig(MyTextEdit* editor, const QString& filePath)
{
    if (!editor || filePath.isEmpty()) return;

    EditorConfig cfg = EditorConfigParser::parse(filePath);
    if (!cfg.isValid()) {
        // 无 .editorconfig 配置：状态栏缩进指示器回退到全局配置
        if (m_labelSpaces) {
            int ts = ConfigManager::instance().getValue("Editor/tabSize", 4).toInt();
            QString style = ConfigManager::instance().getValue(
                "Editor/indentStyle", QStringLiteral("spaces")).toString();
            m_labelSpaces->setText(tr("空格: %1").arg(style == QStringLiteral("tabs") ? 0 : ts));
            m_labelSpaces->setToolTip(tr("缩进: %1 %2")
                .arg(style == QStringLiteral("tabs") ? tr("Tab") : tr("空格")).arg(ts));
        }
        return;
    }

    // 应用缩进配置（按文件覆盖，不写回全局 ConfigManager）
    int tabSize = cfg.indentSize > 0 ? cfg.indentSize : cfg.tabWidth;
    bool useSpaces = (cfg.indentStyle != QStringLiteral("tab") &&
                      cfg.indentStyle != QStringLiteral("tabs"));
    if (tabSize > 0 || !cfg.indentStyle.isEmpty()) {
        editor->setIndentConfig(tabSize > 0 ? tabSize : 4, useSpaces);
    }

    // 应用行尾配置（覆盖文件原生 EOL，遵循 .editorconfig 规范）
    if (!cfg.endOfLine.isEmpty()) {
        QString eolUpper = cfg.endOfLine.toUpper();
        if (eolUpper == QStringLiteral("LF") ||
            eolUpper == QStringLiteral("CRLF") ||
            eolUpper == QStringLiteral("CR")) {
            editor->setEolMode(eolUpper);
            if (m_fileOperator) {
                FileOperator* fo = dynamic_cast<FileOperator*>(m_fileOperator);
                if (fo) fo->setEolMode(eolUpper);
            }
            refreshEolIndicator();
        }
    }

    // 刷新状态栏缩进指示器
    if (m_labelSpaces) {
        int ts = tabSize > 0 ? tabSize : 4;
        m_labelSpaces->setText(tr("空格: %1").arg(useSpaces ? ts : 0));
        m_labelSpaces->setToolTip(tr("缩进 (.editorconfig): %1 %2")
            .arg(useSpaces ? tr("空格") : tr("Tab")).arg(ts));
    }

    // charset: 文件已按检测编码加载，此处仅记录日志（重新编码需重读文件，暂不处理）
    if (!cfg.charset.isEmpty()) {
        LOG_DEBUG("[Widget] .editorconfig charset=" << cfg.charset.toStdString()
                  << " (已按检测编码加载，未重新编码)");
    }
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
        // 终端可见：编辑器 65% : 终端 35%
        // 索引：[0]editorSplitter [1]findReplaceBar [2]welcomePage [3]terminalPanel [4]debugPanel
        int h = height() - 36 - 24;  // 减去标题栏和状态栏
        int editorH = static_cast<int>(h * 0.65);
        int termH = static_cast<int>(h * 0.35);
        m_vSplitter->setSizes({editorH, 0, 0, termH, 0});
    } else {
        // 隐藏面板
        m_terminal->terminateSession();
        m_terminalPanel->hide();
        bool hasTabs = m_tabBar && m_tabBar->tabCount() > 0;
        if (m_welcomePage) m_welcomePage->setVisible(!hasTabs);
        // 终端不可见：编辑器占满 : 欢迎页（无标签时占满）
        m_vSplitter->setSizes({35, 0, hasTabs ? 0 : 500, 0, 0});
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

    // P2-H04: 关闭主窗口时，若处于工作区模式（已关联 .scnb-workspace 文件），
    // 提示用户是否保存工作区（捕获最新的文件夹/打开文件/布局状态）
    auto& wsm = WorkspaceManager::instance();
    if (!wsm.workspaceFile().isEmpty()) {
        int result = ModernDialog::confirm(this, tr("保存工作区"),
            tr("当前处于工作区模式。是否保存工作区以保留最新的文件夹和打开的文件？"));
        if (result == ModernDialog::ROLE_ACCEPT) {
            // 静默保存到已关联的工作区文件（不弹文件对话框）
            if (m_sideBar) {
                Workspace cur = wsm.current();
                for (const QString& f : cur.folders) {
                    wsm.removeFolder(f);
                }
                for (const QString& f : m_sideBar->workspaceFolders()) {
                    wsm.addFolder(f);
                }
            }
            if (m_hSplitter) {
                wsm.setSplitterState(m_hSplitter->saveState());
            }
            if (m_sideBar) {
                m_sideBar->savePanelStates();
                QString saved = ConfigManager::instance().getValue(
                    QStringLiteral("explorer/splitterSizes")).toString();
                if (!saved.isEmpty()) {
                    wsm.setSidebarState(QByteArray::fromBase64(saved.toLatin1()));
                }
            }
            wsm.saveToFile(wsm.workspaceFile());
            ConfigManager::instance().addRecentWorkspace(wsm.workspaceFile());
        } else if (result == ModernDialog::ROLE_REJECT) {
            event->ignore(); // 用户取消关闭
            return;
        }
        // ROLE_DESTRUCTIVE（不保存）：继续关闭流程
    }

    saveWindowState();
    // V2.1 M2/M3: 持久化侧边栏状态（splitter 高度 + 大纲折叠状态）
    if (m_sideBar) {
        m_sideBar->savePanelStates();
    }
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
                    for (MyTextEdit* ed : m_tabBar->allMyTextEditors()) {
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

    // 隐藏欢迎页（无论是否有侧边栏）
    if (m_welcomePage) m_welcomePage->hide();

    // 设置侧边栏工作目录（如果产品配置启用了文件树）
    if (m_sideBar) {
        m_sideBar->setWorkDirectory(dir);
    } else {
        LOG_DEBUG("[Widget] m_sideBar 为空（notebook 产品无文件树），仅设置工作区根目录");
    }

    // P0-2: 同步工作区根目录到 LSP 管理器，使 clangd 使用正确的项目根目录
    if (m_lspCoordinator) {
        m_lspCoordinator->setWorkspaceRoot(dir);
    }

    // P2-H04: 同步到工作区管理器（单文件夹模式重置工作区文件夹列表）
    // setWorkDirectory 是单文件夹模式（清空后设置一个），此处用 remove+add 保持一致
    auto& wsm = WorkspaceManager::instance();
    Workspace cur = wsm.current();
    for (const QString& f : cur.folders) {
        wsm.removeFolder(f);
    }
    wsm.addFolder(dir);
    wsm.recordActiveFile(QString());

    // P2-H03 子项1/2: 工作区切换后刷新 Git 状态栏 + 同步历史面板工作区根
    // 同步 GitManager 单例工作目录，使现有 commit/push/pull/stage 操作作用于新仓库
    GitManager::instance().setWorkingDirectory(dir);
    if (m_gitPanel) m_gitPanel->setWorkspaceRoot(dir);
    updateGitStatusBar();
}

// ========== P3-M01 子项4: 挂载远程工作区 ==========

void Widget::onMountRemoteWorkspaceRequested()
{
    // 1. 读取已保存的 SSH 会话列表
    QList<SshConnectionConfig> configs = SshSessionManager::instance().savedConfigs();
    if (configs.isEmpty()) {
        ModernDialog::warning(this, tr("挂载远程工作区"),
                              tr("暂无已保存的 SSH 会话。\n请先在「SSH 配置」面板中保存连接。"));
        return;
    }

    // 2. 选择会话
    QStringList sessionNames;
    for (const auto& c : configs) {
        sessionNames << (c.name.isEmpty()
                         ? QStringLiteral("%1@%2").arg(c.username, c.host)
                         : c.name);
    }
    bool ok = false;
    QString sessionName = QInputDialog::getItem(
        this, tr("挂载远程工作区"), tr("选择 SSH 会话："),
        sessionNames, 0, false, &ok);
    if (!ok || sessionName.isEmpty()) return;

    // 查找对应配置
    SshConnectionConfig config;
    for (const auto& c : configs) {
        QString name = c.name.isEmpty()
                       ? QStringLiteral("%1@%2").arg(c.username, c.host)
                       : c.name;
        if (name == sessionName) { config = c; break; }
    }
    if (config.host.isEmpty()) {
        ModernDialog::warning(this, tr("挂载远程工作区"),
                              tr("未找到会话配置：%1").arg(sessionName));
        return;
    }

    // 3. 输入远程目录
    QString remoteDir = QInputDialog::getText(
        this, tr("挂载远程工作区"),
        tr("远程目录绝对路径（如 /home/%1/project）：").arg(config.username),
        QLineEdit::Normal, QStringLiteral("/home/%1/").arg(config.username), &ok);
    if (!ok || remoteDir.isEmpty()) return;
    remoteDir = remoteDir.trimmed();

    // 4. 建立 SSH + SFTP 连接（用于后台周期同步）
    auto* client = new SshClient(this);
    if (!client->connect(config)) {
        ModernDialog::warning(this, tr("挂载远程工作区"),
                              tr("SSH 连接失败：%1").arg(client->lastError()));
        client->deleteLater();
        return;
    }
    auto* sftp = new SftpClient(client);
    sftp->setParent(client);  // 跟随 SshClient 生命周期
    if (!sftp->init()) {
        ModernDialog::warning(this, tr("挂载远程工作区"),
                              tr("SFTP 初始化失败：%1").arg(sftp->lastError()));
        client->disconnect();
        client->deleteLater();
        return;
    }

    // 5. 注册 SFTP 客户端到挂载管理器并执行挂载
    RemoteWorkspaceManager::instance().setSftpClient(sessionName, sftp);
    QString mountPoint = RemoteWorkspaceManager::instance().mountRemote(sessionName, remoteDir);
    if (mountPoint.isEmpty()) {
        ModernDialog::warning(this, tr("挂载远程工作区"),
                              tr("挂载失败，请检查远程目录是否存在。"));
        client->disconnect();
        client->deleteLater();
        return;
    }

    // 6. 在侧边栏文件树中显示挂载点（带云图标）
    if (m_sideBar && m_sideBar->explorerPanel()) {
        m_sideBar->explorerPanel()->addRemoteMount(mountPoint, sessionName);
    }

    LOG_INFO("[Widget] 远程工作区已挂载: " << sessionName.toStdString()
             << " -> " << mountPoint.toStdString()
             << " (remote: " << remoteDir.toStdString() << ")");
    ModernDialog::information(this, tr("挂载远程工作区"),
                              tr("已挂载远程工作区：\n%1:%2\n\n本地挂载点：%3\n\n"
                                 "后台将每 5 秒自动同步一次。")
                                  .arg(config.host, remoteDir, mountPoint));
}

// ========== P2-H03 子项1/3: Git 状态栏 + 行级标注实现 ==========

QString Widget::gitWorkspaceRoot() const
{
    // 优先使用 LSP 协调器的工作区根（与 clangd 项目根一致）
    if (m_lspCoordinator) {
        QString root = m_lspCoordinator->workspaceRoot();
        if (!root.isEmpty()) return root;
    }
    // 回退：使用侧边栏当前工作目录
    if (m_sideBar) return m_sideBar->currentWorkDir();
    return QString();
}

void Widget::updateGitStatusBar()
{
    if (!m_labelBranch) return;

    QString root = gitWorkspaceRoot();
    if (root.isEmpty()) {
        m_labelBranch->setText(tr("非 Git 仓库"));
        m_labelBranch->setToolTip(tr("未打开 Git 仓库"));
        return;
    }

    auto& git = GitManager::instance();
    if (!git.isGitRepo(root)) {
        m_labelBranch->setText(tr("非 Git 仓库"));
        m_labelBranch->setToolTip(tr("当前目录不是 Git 仓库"));
        return;
    }

    QString branch = git.currentBranch(root);
    QList<GitFileStatus> statuses = git.fileStatuses(root);
    int modifiedCount = statuses.size();

    // 格式:  branch_name ⚡N （N>0 时显示 ⚡N，0 时不显示）
    QString text = QStringLiteral(" ") + branch;
    if (modifiedCount > 0) {
        text += QStringLiteral(" \u26A1%1").arg(modifiedCount);
    }
    text += QStringLiteral(" ");
    m_labelBranch->setText(text);

    m_labelBranch->setToolTip(
        tr("分支: %1\n修改文件数: %2").arg(branch).arg(modifiedCount));
}

void Widget::requestGitBlameForCurrentFile()
{
    // 仅在标注可见时加载，避免无谓的 git 进程开销
    if (!m_currentTextEdit) return;
    auto* ed = static_cast<MyTextEdit*>(m_currentTextEdit);
    if (!ed->isGitBlameVisible()) return;

    if (!m_tabBar) return;
    QString filePath = m_tabBar->currentFilePath();
    if (filePath.isEmpty()) return;

    // 跳过正在进行的请求（避免叠加），简单策略：若上一个未完成则等待
    if (m_blameWatcher && m_blameWatcher->isRunning()) return;

    m_pendingBlameFilePath = filePath;

    // QtConcurrent::run 在工作线程执行 git blame，避免阻塞 UI
    QFuture<QList<GitBlameLine>> future = QtConcurrent::run([filePath]() {
        return GitBlameReader::blame(filePath);
    });
    m_blameWatcher->setFuture(future);
}

void Widget::onBlameFinished()
{
    if (!m_blameWatcher) return;

    QList<GitBlameLine> result = m_blameWatcher->result();

    // 校验：结果对应的文件仍是当前文件（防止快速切换标签页导致错位）
    if (!m_tabBar || m_tabBar->currentFilePath() != m_pendingBlameFilePath) {
        m_pendingBlameFilePath.clear();
        return;
    }
    m_pendingBlameFilePath.clear();

    if (!m_currentTextEdit) return;
    auto* ed = static_cast<MyTextEdit*>(m_currentTextEdit);
    ed->setGitBlameInfo(result);
}

void Widget::onToggleGitBlame()
{
    if (!m_currentTextEdit) return;
    auto* ed = static_cast<MyTextEdit*>(m_currentTextEdit);
    bool next = !ed->isGitBlameVisible();
    ed->setGitBlameVisible(next);

    // 开启时立即加载 blame 信息；关闭时清空
    if (next) {
        requestGitBlameForCurrentFile();
    } else {
        ed->clearGitBlameInfo();
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

// ============================================================
// P2-H04: 工作区持久化（.scnb-workspace 文件保存/加载/恢复）
// ============================================================

void Widget::onSaveWorkspaceRequested()
{
    // 收集当前 UI 状态到 WorkspaceManager，然后序列化到用户选择的文件
    auto& wsm = WorkspaceManager::instance();

    // 同步当前侧边栏文件夹列表（确保工作区管理器与 SideBar 状态一致）
    if (m_sideBar) {
        Workspace cur = wsm.current();
        for (const QString& f : cur.folders) {
            wsm.removeFolder(f);
        }
        for (const QString& f : m_sideBar->workspaceFolders()) {
            wsm.addFolder(f);
        }
    }

    // 捕获主分割布局状态（水平分割器：侧边栏 | 编辑区）
    if (m_hSplitter) {
        wsm.setSplitterState(m_hSplitter->saveState());
    }
    // 捕获侧边栏面板状态（Explorer splitter + 大纲折叠）
    if (m_sideBar) {
        m_sideBar->savePanelStates();
        // 读取 ExplorerPanel 持久化的 splitter 状态作为 sidebarState
        QString saved = ConfigManager::instance().getValue(
            QStringLiteral("explorer/splitterSizes")).toString();
        if (!saved.isEmpty()) {
            wsm.setSidebarState(QByteArray::fromBase64(saved.toLatin1()));
        }
    }

    // 默认文件名：工作区名称 + 扩展名
    QString defaultName = wsm.current().name;
    if (defaultName.isEmpty()) {
        defaultName = tr("untitled");
    }
    defaultName += QLatin1String(WorkspaceManager::kExtension);

    // 默认目录：首个工作区文件夹的父目录，或用户文档目录
    QString defaultDir;
    if (m_sideBar && !m_sideBar->workspaceFolders().isEmpty()) {
        QString firstFolder = m_sideBar->workspaceFolders().first();
        defaultDir = QFileInfo(firstFolder).absolutePath();
    }
    if (defaultDir.isEmpty()) {
        defaultDir = QCoreApplication::applicationDirPath();
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, tr("保存工作区"),
        defaultDir + QStringLiteral("/") + defaultName,
        tr("SoulCove 工作区 (*%1)").arg(QLatin1String(WorkspaceManager::kExtension)));

    if (filePath.isEmpty()) return;

    // 确保扩展名
    if (!filePath.endsWith(QLatin1String(WorkspaceManager::kExtension))) {
        filePath += QLatin1String(WorkspaceManager::kExtension);
    }

    // 设置工作区名称为文件名（去扩展名）
    QString wsName = QFileInfo(filePath).completeBaseName();
    wsm.setName(wsName);
    wsm.saveToFile(filePath);
    wsm.setWorkspaceFile(filePath);

    // 记录到最近工作区列表
    ConfigManager::instance().addRecentWorkspace(filePath);

    LOG_INFO("[Widget] 工作区已保存: " << filePath.toStdString()
             << " | name=" << wsName.toStdString());
    ModernDialog::information(this, tr("保存工作区"),
        tr("工作区已保存到\n%1").arg(filePath));
}

void Widget::onOpenWorkspaceRequested()
{
    QStringList recent = ConfigManager::instance().recentWorkspaces();

    // 默认目录：最近工作区所在目录，或应用目录
    QString defaultDir;
    if (!recent.isEmpty() && QFile::exists(recent.first())) {
        defaultDir = QFileInfo(recent.first()).absolutePath();
    }
    if (defaultDir.isEmpty()) {
        defaultDir = QCoreApplication::applicationDirPath();
    }

    QString filePath = QFileDialog::getOpenFileName(
        this, tr("打开工作区"),
        defaultDir,
        tr("SoulCove 工作区 (*%1);;所有文件 (*)").arg(
            QLatin1String(WorkspaceManager::kExtension)));

    if (filePath.isEmpty()) return;

    auto& wsm = WorkspaceManager::instance();
    if (!wsm.loadFromFile(filePath)) {
        ModernDialog::warning(this, tr("打开工作区"),
            tr("无法加载工作区文件：\n%1").arg(filePath));
        return;
    }

    Workspace ws = wsm.current();

    // 隐藏欢迎页
    if (m_welcomePage) m_welcomePage->hide();

    // 切换工作区：清空并重建文件树（多文件夹模式）
    if (m_sideBar) {
        m_sideBar->setWorkspaceFolders(ws.folders);
        // P0-2: 同步工作区根目录到 LSP（用第一个文件夹）
        if (m_lspCoordinator && !ws.folders.isEmpty()) {
            m_lspCoordinator->setWorkspaceRoot(ws.folders.first());
        }
    }

    // 恢复主分割布局状态
    if (m_hSplitter && !ws.splitterState.isEmpty()) {
        m_hSplitter->restoreState(ws.splitterState);
    }
    // 恢复侧边栏状态
    if (m_sideBar && !ws.sidebarState.isEmpty()) {
        ConfigManager::instance().setValue(
            QStringLiteral("explorer/splitterSizes"),
            QString::fromLatin1(ws.sidebarState.toBase64()));
        if (m_sideBar->explorerPanel()) {
            m_sideBar->explorerPanel()->loadState();
        }
    }

    // 打开工作区中记录的文件
    for (const QString& f : ws.openFiles) {
        if (FileController::exists(f)) {
            QString content = FileController::readFile(f);
            if (!content.isNull()) {
                m_tabBar->openFileTab(f, content);
            }
        }
    }

    // 激活上次活动文件
    if (!ws.activeFile.isEmpty()) {
        int idx = m_tabBar->findTabByFilePath(ws.activeFile);
        if (idx >= 0) {
            m_tabBar->switchToTab(idx);
        }
    }

    // 记录到最近工作区列表
    ConfigManager::instance().addRecentWorkspace(filePath);

    LOG_INFO("[Widget] 工作区已加载: " << filePath.toStdString()
             << " | folders=" << ws.folders.size()
             << " openFiles=" << ws.openFiles.size());
}

void Widget::promptRestoreLastWorkspace()
{
    QStringList recent = ConfigManager::instance().recentWorkspaces();
    if (recent.isEmpty()) return;

    // 取最近一个存在的工作区文件
    QString lastWs;
    for (const QString& path : recent) {
        if (QFile::exists(path)) {
            lastWs = path;
            break;
        }
    }
    if (lastWs.isEmpty()) return;

    int result = ModernDialog::question(this, tr("恢复工作区"),
        tr("检测到上次的工作区：\n%1\n\n是否恢复该工作区？").arg(lastWs));
    if (result == ModernDialog::ROLE_ACCEPT) {
        // 复用打开工作区逻辑
        auto& wsm = WorkspaceManager::instance();
        if (wsm.loadFromFile(lastWs)) {
            Workspace ws = wsm.current();
            if (m_welcomePage) m_welcomePage->hide();
            if (m_sideBar) {
                m_sideBar->setWorkspaceFolders(ws.folders);
                if (m_lspCoordinator && !ws.folders.isEmpty()) {
                    m_lspCoordinator->setWorkspaceRoot(ws.folders.first());
                }
            }
            if (m_hSplitter && !ws.splitterState.isEmpty()) {
                m_hSplitter->restoreState(ws.splitterState);
            }
            if (m_sideBar && !ws.sidebarState.isEmpty()) {
                ConfigManager::instance().setValue(
                    QStringLiteral("explorer/splitterSizes"),
                    QString::fromLatin1(ws.sidebarState.toBase64()));
                if (m_sideBar->explorerPanel()) {
                    m_sideBar->explorerPanel()->loadState();
                }
            }
            for (const QString& f : ws.openFiles) {
                if (FileController::exists(f)) {
                    QString content = FileController::readFile(f);
                    if (!content.isNull()) {
                        m_tabBar->openFileTab(f, content);
                    }
                }
            }
            if (!ws.activeFile.isEmpty()) {
                int idx = m_tabBar->findTabByFilePath(ws.activeFile);
                if (idx >= 0) {
                    m_tabBar->switchToTab(idx);
                }
            }
            ConfigManager::instance().addRecentWorkspace(lastWs);
            LOG_INFO("[Widget] 已恢复上次工作区: " << lastWs.toStdString());
        }
    }
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
    // P3-M03 子项3: 切换列选择模式（Shift+Alt+拖拽也可进入，命令面板提供显式切换入口）
    m_commandPalette->registerCommand({"editor.toggleColumnSelection", tr("切换列选择模式"), QString(), tr("编辑")});

    // --- 视图类 ---
    m_commandPalette->registerCommand({"view.toggleSidebar",   tr("切换侧边栏"),   QString(),      tr("视图")});
    m_commandPalette->registerCommand({"view.toggleTerminal",  tr("切换终端"),     "Ctrl+`",       tr("视图")});
    m_commandPalette->registerCommand({"view.toggleWelcome",   tr("欢迎页"),       QString(),      tr("视图")});
    m_commandPalette->registerCommand({"view.diffCompare",     tr("文件对比"),     QString(),      tr("视图")});
    // P2-H03 子项3: 显示/隐藏 Git 行级标注
    m_commandPalette->registerCommand({"view.toggleGitBlame",  tr("显示 Git 标注"),  QString(),      tr("视图")});

    // --- 终端类 ---
    m_commandPalette->registerCommand({"term.new",         tr("新建终端"),     QString(),      tr("终端")});
    m_commandPalette->registerCommand({"term.clear",       tr("清屏"),         QString(),      tr("终端")});
    m_commandPalette->registerCommand({"term.switchType",  tr("切换Shell"),    QString(),      tr("终端")});

    // --- 调试类 (P3-M04 子项3) ---
    m_commandPalette->registerCommand({"debug.startContinue", tr("开始调试 / 继续"), "F5",          tr("调试")});
    m_commandPalette->registerCommand({"debug.stepOver",      tr("单步跳过"),       "F10",         tr("调试")});
    m_commandPalette->registerCommand({"debug.stepInto",      tr("单步进入"),       "F11",         tr("调试")});
    m_commandPalette->registerCommand({"debug.stepOut",       tr("单步跳出"),       "Shift+F11",   tr("调试")});
    m_commandPalette->registerCommand({"debug.stop",          tr("停止调试"),       "Shift+F5",    tr("调试")});
    m_commandPalette->registerCommand({"debug.togglePanel",   tr("切换调试面板"),   "Ctrl+Shift+D", tr("调试")});

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

    // P3-M03 子项3: 切换列选择模式
    m_commandRegistry.registerCommand(QStringLiteral("editor.toggleColumnSelection"),
                                       [this]{ onToggleColumnSelectionMode(); });

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
    // P2-H03 子项3: 切换 Git 行级标注
    m_commandRegistry.registerCommand(QStringLiteral("view.toggleGitBlame"),
                                       [this]{ onToggleGitBlame(); });

    // === 终端类 ===
    m_commandRegistry.registerCommand(QStringLiteral("term.new"),    [this]{ if (m_terminal) m_terminal->startSession(); });
    m_commandRegistry.registerCommand(QStringLiteral("term.clear"),  [this]{ if (m_terminal) m_terminal->executeCommand(QStringLiteral("cls")); });
    m_commandRegistry.registerCommand(QStringLiteral("term.switchType"), []{ LOG_DEBUG("[CommandPalette] 切换Shell类型（待实现）"); });

    // === 调试类 (P3-M04 子项3) ===
    m_commandRegistry.registerCommand(QStringLiteral("debug.startContinue"), [this]{ onDebugStart(); });
    m_commandRegistry.registerCommand(QStringLiteral("debug.stepOver"),      [this]{ onDebugStepOver(); });
    m_commandRegistry.registerCommand(QStringLiteral("debug.stepInto"),      [this]{ onDebugStepInto(); });
    m_commandRegistry.registerCommand(QStringLiteral("debug.stepOut"),       [this]{ onDebugStepOut(); });
    m_commandRegistry.registerCommand(QStringLiteral("debug.stop"),          [this]{ onDebugStop(); });
    m_commandRegistry.registerCommand(QStringLiteral("debug.togglePanel"),   [this]{ onToggleDebugPanel(); });

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
    // P2-H02 子项1: 「工具 → 代码片段管理」打开管理对话框（CRUD + 导入导出 + VSCode 兼容）
    m_commandRegistry.registerCommand(QStringLiteral("snippet.manage"), [this]{
        SnippetManagerDialog dlg(this);
        // 片段集合变更时刷新补全列表
        connect(&dlg, &SnippetManagerDialog::snippetChanged, this, [this](){
            if (m_completer) m_completer->updateCompletionList();
        });
        dlg.exec();
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
            if (idx >= 0 && idx < results.size() && m_currentTextEdit) {
                // P2-H02 子项2: 取选中文本作为 $SELECTION，先删除选中再插入展开结果
                QTextCursor cur = m_currentTextEdit->textCursor();
                QString selection = cur.selectedText();
                QString expanded = sm.expandSnippet(results[idx], selection);
                if (cur.hasSelection())
                    cur.removeSelectedText();
                cur.insertText(expanded);
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

// ========== C02-4: 性能监控面板（调试模式）==========

void Widget::onShowPerformanceMonitor()
{
    // 弹出 QDialog 显示 PerformanceMonitor 统计报告
    // 报告内容来自 PerformanceMonitor::summaryReport()
    QDialog dlg(this);
    dlg.setWindowTitle(tr("性能监控面板"));
    dlg.setMinimumSize(600, 400);

    auto* layout = new QVBoxLayout(&dlg);

    auto* textEdit = new QPlainTextEdit(&dlg);
    textEdit->setReadOnly(true);
    // 等宽字体保证报告列对齐
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    textEdit->setFont(monoFont);
    textEdit->setPlainText(PerformanceMonitor::instance().summaryReport());
    layout->addWidget(textEdit);

    auto* btnLayout = new QHBoxLayout();
    auto* btnReset  = new QPushButton(tr("清空统计"), &dlg);
    auto* btnClose  = new QPushButton(tr("关闭"), &dlg);
    btnLayout->addStretch();
    btnLayout->addWidget(btnReset);
    btnLayout->addWidget(btnClose);
    layout->addLayout(btnLayout);

    // 清空统计并刷新显示
    connect(btnReset, &QPushButton::clicked, this, [textEdit]() {
        PerformanceMonitor::instance().reset();
        textEdit->setPlainText(PerformanceMonitor::instance().summaryReport());
    });
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    LOG_INFO("[Widget] 显示性能监控面板 (performanceMonitorEnabled="
             << ConfigManager::instance().performanceMonitorEnabled() << ")");

    dlg.exec();
}

// ========== M4: 代码格式化 ==========

void Widget::onFormatDocument()
{
    // C02-4: 性能监控 — 格式化文档（类比 filterSettings，重操作）
    PerformanceMonitor::instance().startTrace(QStringLiteral("onFormatDocument"));

    if (!m_currentTextEdit || !m_tabBar) {
        PerformanceMonitor::instance().endTrace(QStringLiteral("onFormatDocument"));
        return;
    }
    EditorActions::formatDocument(m_currentTextEdit, m_tabBar->currentFilePath());

    PerformanceMonitor::instance().endTrace(QStringLiteral("onFormatDocument"));
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

// ========== P2-H01: 终端联动 ==========

void Widget::onRunInTerminal(const QString& code)
{
    if (code.isEmpty()) return;

    // 1. 如果终端面板未显示，先显示（onToggleTerminal 会启动会话并设置工作目录）
    if (!m_terminalVisible) {
        onToggleTerminal();
    }

    if (!m_terminal) return;

    // 2. 获取当前活动终端的后端
    TerminalBackend* backend = m_terminal->currentBackend();
    if (!backend || !backend->isRunning()) {
        // 后端未运行时提示（终端面板已显示，用户可见）
        if (m_terminal->asWidget()) {
            m_terminal->executeCommand(code);
        }
        return;
    }

    // 3. 发送代码到终端
    // 规范化换行：QTextCursor 选中文本使用 U+2029（已在 MyTextEdit 中替换为 \n），
    // 统一转换为 Windows 风格 \r\n 以兼容 CMD/PowerShell
    QString codeToSend = code;
    codeToSend.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));  // 先归一化
    codeToSend.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));  // 再转为 Windows 换行
    if (!codeToSend.endsWith(QStringLiteral("\r\n"))) {
        codeToSend += QStringLiteral("\r\n");
    }
    backend->write(codeToSend.toLocal8Bit());
}

// ========== LSP 语言服务器响应槽 ==========
// 注：补全/诊断路由已下沉到 LspCoordinator，Widget 仅保留 UI 交互级响应

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

    // C03-6: 预览请求分支 — 不跳转，取目标行 ±5 行 snippet 显示 DefinitionPreviewPopup
    if (m_pendingDefinitionPreview) {
        m_pendingDefinitionPreview = false;  // 复位标志（一次性的）
        if (!m_definitionPreview) return;

        // 取目标行 ±5 行代码片段（LSP 行号 0-based，编辑器/显示用 1-based）
        // 优先从已打开的编辑器取（避免大文件全量读入），未打开则用 QFile 按行读
        QString snippet;
        MyTextEdit* targetEditor = nullptr;
        if (m_tabBar) {
            // 遍历所有已打开标签查找目标文件对应的编辑器
            const auto editors = m_tabBar->allEditors();
            for (const auto& kv : editors) {
                if (kv.first == targetPath) {
                    targetEditor = qobject_cast<MyTextEdit*>(kv.second ? kv.second->asWidget() : nullptr);
                    break;
                }
            }
        }
        if (targetEditor) {
            QTextBlock blk = targetEditor->document()->firstBlock();
            for (int i = 0; i < line && blk.isValid(); ++i) blk = blk.next();
            // 向前回退 5 行
            for (int i = 0; i < 5 && blk.isValid() && blk.previous().isValid(); ++i) {
                blk = blk.previous();
            }
            for (int i = 0; i < 11 && blk.isValid(); ++i) {
                snippet += blk.text() + QStringLiteral("\n");
                blk = blk.next();
            }
        } else {
            // 文件未打开：QFile 按行读取，截取 [line-5, line+5] 区间
            QFile f(targetPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                int startLine = qMax(0, line - 5);   // 0-based 起始
                int endLine   = line + 5;            // 0-based 结束（含）
                int idx = 0;
                while (!f.atEnd()) {
                    QString row = QString::fromUtf8(f.readLine());
                    if (idx >= startLine && idx <= endLine) {
                        snippet += row;
                    }
                    if (idx > endLine) break;
                    ++idx;
                }
                f.close();
            } else {
                LOG_DEBUG("[Widget] 定义预览: 无法打开目标文件 " << targetPath);
                return;
            }
        }

        m_definitionPreview->showDefinition(targetPath, line + 1, col + 1, snippet);

        // 定位弹窗到点击位置（使用鼠标当前位置，编辑器刚刚被点击）
        // 优先使用当前编辑器光标矩形，兜底使用 QCursor::pos()
        QPoint globalPos;
        MyTextEdit* ed = qobject_cast<MyTextEdit*>(
            m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
        if (ed) {
            globalPos = ed->mapToGlobal(ed->cursorRect().bottomRight());
        }
        if (globalPos.isNull()) globalPos = QCursor::pos();
        m_definitionPreview->showAt(globalPos);
        return;
    }

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
    // F1: 悬停文档响应 — 使用 Markdown 富文本弹窗渲染（替代 QToolTip 原始文本）
    LOG_DEBUG("[Widget] LSP 悬停文档: " << documentation.left(80).toStdString()
              << " file=" << filePath.toStdString());

    if (documentation.isEmpty()) {
        if (m_hoverPopup) m_hoverPopup->hidePopup();
        return;
    }

    if (!m_tabBar || m_tabBar->currentFilePath() != filePath) return;

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;

    // F1: 使用鼠标悬停位置（全局坐标）定位弹窗，而非文本光标位置
    // 避免弹窗出现在错误的行（文本光标可能不在鼠标位置）
    QPoint cursorPos = ed->lastHoverGlobalPos();
    if (cursorPos.isNull()) {
        // 兜底：如果鼠标位置不可用，使用文本光标位置
        cursorPos = ed->cursorRect().bottomLeft();
        cursorPos = ed->mapToGlobal(cursorPos);
    }

    if (m_hoverPopup) {
        // F1: Markdown 富文本渲染路径（支持标题/分割线/代码块/Doxygen/行内代码）
        // 带淡入动画，自动适配弹窗宽高
        m_hoverPopup->showMarkdown(documentation, cursorPos);
    } else {
        // 兜底：无 HoverPopup 时退回 QToolTip 原始文本
        QToolTip::showText(cursorPos, documentation, ed);
    }
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
    // V2.1: LSP 文档符号 → SideBar/ExplorerPanel 内嵌大纲区域更新
    // 注：编辑器语义高亮已由 LspCoordinator 内部路由处理，Widget 仅更新大纲
    LOG_DEBUG("[Widget] LSP 文档符号（大纲更新）: " << symbols.size()
              << " 个, file=" << filePath.toStdString());

    // V2.1 C3 加固：校验响应文件是否为当前编辑器文件
    // 场景：用户快速切换 A→B→A，B 的 LSP 响应滞后到达时用户已回到 A，
    //       若不校验会用 B 的符号覆盖 A 的大纲
    if (m_tabBar && m_tabBar->currentFilePath() != filePath) {
        LOG_DEBUG("[Widget] 丢弃过期的 LSP 符号响应: 响应文件=" << filePath.toStdString()
                  << " 当前文件=" << m_tabBar->currentFilePath().toStdString());
        return;
    }

    if (m_sideBar) {
        m_sideBar->updateOutlineForEditor(filePath, symbols);
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
    // C02-4: 性能监控（仅启用时记录，否则立即返回）
    PerformanceMonitor::instance().startTrace(QStringLiteral("onLspGotoDefinition"));

    // F12 跳转定义 — 获取光标位置，请求 LSP definition
    if (!m_lspCoordinator || !m_tabBar) {
        PerformanceMonitor::instance().endTrace(QStringLiteral("onLspGotoDefinition"));
        return;
    }

    QString path = m_tabBar->currentFilePath();
    if (path.isEmpty() || !m_lspCoordinator->hasServerForFile(path)) {
        LOG_DEBUG("[Widget] F12 跳转定义: 当前文件无 LSP 服务器");
        PerformanceMonitor::instance().endTrace(QStringLiteral("onLspGotoDefinition"));
        return;
    }

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) {
        PerformanceMonitor::instance().endTrace(QStringLiteral("onLspGotoDefinition"));
        return;
    }

    // J2: 跳转前将当前位置压入导航历史栈，供 Ctrl+← 回退
    m_navStack.append({ path, ed->currentLine(), ed->currentColumn() });
    // 限制栈深度，避免无限增长
    if (m_navStack.size() > 50) m_navStack.removeFirst();
    // P0 C03: 跳转到新位置时清空前进栈（与浏览器导航行为一致）
    m_navForwardStack.clear();

    // LSP 行列从 0 开始，编辑器从 1 开始
    int line = ed->currentLine() - 1;
    int col = ed->currentColumn() - 1;
    m_lspCoordinator->requestDefinition(path, line, col);

    PerformanceMonitor::instance().endTrace(QStringLiteral("onLspGotoDefinition"));
}

void Widget::onDefinitionPreviewRequested(int line, int col)
{
    // C03-6: Ctrl+Alt+Click 触发定义预览 — 请求 LSP definition 但不跳转
    // 通过 m_pendingDefinitionPreview 标记 onLspDefinitionReady 走预览分支
    // 注：line/col 由 MyTextEdit 传入，1-based；LSP 需要 0-based
    if (!m_lspCoordinator || !m_tabBar || !m_definitionPreview) return;

    QString path = m_tabBar->currentFilePath();
    if (path.isEmpty() || !m_lspCoordinator->hasServerForFile(path)) {
        LOG_DEBUG("[Widget] 定义预览: 当前文件无 LSP 服务器");
        return;
    }

    // 设置预览标志，下次 onLspDefinitionReady 走预览分支
    m_pendingDefinitionPreview = true;
    // 不压入导航栈（预览不算跳转），不清空前进栈
    m_lspCoordinator->requestDefinition(path, line - 1, col - 1);
}

void Widget::onLspGotoImplementation()
{
    // P0 C03: Ctrl+F12 跳转实现 — 请求 LSP textDocument/implementation
    // 响应处理复用 definition 信号（implementationReady → definitionReady）
    if (!m_lspCoordinator || !m_tabBar) return;

    QString path = m_tabBar->currentFilePath();
    if (path.isEmpty() || !m_lspCoordinator->hasServerForFile(path)) {
        LOG_DEBUG("[Widget] Ctrl+F12 跳转实现: 当前文件无 LSP 服务器");
        return;
    }

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;

    // 跳转前将当前位置压入导航历史栈
    m_navStack.append({ path, ed->currentLine(), ed->currentColumn() });
    if (m_navStack.size() > 50) m_navStack.removeFirst();
    m_navForwardStack.clear();

    // LSP 行列从 0 开始，编辑器从 1 开始
    int line = ed->currentLine() - 1;
    int col = ed->currentColumn() - 1;
    m_lspCoordinator->requestImplementation(path, line, col);
}

void Widget::navigateBack()
{
    // J2: Ctrl+← 导航回退 — 从历史栈弹出上一处光标位置并恢复
    if (m_navStack.isEmpty()) {
        LOG_DEBUG("[Widget] 导航回退: 历史栈为空，无上一处位置");
        return;
    }

    NavigationEntry entry = m_navStack.takeLast();

    // P0 C03: 回退前将当前位置压入前进栈，供 Ctrl+→ 前进
    if (m_tabBar && m_currentTextEdit) {
        MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
        if (ed) {
            m_navForwardStack.append({ m_tabBar->currentFilePath(), ed->currentLine(), ed->currentColumn() });
        }
    }

    // 如果目标文件与当前文件不同，打开目标文件
    QString currentPath = m_tabBar ? m_tabBar->currentFilePath() : QString();
    if (currentPath != entry.filePath) {
        if (FileController::exists(entry.filePath)) {
            onFileOpenFromSidebar(entry.filePath);
        } else {
            LOG_DEBUG("[Widget] 导航回退: 文件不存在 " << entry.filePath);
            return;
        }
    }

    // 定位光标到保存的行/列（编辑器行列从 1 开始）
    if (!m_currentTextEdit) return;
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
    if (!ed) return;

    QTextCursor cursor = ed->textCursor();
    QTextBlock block = ed->document()->firstBlock();
    for (int i = 1; i < entry.line && block.isValid(); ++i) {
        block = block.next();
    }
    if (block.isValid()) {
        cursor.setPosition(block.position() + qMax(0, entry.col - 1));
        ed->setTextCursor(cursor);
        ed->setFocus();
        // 滚动到目标行
        int scrollPos = (entry.line - 1) * ed->fontMetrics().lineSpacing();
        ed->verticalScrollBar()->setValue(qMax(0, scrollPos - ed->height() / 3));
    }
}

void Widget::navigateForward()
{
    // P0 C03: Ctrl+→ 导航前进 — 从前进栈弹出下一处光标位置并恢复
    if (m_navForwardStack.isEmpty()) {
        LOG_DEBUG("[Widget] 导航前进: 前进栈为空，无下一处位置");
        return;
    }

    NavigationEntry entry = m_navForwardStack.takeLast();

    // 前进前将当前位置压入回退栈，供 Ctrl+← 再次回退
    if (m_tabBar && m_currentTextEdit) {
        MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
        if (ed) {
            m_navStack.append({ m_tabBar->currentFilePath(), ed->currentLine(), ed->currentColumn() });
            if (m_navStack.size() > 50) m_navStack.removeFirst();
        }
    }

    // 如果目标文件与当前文件不同，打开目标文件
    QString currentPath = m_tabBar ? m_tabBar->currentFilePath() : QString();
    if (currentPath != entry.filePath) {
        if (FileController::exists(entry.filePath)) {
            onFileOpenFromSidebar(entry.filePath);
        } else {
            LOG_DEBUG("[Widget] 导航前进: 文件不存在 " << entry.filePath);
            return;
        }
    }

    // 定位光标到保存的行/列
    if (!m_currentTextEdit) return;
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
    if (!ed) return;

    QTextCursor cursor = ed->textCursor();
    QTextBlock block = ed->document()->firstBlock();
    for (int i = 1; i < entry.line && block.isValid(); ++i) {
        block = block.next();
    }
    if (block.isValid()) {
        cursor.setPosition(block.position() + qMax(0, entry.col - 1));
        ed->setTextCursor(cursor);
        ed->setFocus();
        int scrollPos = (entry.line - 1) * ed->fontMetrics().lineSpacing();
        ed->verticalScrollBar()->setValue(qMax(0, scrollPos - ed->height() / 3));
    }
}

void Widget::clearNavigationStack()
{
    // P0 C03: 清空导航栈（文件关闭/项目切换时调用）
    m_navStack.clear();
    m_navForwardStack.clear();
}

void Widget::onLspFindReferences()
{
    // Shift+F12 查找引用 — 获取光标位置，请求 LSP references
    if (!m_lspCoordinator || !m_tabBar) return;

    QString path = m_tabBar->currentFilePath();
    if (path.isEmpty() || !m_lspCoordinator->hasServerForFile(path)) {
        LOG_DEBUG("[Widget] Shift+F12 查找引用: 当前文件无 LSP 服务器");
        return;
    }

    MyTextEdit* ed = qobject_cast<MyTextEdit*>(
        m_currentTextEdit ? m_currentTextEdit->asWidget() : nullptr);
    if (!ed) return;

    int line = ed->currentLine() - 1;
    int col = ed->currentColumn() - 1;
    m_lspCoordinator->requestReferences(path, line, col);
}

// ============================================================
// Bug4: F11 全屏编辑模式切换
// ============================================================

void Widget::onToggleFullScreen()
{
    if (!m_isFullScreenMode) {
        // 进入全屏：保存当前窗口几何，隐藏标题栏，全屏显示
        m_preFullScreenGeometry = geometry();
        m_preFullScreenMaximized = isMaximized();
        if (m_titleBar) {
            m_titleBar->hide();
        }
        // showFullScreen 使窗口覆盖整个屏幕（无边框窗口原生支持）
        showFullScreen();
        m_isFullScreenMode = true;
    } else {
        // 退出全屏：恢复标题栏，恢复窗口几何
        if (m_titleBar) {
            m_titleBar->show();
        }
        showNormal();
        m_isFullScreenMode = false;
        // 恢复全屏前的窗口状态
        if (m_preFullScreenMaximized) {
            showMaximized();
        } else {
            setGeometry(m_preFullScreenGeometry);
        }
    }
}

// ============================================================
// Bug1: #include 头文件路径解析与打开
// ============================================================

void Widget::openIncludeFile(const QString& includeText, bool isSystem)
{
    if (!m_tabBar || includeText.length() < 2) return;

    // 去除定界符 <...> / "..."，获取原始头文件相对路径
    QString headerRelPath = includeText.mid(1, includeText.length() - 2);
    if (headerRelPath.isEmpty()) return;

    // 获取当前文件所在目录 + 工作区根目录
    QString currentPath = m_tabBar->currentFilePath();
    QString currentDir = QFileInfo(currentPath).absolutePath();
    QString workspaceRoot = m_lspCoordinator ? m_lspCoordinator->workspaceRoot() : QString();

    // 候选搜索目录列表
    QStringList searchDirs;

    if (!isSystem) {
        // 本地头文件 "..."：优先当前文件目录，其次工作区根目录
        searchDirs << currentDir;
        if (!workspaceRoot.isEmpty() && workspaceRoot != currentDir) {
            searchDirs << workspaceRoot;
        }
    } else {
        // 系统头文件 <...>：搜索 Qt include 目录、工作区、当前文件目录
        QString qtPath = ConfigManager::instance().getValue(
            QStringLiteral("Build/qtPath")).toString();
        if (!qtPath.isEmpty()) {
            // Qt6 安装结构: <QtPath>/<version>/<compiler>/include/<Module>
            // 也可能直接是 <QtPath>/include
            searchDirs << QDir(qtPath).filePath(QStringLiteral("include"));
            // 遍历 Qt 安装目录下可能的 include 子目录
            QDir qtDir(qtPath);
            QStringList versionDirs = qtDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& ver : versionDirs) {
                QDir verDir(qtDir.filePath(ver));
                QStringList compilerDirs = verDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString& comp : compilerDirs) {
                    QString incPath = verDir.filePath(comp + QStringLiteral("/include"));
                    if (QFileInfo::exists(incPath)) {
                        searchDirs << incPath;
                    }
                }
            }
        }
        if (!workspaceRoot.isEmpty()) {
            searchDirs << workspaceRoot;
        }
        searchDirs << currentDir;  // 兜底
    }

    // 在候选目录中查找头文件
    for (const QString& dir : searchDirs) {
        // headerRelPath 可能含子路径如 "ui/editor/MyTextEdit.h"
        QString candidate = QDir(dir).filePath(headerRelPath);
        if (QFileInfo::exists(candidate)) {
            onFileOpenFromSidebar(QDir::toNativeSeparators(candidate));
            return;
        }
        // Qt 头文件通常无扩展名，如 <QPushButton> → QtWidgets/QPushButton
        // 尝试在 include 目录下递归查找（限一层子目录）
        if (isSystem) {
            QDir incDir(dir);
            QStringList subDirs = incDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& sub : subDirs) {
                QString subCandidate = incDir.filePath(sub + QStringLiteral("/") + headerRelPath);
                if (QFileInfo::exists(subCandidate)) {
                    onFileOpenFromSidebar(QDir::toNativeSeparators(subCandidate));
                    return;
                }
            }
        }
    }

    LOG_DEBUG("[Widget] #include 头文件未找到: " << includeText.toStdString()
              << " (已搜索 " << searchDirs.size() << " 个目录)");
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

// ========== P3-M04 子项1: 任务输出错误跳转 ==========

void Widget::onJumpToLocation(const QString& filePath, int line, int col)
{
    LOG_DEBUG("[Widget] 跳转位置: " << filePath.toStdString() << ":" << line << ":" << col);
    if (filePath.isEmpty()) return;

    // 打开目标文件（若未打开）
    QString currentPath = m_tabBar ? m_tabBar->currentFilePath() : QString();
    if (currentPath != filePath) {
        if (FileController::exists(filePath)) {
            onFileOpenFromSidebar(filePath);
        } else {
            LOG_DEBUG("[Widget] 跳转目标文件不存在: " << filePath);
            return;
        }
    }

    // 定位光标到目标行（line/col 为 1-based，编辑器 QTextBlock 为 0-based）
    if (!m_currentTextEdit) return;
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
    if (!ed) return;

    QTextCursor cursor = ed->textCursor();
    QTextBlock block = ed->document()->firstBlock();
    for (int i = 0; i < line - 1 && block.isValid(); ++i) {
        block = block.next();
    }
    if (block.isValid()) {
        cursor.setPosition(block.position() + qMax(0, col - 1));
        ed->setTextCursor(cursor);
        ed->setFocus();
        cursor.movePosition(QTextCursor::StartOfBlock);
        ed->setTextCursor(cursor);
        // 滚动到目标行（居中显示）
        int scrollPos = (line - 1) * ed->fontMetrics().lineSpacing();
        ed->verticalScrollBar()->setValue(qMax(0, scrollPos - ed->height() / 3));
    }
}

// ========== P3-M04 子项3: 调试功能 ==========

void Widget::onDebugStart()
{
    if (!m_debugManager) return;

    if (m_debugManager->state() == DebugState::Paused) {
        // 暂停状态 → 继续执行
        m_debugManager->continueExecution();
    } else if (m_debugManager->state() == DebugState::Stopped) {
        // 未启动 → 请求开始调试（弹文件选择）
        onDebugStartRequested();
    }
}

void Widget::onDebugContinue()
{
    if (m_debugManager && m_debugManager->state() == DebugState::Paused) {
        m_debugManager->continueExecution();
    }
}

void Widget::onDebugStepOver()
{
    if (m_debugManager && m_debugManager->state() == DebugState::Paused) {
        m_debugManager->stepOver();
    }
}

void Widget::onDebugStepInto()
{
    if (m_debugManager && m_debugManager->state() == DebugState::Paused) {
        m_debugManager->stepInto();
    }
}

void Widget::onDebugStepOut()
{
    if (m_debugManager && m_debugManager->state() == DebugState::Paused) {
        m_debugManager->stepOut();
    }
}

void Widget::onDebugStop()
{
    if (m_debugManager && m_debugManager->isActive()) {
        m_debugManager->stopDebug();
    }
}

void Widget::onBreakpointToggled(int line, bool enabled)
{
    // 获取当前文件路径
    QString filePath = m_tabBar ? m_tabBar->currentFilePath() : QString();
    if (filePath.isEmpty() || !m_debugManager) return;

    if (enabled) {
        m_debugManager->setBreakpoint(filePath, line);
        if (m_debugView) m_debugView->addBreakpointToList(filePath, line);
    } else {
        m_debugManager->removeBreakpoint(filePath, line);
        if (m_debugView) m_debugView->removeBreakpointFromList(filePath, line);
    }
    LOG_DEBUG("[Widget] 断点切换: " << filePath.toStdString() << ":" << line
              << " enabled=" << enabled);
}

void Widget::onDebugStartRequested()
{
    if (!m_debugManager) return;

    // 弹出文件选择对话框选择要调试的可执行文件
    QString program = QFileDialog::getOpenFileName(
        this, tr("选择要调试的可执行文件"), QString(),
        tr("可执行文件 (*.exe);;所有文件 (*)"));
    if (program.isEmpty()) return;

    // 工作目录：优先使用可执行文件所在目录
    QString workDir = QFileInfo(program).absolutePath();

    // 启动调试会话（无命令行参数）
    m_debugManager->startDebug(program, QStringList(), workDir);

    // 自动显示调试面板
    if (!m_debugPanelVisible) {
        onToggleDebugPanel();
    }
}

void Widget::onDebugBreakpointHit(const QString& file, int line)
{
    LOG_DEBUG("[Widget] 调试器命中断点: " << file.toStdString() << ":" << line);
    if (file.isEmpty()) return;

    // 打开目标文件（若未打开）
    QString currentPath = m_tabBar ? m_tabBar->currentFilePath() : QString();
    if (currentPath != file) {
        if (FileController::exists(file)) {
            onFileOpenFromSidebar(file);
        } else {
            LOG_DEBUG("[Widget] 断点文件不存在: " << file);
            return;
        }
    }

    // 定位光标到断点行（line 为 1-based）
    if (!m_currentTextEdit) return;
    MyTextEdit* ed = qobject_cast<MyTextEdit*>(m_currentTextEdit->asWidget());
    if (!ed) return;

    QTextCursor cursor = ed->textCursor();
    QTextBlock block = ed->document()->firstBlock();
    for (int i = 0; i < line - 1 && block.isValid(); ++i) {
        block = block.next();
    }
    if (block.isValid()) {
        cursor.setPosition(block.position());
        ed->setTextCursor(cursor);
        ed->setFocus();
        // 滚动到目标行（居中显示）
        int scrollPos = (line - 1) * ed->fontMetrics().lineSpacing();
        ed->verticalScrollBar()->setValue(qMax(0, scrollPos - ed->height() / 3));
    }
}

void Widget::onToggleDebugPanel()
{
    if (!m_debugPanel || !m_vSplitter) return;

    m_debugPanelVisible = !m_debugPanelVisible;

    if (m_debugPanelVisible) {
        // 显示调试面板：隐藏欢迎页，调整分割比例
        if (m_welcomePage) m_welcomePage->hide();
        m_debugPanel->show();
        // 编辑器 70% : 调试 30%
        // 索引：[0]editorSplitter [1]findReplaceBar [2]welcomePage [3]terminalPanel [4]debugPanel
        int h = height() - 36 - 24;  // 减去标题栏和状态栏
        int editorH = static_cast<int>(h * 0.70);
        int debugH = static_cast<int>(h * 0.30);
        m_vSplitter->setSizes({editorH, 0, 0, 0, debugH});
    } else {
        // 隐藏调试面板
        m_debugPanel->hide();
        bool hasTabs = m_tabBar && m_tabBar->tabCount() > 0;
        if (m_welcomePage) m_welcomePage->setVisible(!hasTabs);
        m_vSplitter->setSizes({35, 0, hasTabs ? 0 : 500, 0, 0});
    }
}
