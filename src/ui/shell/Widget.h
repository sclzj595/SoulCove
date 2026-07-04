#ifndef WIDGET_H
#define WIDGET_H

#include "ui/shell/FramelessWindow.h"
#include "ui/shell/TitleBar.h"
#include "ui/editor/EditorTabBar.h"
#include "ui/sidebar/SideBar.h"
#include "ui/terminal/EmbeddedTerminal.h"
#include "ui/terminal/SshTerminalWidget.h"
#include "ui/tools/CommandPalette.h"
#include "core/format/CodeFormatter.h"
#include "ui/sidebar/GitPanel.h"
#include "core/vcs/GitBlameReader.h"  // P2-H03 子项3: GitBlameLine（blame 异步加载）
#include "core/lsp/LspManager.h"  // LspCompletionItem/LspDiagnostic 等类型仍需使用
#include "controller/CommandRegistry.h"
#include "controller/LspCoordinator.h"

#include "interfaces/core/IObserver.h"
#include "interfaces/core/IFileOperator.h"
#include "interfaces/editor/ICompleter.h"
#include "factory/ProductConfig.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <QSplitter>
#include <QFileSystemWatcher>
#include <QHash>
#include <QFutureWatcher>  // P2-H03 子项3: Git blame 异步加载

class MyTextEdit;
class IEditorEdit;
class SettingsPage;
class SshConfigPanel;
class DiffViewer;
class GitPanel;
class LspCoordinator;
class IdleTabTracker;  // R4: 闲置标签页追踪器
class HoverPopup;      // H1: Markdown 悬停预览弹窗
class DefinitionPreviewPopup;  // C03-6: 定义预览弹窗
class DebugManager;    // P3-M04 子项3: GDB MI 调试管理器
class DebugView;       // P3-M04 子项3: 调试视图面板

/// @brief SoulCove 主窗口（VSCode三栏布局）
///
/// 布局结构：
/// ┌──────────────────────────────────────────────┐
/// │ TitleBar  [图标|标题|新建打开保存|弹簧|—□✕]   │
/// ├──────┬───────────────────────────────────────┤
/// │SideBar│  EditorTabBar                        │
/// │(资源栏│  [标签页栏]                           │
/// │ )    │  [编辑区 MyTextEdit + 行号]            │
/// ├──────┴───────────────────────────────────────┤
/// │ StatusBar        (光标位置 / 编码)            │
/// └──────────────────────────────────────────────┘
class Widget : public FramelessWindow, public IObserver
{
    Q_OBJECT

public:
    /// @brief 构造主窗口
    /// @param config 产品线配置（决定哪些子系统被初始化），默认为 IDE 全功能
    explicit Widget(const ProductConfig& config = ProductConfig::ide(), QWidget *parent = nullptr);
    ~Widget();

    /// @brief 获取当前产品线配置
    const ProductConfig& productConfig() const { return m_productConfig; }

    // ========== IObserver 接口实现 ==========
    void onUpdate(const QString& event, const QVariant& data = QVariant()) override;

    // ========== UI构建 ==========
    void createUi();
    QWidget* createWelcomePage();  // 创建 VSCode 风格欢迎页

    // ========== 配置操作 ==========
    void loadConfig();
    void saveConfig();

    // ========== 字体缩放 ==========
    void fontUp();
    void fontDown();

protected:
    /// @brief 窗口关闭事件拦截（覆盖系统关闭，触发保存检查）
    void closeEvent(QCloseEvent* event) override;
    /// @brief 窗口状态变化事件（最大化/还原时同步按钮图标）
    void changeEvent(QEvent* event) override;
    /// @brief 拖拽进入事件
    void dragEnterEvent(QDragEnterEvent* event) override;
    /// @brief 拖放事件（打开拖入的文件）
    void dropEvent(QDropEvent* event) override;
    /// @brief P3-M03 子项1: 事件过滤器 — 拦截状态栏 EOL label 的鼠标点击，弹出 EOL 选择菜单
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    // === 文件操作 ===
    void on_btnNew_clicked();
    void on_btnOpen_clicked();
    void on_btnSave_clicked();
    void saveCurrentFileDirect();    // Ctrl+S 静默保存（不弹窗）
    void onCurrentIndexChanged(int index);
    void onSettingsClicked();
    void onSshConfigClicked();  // 打开SSH配置面板（标签页内嵌）

    // === 标签页联动 ===
    void onCurrentEditorChanged(MyTextEdit* editor);
    void onTabCountChanged(int count);
    void onAllTabsClosed();
    void onFileOpenFromSidebar(const QString& filePath);
    void onSidebarCreateFile();
    void onSidebarDeleteFile(const QString& filePath);
    void onSidebarRenameFile(const QString& filePath);
    void onSidebarOpenInFolder(const QString& filePath);
    // V1.9: 文件夹/移动
    void onSidebarCreateFolder();
    void onSidebarMoveFile(const QString& sourcePath, const QString& targetDir);

    // V1.9: 大纲符号跳转（V2.1: 大纲已迁移至 SideBar/ExplorerPanel，跳转逻辑保留）
    void onOutlineSymbolClicked(const QString& filePath, int line, int col, int endLine, int endCol);
    void refreshOutlineForCurrentEditor();  // 刷新当前编辑器的大纲（委托给 SideBar）
    void onOutlineRefreshDebounced();       // 大纲防抖刷新（文本变更 200ms 后触发）

    // V1.9: 编辑器分栏
    void onToggleSplitEditor();        // Ctrl+\ 切换水平分栏
    void onToggleVerticalSplit();      // Ctrl+Shift+\ 切换垂直分栏
    void onCloseSplitView();           // 关闭分栏
    void onSplitOrientationChanged();  // 分栏方向切换

    // V1.9: 多文件夹工作区
    void onAddFolderToWorkspace();     // 添加文件夹到工作区

    // P2-H04: 工作区持久化
    void onSaveWorkspaceRequested();   // 保存工作区到 .scnb-workspace 文件
    void onOpenWorkspaceRequested();   // 从 .scnb-workspace 文件打开工作区
    void promptRestoreLastWorkspace(); // 启动时提示恢复最近工作区

    // V1.9: 合并冲突解决
    void onResolveMergeConflicts();    // 解决当前文件的合并冲突

    // === 光标位置更新 ===
    void onCursorPositionChanged();

    // === 标题栏窗口控制 ===
    void onMinimizeRequested();
    void onMaximizeRequested();
    void onCloseRequested();

    // === 标题栏右键菜单 ===
    void onOpenFolderRequested();
    void onRefreshRequested();
    void onQuitRequested();
    // P3-M01 子项4: 挂载远程工作区（文件菜单触发）
    void onMountRemoteWorkspaceRequested();

    // === 主题切换 ===
    void onThemeChanged();

    // === 终端切换 ===
    void onToggleTerminal();

    // === P3-M04 子项3: 调试面板切换 ===
    void onToggleDebugPanel();

    // === 命令面板 ===
    void onToggleCommandPalette();
    void onCommandTriggered(const QString& commandId);

    // === 代码格式化 (M4) ===
    void onFormatDocument();

    // === P2-H03 子项1/3: Git 状态栏 + 行级标注 ===
    void onToggleGitBlame();              // 视图菜单：切换 Git 标注显示

    // === 查找/替换 ===
    void onFindRequested();
    void onReplaceRequested();

    // === 右键菜单新增动作 ===
    void onCopyFilePath();           // 复制当前文件路径到剪贴板
    void onOpenInFolder();           // 在系统文件管理器中打开当前文件所在目录
    void onToggleLineComment();      // 切换行注释（Ctrl+/）
    void onToUpperCase();            // 选中文本转大写
    void onToLowerCase();            // 选中文本转小写

    // === P2-H01: 终端联动 ===
    void onRunInTerminal(const QString& code);  // 选中代码发送到终端执行

    // === LSP 语言服务器响应槽 ===
    // 补全/诊断/符号路由已下沉到 LspCoordinator，Widget 仅保留 UI 交互级响应
    void onLspDefinitionReady(const QString& filePath, const QString& uri, int line, int col);
    void onLspHoverReady(const QString& filePath, const QString& documentation);
    void onLspReferencesReady(const QString& filePath, const QList<QVariantMap>& references);
    void onLspSymbolsReady(const QString& filePath, const QList<QVariantMap>& symbols);
    void onLspServerError(const QString& filePath, const QString& error);

    // === LSP 代码导航触发 (L15/L17) ===
    /// F12 跳转定义 — 获取光标位置并请求 LSP definition
    void onLspGotoDefinition();
    /// P0 C03: Ctrl+F12 跳转实现 — 请求 LSP textDocument/implementation
    void onLspGotoImplementation();
    /// Shift+F12 查找引用 — 获取光标位置并请求 LSP references
    void onLspFindReferences();

    /// Bug4: F11 全屏编辑模式切换 — 隐藏标题栏 + 无边框全屏，再次按 F11 恢复
    void onToggleFullScreen();

    /// J2: 导航回退 — Ctrl+← 返回上一处光标位置（跳转定义后回退）
    void navigateBack();
    /// P0 C03: 导航前进 — Ctrl+→ 前进到下一处光标位置（回退后可前进）
    void navigateForward();
    /// P0 C03: 清空导航栈（文件关闭/项目切换时调用）
    void clearNavigationStack();

    // === C03-6: 定义预览（Ctrl+Alt+Click 悬浮） ===
    /// Ctrl+Alt+Click 触发 — 请求 LSP definition 但不跳转，悬浮显示目标行附近代码
    void onDefinitionPreviewRequested(int line, int col);

    /// C02-4: 显示性能监控面板（Alt+Shift+P，调试模式）
    /// 弹出 QDialog 显示 PerformanceMonitor 统计报告
    void onShowPerformanceMonitor();

    // === Diff 视图 (M5) ===
    void openDiffView(const QString& path1, const QString& path2);

    // === 文件外部修改监听 (T18) ===
    void onFileChangedExternally(const QString& path);

    // === P3-M04 子项1: 任务输出错误跳转 ===
    /// @brief 双击任务输出行的 file:line:col 跳转
    /// 由 TasksPanel::jumpToLocationRequested 信号触发
    void onJumpToLocation(const QString& filePath, int line, int col);

    // === P3-M04 子项3: 调试功能 ===
    void onDebugStart();        ///< F5: 开始调试（无活跃会话时）/ 继续（暂停时）
    void onDebugContinue();     ///< 继续（暂停时）
    void onDebugStepOver();     ///< F10: 单步跳过
    void onDebugStepInto();     ///< F11: 单步进入
    void onDebugStepOut();      ///< Shift+F11: 单步跳出
    void onDebugStop();         ///< Shift+F5: 停止调试
    /// @brief 编辑器断点切换信号 — 同步到 DebugManager
    void onBreakpointToggled(int line, bool enabled);
    /// @brief DebugView 请求开始调试 — 弹文件选择对话框
    void onDebugStartRequested();
    /// @brief 调试器命中断点 — 跳转到对应文件:行
    void onDebugBreakpointHit(const QString& file, int line);

private:
    // ========== 布局管理 ==========
    QVBoxLayout* m_mainLayout;
    QSplitter*     m_hSplitter;      // 水平分割器（侧边栏 | 编辑区）
    QHBoxLayout* m_statusBarLayout;

    // === UI组件层 ==========
    TitleBar*      m_titleBar;       // 自定义标题栏（含工具按钮+窗口控制）
    EditorTabBar*  m_tabBar;         // 文件标签页栏（含多编辑器管理）
    SideBar*       m_sideBar;        // 左侧资源栏
    QWidget*       m_statusBar;      // 底部状态栏面板
    class FindReplaceBar* m_findReplaceBar = nullptr;  // 查找替换面板（V1.9）

    // === 状态栏组件 ===
    QLabel*      m_labelPosition;   // 光标位置
    QLabel*      m_labelModState;   // 文件修改状态
    QComboBox*   m_comboBoxEncoding;// 编码选择
    QLabel*      m_labelBranch = nullptr;  // P2-H03 子项1: Git 分支/修改数显示
    QTimer*      m_gitStatusBarTimer = nullptr;  // P2-H03 子项1: 状态栏周期刷新定时器
    QLabel*      m_labelEol = nullptr;     // P3-M03 子项1: 行尾类型指示器（可点击切换 LF/CRLF/CR）
    QLabel*      m_labelSpaces = nullptr;  // P3-M03 子项4: 缩进指示器（用于 .editorconfig 同步显示）
    QLabel*      m_labelClock = nullptr;   // P3-M05: 状态栏时钟（本地化格式）
    QTimer*      m_clockTimer = nullptr;   // P3-M05: 时钟刷新定时器（1秒间隔）

    // === 核心模块（接口访问）===
    IFileOperator* m_fileOperator;
    ICompleter*    m_completer;

    // === 自动保存 ===
    QTimer*        m_autoSaveTimer;

    // === 设置页面 ===
    SettingsPage*   m_settingsPage;

    // === 终端面板 ===
    EmbeddedTerminal* m_terminal;     // 内嵌终端
    SshTerminalWidget* m_sshTerminal = nullptr;  // SSH远程终端
    SshConfigPanel*  m_sshConfigPanel = nullptr;   // SSH配置面板（标签页内嵌）
    QWidget*         m_terminalPanel; // 终端面板容器（含面板标题栏）
    QSplitter*      m_vSplitter;      // 垂直分割器（编辑区/终端）
    bool            m_terminalVisible = false;

    // === P3-M04 子项3: 调试面板 ===
    DebugManager*    m_debugManager = nullptr;  // 调试管理器（GDB MI 驱动）
    DebugView*       m_debugView = nullptr;     // 调试视图（变量/调用栈/断点）
    QWidget*         m_debugPanel = nullptr;    // 调试面板容器（与终端面板同级）
    bool             m_debugPanelVisible = false;

    // === V1.9: 编辑器分栏 ===
    QSplitter*      m_editorSplitter = nullptr;   // 编辑器分栏分割器（m_tabBar | m_splitView）
    class EditorSplitView* m_splitView = nullptr;  // 分栏视图
    Qt::Orientation m_splitOrientation = Qt::Horizontal;  // 分栏方向

    // === V2.1: 大纲防抖定时器（大纲面板已迁移至 SideBar/ExplorerPanel）===
    QTimer*         m_outlineDebounceTimer = nullptr;  // 大纲防抖定时器（200ms）

    /// @brief 创建 VSCode 风格的底部面板（包含终端/问题/输出标签栏）
    QWidget* createTerminalPanel();

    /// @brief P3-M04 子项3: 创建调试面板容器（与终端面板同级，含 DebugView）
    QWidget* createDebugPanel();

    // === 欢迎页（无标签时显示）===
    QWidget*       m_welcomePage;     // VSCode风格欢迎页

    // === 当前活跃编辑器（由TabBar驱动切换）==========
    IEditorEdit* m_currentTextEdit;   // 始终指向当前标签的编辑器（接口指针）

    // === 查找状态 ===
    QString m_lastSearchText;  // 上次搜索文本（供查找/替换复用）

    // === 命令面板 ===
    CommandPalette* m_commandPalette = nullptr;
    CommandRegistry m_commandRegistry;  ///< 命令注册表（哈希表替代 if-else 链）

    // === 文件外部修改监听 (T18) ===
    QFileSystemWatcher* m_fileWatcher = nullptr;
    bool m_suppressFileWatch = false;  // 内部保存时抑制监听

    // === Git 面板 ===
    GitPanel* m_gitPanel = nullptr;

    // === P2-H03 子项3: Git blame 异步加载 ===
    QFutureWatcher<QList<GitBlameLine>>* m_blameWatcher = nullptr;
    QString m_pendingBlameFilePath;  // 当前 blame 请求对应的文件路径（用于丢弃过期结果）

    // === LSP 语言服务器管理器（门面模式，多语言客户端生命周期+信号路由）===
    LspCoordinator* m_lspCoordinator = nullptr;  // LSP 协调器（拥有 LspManager）

    // R4: 闲置标签页追踪器（观察者模式，从 EditorTabBar 提取的 SRP 组件）
    IdleTabTracker* m_idleTabTracker = nullptr;

    // H1: Markdown 悬停预览弹窗（富文本渲染，替代 QToolTip 原始文本）
    HoverPopup* m_hoverPopup = nullptr;

    // C03-6: 定义预览弹窗（Ctrl+Alt+Click 触发，悬浮显示目标定义代码片段）
    DefinitionPreviewPopup* m_definitionPreview = nullptr;
    /// C03-6: 标记下一次 onLspDefinitionReady 为预览请求（不跳转，显示弹窗）
    bool m_pendingDefinitionPreview = false;

    // J2: 代码导航历史栈 — 跳转定义前 push 当前位置，Ctrl+← pop 回退
    struct NavigationEntry {
        QString filePath;  // 文件路径
        int line;          // 行号（1-based，与编辑器一致）
        int col;           // 列号（1-based，与编辑器一致）
    };
    QList<NavigationEntry> m_navStack;
    /// P0 C03: 导航前进栈 — 回退时 push 到前进栈，跳转新位置时清空前进栈
    QList<NavigationEntry> m_navForwardStack;

    // P2-2: didChange 防抖定时器（每个编辑器一个，避免 static QHash 内存泄漏）
    QHash<MyTextEdit*, QTimer*> m_debounceTimers;
    /// P2-2: requestSymbols 节流时间戳（避免快速编辑时频繁请求符号）
    qint64 m_lastSymbolsRequestMs = 0;

    // Bug4: 全屏模式状态 — 记录全屏前的窗口几何，供退出时恢复
    bool    m_isFullScreenMode = false;
    QRect   m_preFullScreenGeometry;    // 全屏前的窗口几何
    bool    m_preFullScreenMaximized = false;  // 全屏前是否处于最大化

    // === 产品线配置 ===
    ProductConfig m_productConfig;  ///< 产品线功能开关（注入式配置）

    // ========== 辅助方法 ==========
    /// 注册快捷键命令（通过 ShortcutFilter 统一管理，Command+Filter+Observer 模式）
    void registerShortcutCommands();
    void bindCurrentEditor(MyTextEdit* editor);   // 绑定编辑器的信号槽
    /// Bug1: 解析 #include 头文件路径并打开（本地 "..." 相对当前文件目录，系统 <...> 搜索 Qt/工作区路径）
    void openIncludeFile(const QString& includeText, bool isSystem);
    void updateTitleForCurrentTab();               // 更新标题栏显示
    void restoreWindowState();                     // 恢复窗口位置/大小
    void saveWindowState();                         // 保存窗口位置/大小
    void setupCommandPalette();                     // 初始化命令面板
    void registerCommands();                         // 注册命令面板命令到 CommandRegistry

    // === P2-H03 子项1/3: Git 状态栏 + 行级标注辅助方法 ===
    QString gitWorkspaceRoot() const;                // 获取当前 Git 工作区根目录
    void updateGitStatusBar();                       // 刷新状态栏 Git 分支/修改数
    void requestGitBlameForCurrentFile();            // 异步加载当前文件的 blame 信息
    void onBlameFinished();                          // blame 异步完成回调

    // === P3-M03 子项1: EOL 切换槽 ===
    /// 显示 EOL 选择菜单（点击状态栏 EOL label 时触发）
    void showEolMenu();
    /// 切换当前编辑器 EOL 模式并刷新状态栏
    void switchEolMode(const QString& eol);
    /// 刷新状态栏 EOL 指示器（按当前编辑器的 EOL 模式）
    void refreshEolIndicator();

    // === P3-M03 子项3: 列选择模式切换槽 ===
    void onToggleColumnSelectionMode();

    // === P3-M03 子项4: .editorconfig 应用 ===
    /// 检测并应用 .editorconfig 配置到指定编辑器
    /// @param editor 目标编辑器
    /// @param filePath 文件路径（用于向上查找 .editorconfig）
    void applyEditorConfig(class MyTextEdit* editor, const QString& filePath);
};

#endif // WIDGET_H
