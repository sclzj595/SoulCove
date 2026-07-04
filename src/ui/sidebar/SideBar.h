#ifndef SIDEBAR_H
#define SIDEBAR_H

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QVariantMap>

#include "core/config/ThemeManager.h"
#include "interfaces/ui/ISideFileBar.h"

class GitPanel;
class TaskManager;  // M15: 任务管理器（前向声明）
class TasksPanel;    // M15: 任务面板（已抽出）
class SearchPanel;   // 搜索面板（已抽出）
class ExplorerPanel; // 资源管理器面板（已抽出）

/// @brief VSCode风格左侧资源栏组件
/// 包含活动图标栏（Activity Bar）+ 可折叠内容区域
/// 支持图标切换面板显示/隐藏，暗黑主题样式
/// 文件树使用QTreeWidget实现文件夹层级结构
class SideBar : public QWidget, public ISideFileBar
{
    Q_OBJECT

public:
    /// 活动面板类型枚举
    enum class Activity {
        Explorer,   // 文件资源管理器
        Search,     // 搜索
        Git,        // Git版本控制
        Tasks       // M15: 任务系统
    };

    explicit SideBar(QWidget* parent = nullptr);

    /// @brief 切换到指定活动面板
    void switchToActivity(Activity activity);

    /// @brief 设置侧边栏宽度
    void setPanelWidth(int width) override;

    /// @brief 获取当前活动面板类型
    Activity currentActivity() const { return m_currentActivity; }

    /// @brief 刷新文件列表（ISideFileBar接口实现）
    void refreshFileList() override;

    /// @brief 设置/切换工作目录（单文件夹模式，兼容旧接口）
    void setWorkDirectory(const QString& dirPath);

    /// @brief V1.9: 添加文件夹到工作区（多文件夹模式）
    /// @param dirPath 文件夹路径
    /// @return 是否添加成功（已存在则返回 false）
    bool addWorkspaceFolder(const QString& dirPath);

    /// @brief V1.9: 从工作区移除文件夹
    /// @param index 文件夹索引
    void removeWorkspaceFolder(int index);

    /// @brief V1.9: 获取工作区所有文件夹
    QStringList workspaceFolders() const { return m_workspaceFolders; }

    /// @brief V1.9: 清空工作区所有文件夹
    void clearWorkspace();

    /// @brief P2-H04: 设置工作区文件夹列表（多文件夹模式，切换工作区时调用）
    /// 清空当前工作区并替换为新的文件夹列表（触发文件树重建）
    void setWorkspaceFolders(const QStringList& folders);

    /// @brief P2-H04: 按路径从工作区移除文件夹（多文件夹模式）
    /// @return 是否移除成功（不存在则返回 false）
    bool removeWorkspaceFolderByPath(const QString& folder);

    /// @brief P2-H04: 获取内嵌的 ExplorerPanel 实例（供 Widget 直接调用）
    ExplorerPanel* explorerPanel() const { return m_explorerPanel; }

    /// @brief 根据文件路径高亮选中侧边栏对应项（Tab→Sidebar同步）
    void selectFileByPath(const QString& filePath);

    /// @brief 设置终端按钮选中状态（由外部同步）
    void setTerminalButtonChecked(bool checked);

    /// @brief 获取当前工作目录（返回第一个文件夹，兼容旧接口）
    QString currentWorkDir() const { return m_workDir; }

    /// @brief 获取内嵌的 GitPanel 实例
    GitPanel* gitPanelWidget() const { return m_gitPanelWidget; }

    /// @brief P3-M04 子项1/3: 获取内嵌的 TasksPanel 实例（供 Widget 连接 jumpToLocationRequested 等信号）
    TasksPanel* tasksPanel() const { return m_tasksPanel; }

    /// @brief V2.1: 更新大纲符号（LSP documentSymbol 响应）
    /// 由 Widget::onLspSymbolsReady 调用，委托给 ExplorerPanel 内嵌的大纲区域
    void updateOutlineForEditor(const QString& filePath, const QList<QVariantMap>& symbols);

    /// @brief V2.1: 离线正则扫描更新大纲（无 LSP 时的 fallback）
    void updateOutlineFromText(const QString& filePath, const QString& content);

    /// @brief V2.1: 清空大纲（文件关闭时调用）
    void clearOutline();

    /// @brief V2.1 C3: 立即同步文件路径（LSP 异步请求期间防止错误跳转）
    void resetOutlineFilePath(const QString& filePath);

    /// V2.1 M2/M3: 持久化 ExplorerPanel + OutlinePanel 状态到磁盘
    void savePanelStates();

signals:
    /// @brief 文件列表中的文件被双击打开
    void fileOpenRequested(const QString& filePath);

    /// @brief 请求新建文件
    void fileCreateRequested();

    /// @brief V1.9: 请求新建文件夹
    void folderCreateRequested();

    /// @brief V1.9: 请求移动文件（拖拽）— 参数：源路径、目标目录
    void fileMoveRequested(const QString& sourcePath, const QString& targetDir);

    /// @brief V1.9: 定位请求（搜索结果点击跳转，复用大纲跳转路径）
    /// @param filePath 文件路径
    /// @param line 行号(0-based)
    /// @param col 列号(0-based)
    void outlineSymbolClicked(const QString& filePath, int line, int col, int endLine, int endCol);

    /// @brief V1.9: 工作区文件夹变更 — 参数：所有文件夹路径
    void workspaceFoldersChanged(const QStringList& folders);

    /// @brief V1.9: 请求添加文件夹到工作区
    void addFolderToWorkspaceRequested();

    /// @brief 请求删除文件
    void fileDeleteRequested(const QString& filePath);

    /// @brief 请求重命名文件
    void fileRenameRequested(const QString& filePath);

    /// @brief 请求在文件夹中打开
    void openInFolderRequested(const QString& filePath);

    /// @brief 请求打开文件夹（选择工作区）
    void openFolderRequested();

    /// @brief 终端面板切换请求
    void terminalToggleRequested();

private slots:
    void onActivityButtonClicked();

    /// @brief 处理 ExplorerPanel 的"打开文件夹"按钮点击（QFileDialog + setWorkDirectory）
    void onExplorerOpenFolderClicked();

    /// @brief 终端按钮点击
    void onTerminalClicked();

private:
    /// @brief 创建活动图标按钮
    QPushButton* createActivityBtn(const QString& iconText, Activity activity, const QString& tooltip = QString());

    /// @brief 主题切换时刷新活动按钮样式
    void refreshActivityStyles();

    // === 布局 ===
    QHBoxLayout* m_mainLayout;
    QVBoxLayout* m_activityLayout;      // 图标按钮竖排
    QWidget*      m_panelWidget;         // 右侧内容面板
    QStackedWidget* m_panelStack;        // 面板堆栈（切换不同活动面板）

    // === 活动图标栏（窄条）===
    QWidget*       m_activityBar;
    QPushButton*  m_btnExplorer;
    QPushButton*  m_btnSearch;
    QPushButton*  m_btnGit;
    QPushButton*  m_btnTasks;          // M15: 任务按钮
    QPushButton*  m_btnTerminal;      // 终端切换按钮

    // === Explorer 面板（已抽出为 ExplorerPanel）===
    ExplorerPanel* m_explorerPanel = nullptr;  // 资源管理器面板（拥有 fileTree/header/buttons）

    // === Search 面板（已抽出为 SearchPanel）===
    SearchPanel*   m_searchPanel = nullptr;  // 搜索面板（拥有 input/results/options）

    // === Git 面板 ===
    QWidget*       m_gitPanel;
    GitPanel*      m_gitPanelWidget;     // Git 源代码管理面板（替代原TODO列表）

    // === Tasks 面板（M15: 任务系统，已抽出为 TasksPanel）===
    TasksPanel*    m_tasksPanel = nullptr;  // 任务面板（拥有 tree/output/buttons）

    Activity m_currentActivity = Activity::Explorer;

    // === 工作目录 ===
    QString m_workDir;                        // 当前工作目录路径（兼容旧接口，= m_workspaceFolders.first()）
    QStringList m_workspaceFolders;            // V1.9: 工作区文件夹列表（多文件夹模式）
};

#endif // SIDEBAR_H
