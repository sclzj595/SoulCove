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
#include "core/lsp/LspManager.h"  // LspCompletionItem/LspDiagnostic 等类型仍需使用
#include "controller/CommandRegistry.h"
#include "controller/LspCoordinator.h"

#include "interfaces/core/IObserver.h"
#include "interfaces/core/IFileOperator.h"
#include "interfaces/editor/ICompleter.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <QSplitter>
#include <QFileSystemWatcher>

class MyTextEdit;
class IEditorEdit;
class SettingsPage;
class SshConfigPanel;
class DiffViewer;
class GitPanel;

/// @brief scNotebook 主窗口（VSCode三栏布局）
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
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

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

    // V1.9: 大纲符号跳转
    void onOutlineSymbolClicked(const QString& filePath, int line, int col);
    void refreshOutlineForCurrentEditor();  // 刷新当前编辑器的大纲

    // V1.9: 编辑器分栏
    void onToggleSplitEditor();        // Ctrl+\ 切换水平分栏
    void onToggleVerticalSplit();      // Ctrl+Shift+\ 切换垂直分栏
    void onCloseSplitView();           // 关闭分栏
    void onSplitOrientationChanged();  // 分栏方向切换

    // V1.9: 多文件夹工作区
    void onAddFolderToWorkspace();     // 添加文件夹到工作区

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

    // === 主题切换 ===
    void onThemeChanged();

    // === 终端切换 ===
    void onToggleTerminal();

    // === 命令面板 ===
    void onToggleCommandPalette();
    void onCommandTriggered(const QString& commandId);

    // === 代码格式化 (M4) ===
    void onFormatDocument();

    // === 查找/替换 ===
    void onFindRequested();
    void onReplaceRequested();

    // === 右键菜单新增动作 ===
    void onCopyFilePath();           // 复制当前文件路径到剪贴板
    void onOpenInFolder();           // 在系统文件管理器中打开当前文件所在目录
    void onToggleLineComment();      // 切换行注释（Ctrl+/）
    void onToUpperCase();            // 选中文本转大写
    void onToLowerCase();            // 选中文本转小写

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
    /// Shift+F12 查找引用 — 获取光标位置并请求 LSP references
    void onLspFindReferences();

    // === Diff 视图 (M5) ===
    void openDiffView(const QString& path1, const QString& path2);

    // === 文件外部修改监听 (T18) ===
    void onFileChangedExternally(const QString& path);

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

    // === V1.9: 编辑器分栏 ===
    QSplitter*      m_editorSplitter = nullptr;   // 编辑器分栏分割器（m_tabBar | m_splitView）
    class EditorSplitView* m_splitView = nullptr;  // 分栏视图
    Qt::Orientation m_splitOrientation = Qt::Horizontal;  // 分栏方向

    /// @brief 创建 VSCode 风格的底部面板（包含终端/问题/输出标签栏）
    QWidget* createTerminalPanel();

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

    // === LSP 语言服务器管理器（门面模式，多语言客户端生命周期+信号路由）===
    LspCoordinator* m_lspCoordinator = nullptr;  // LSP 协调器（拥有 LspManager）

    // ========== 辅助方法 ==========
    /// 注册快捷键命令（通过 ShortcutFilter 统一管理，Command+Filter+Observer 模式）
    void registerShortcutCommands();
    void bindCurrentEditor(MyTextEdit* editor);   // 绑定编辑器的信号槽
    void updateTitleForCurrentTab();               // 更新标题栏显示
    void restoreWindowState();                     // 恢复窗口位置/大小
    void saveWindowState();                         // 保存窗口位置/大小
    void setupCommandPalette();                     // 初始化命令面板
    void registerCommands();                         // 注册命令面板命令到 CommandRegistry
};

#endif // WIDGET_H
