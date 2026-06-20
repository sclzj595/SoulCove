#ifndef SIDEBAR_H
#define SIDEBAR_H

#include <QWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QStackedWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QCheckBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QDir>

#include "core/config/ThemeManager.h"
#include "interfaces/ui/ISideFileBar.h"

class GitPanel;
class TaskManager;  // M15: 任务管理器（前向声明）
class OutlinePanel;  // V1.9: 大纲面板（已抽出）
class TasksPanel;    // M15: 任务面板（已抽出）
class SearchPanel;   // 搜索面板（已抽出）
class QDropEvent;   // V1.9: 文件树拖拽

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
        Tasks,      // M15: 任务系统
        Outline,    // V1.9: 大纲（符号导航）
        Extensions  // 扩展
    };

    explicit SideBar(QWidget* parent = nullptr);

    /// @brief V1.9: 事件过滤器 — 拦截文件树的拖拽事件
    bool eventFilter(QObject* obj, QEvent* event) override;

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

    /// @brief 根据文件路径高亮选中侧边栏对应项（Tab→Sidebar同步）
    void selectFileByPath(const QString& filePath);

    /// @brief 设置终端按钮选中状态（由外部同步）
    void setTerminalButtonChecked(bool checked);

    /// @brief 获取当前工作目录（返回第一个文件夹，兼容旧接口）
    QString currentWorkDir() const { return m_workDir; }

    /// @brief 获取内嵌的 GitPanel 实例
    GitPanel* gitPanelWidget() const { return m_gitPanelWidget; }

    /// @brief V1.9: 更新大纲面板（由 Widget 层在 LSP symbolsReady 时调用）
    /// @param filePath 当前文件路径
    /// @param symbols LSP documentSymbol 响应（QVariantMap 列表，含 name/kind/range/children）
    void updateOutline(const QString& filePath, const QList<QVariantMap>& symbols);

    /// @brief V1.9: 清空大纲面板（文件关闭时调用）
    void clearOutline();

    /// @brief V1.9: 离线正则扫描符号并更新大纲（无 LSP 时的 fallback）
    /// @param filePath 当前文件路径
    /// @param content 文件内容
    void updateOutlineFromText(const QString& filePath, const QString& content);

signals:
    /// @brief 文件列表中的文件被双击打开
    void fileOpenRequested(const QString& filePath);

    /// @brief 请求新建文件
    void fileCreateRequested();

    /// @brief V1.9: 请求新建文件夹
    void folderCreateRequested();

    /// @brief V1.9: 请求移动文件（拖拽）— 参数：源路径、目标目录
    void fileMoveRequested(const QString& sourcePath, const QString& targetDir);

    /// @brief V1.9: 大纲符号被点击 — 参数：文件路径、行号(0-based)、列号(0-based)
    void outlineSymbolClicked(const QString& filePath, int line, int col);

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
    void onFileItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onFileItemClicked(QTreeWidgetItem* item, int column);
    void onFileContextMenu(const QPoint& pos);
    void onExplorerNewFile();
    void onExplorerRefresh();
    void onExplorerCollapseAll();
    void onExplorerOpenFolder();

    /// @brief 终端按钮点击
    void onTerminalClicked();

private:
    /// @brief 创建活动图标按钮
    QPushButton* createActivityBtn(const QString& iconText, Activity activity, const QString& tooltip = QString());

    /// @brief 递归填充文件树
    void populateFileTree(QTreeWidgetItem* parentItem, const QDir& dir);

    /// @brief 根据后缀获取文件图标
    QString fileIcon(const QString& suffix) const;

    /// @brief 主题切换时刷新活动按钮样式
    void refreshActivityStyles();

    /// @brief 在树中递归查找匹配路径的项
    QTreeWidgetItem* findTreeItemByPath(QTreeWidgetItem* parent, const QString& filePath) const;

    /// @brief V1.9: 处理文件树拖拽事件（eventFilter 方式）
    bool handleTreeDropEvent(QDropEvent* event);

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
    QPushButton*  m_btnOutline = nullptr;   // V1.9: 大纲按钮
    QPushButton*  m_btnExtensions;
    QPushButton*  m_btnTerminal;      // 终端切换按钮

    // === Explorer 面板 ===
    QWidget*       m_explorerPanel;
    QWidget*       m_explorerHeader;     // 头部工具栏区域
    QLabel*        m_panelTitle;
    QLabel*        m_pathLabel;          // 当前工作目录路径显示
    QPushButton*   m_btnNewFile;         // 新建文件按钮
    QPushButton*   m_btnOpenFolder;      // 打开文件夹按钮
    QPushButton*   m_btnRefresh;         // 刷新按钮
    QPushButton*   m_btnCollapseAll;     // 折叠全部按钮
    QTreeWidget*   m_fileTree;           // 文件树（替代原QListWidget）

    // === Search 面板（已抽出为 SearchPanel）===
    SearchPanel*   m_searchPanel = nullptr;  // 搜索面板（拥有 input/results/options）

    // === Git 面板 ===
    QWidget*       m_gitPanel;
    GitPanel*      m_gitPanelWidget;     // Git 源代码管理面板（替代原TODO列表）

    // === Tasks 面板（M15: 任务系统，已抽出为 TasksPanel）===
    TasksPanel*    m_tasksPanel = nullptr;  // 任务面板（拥有 tree/output/buttons）

    // === Outline 面板（V1.9: 大纲/符号导航，已抽出为 OutlinePanel）===
    OutlinePanel*  m_outlinePanel = nullptr;  // 大纲面板（拥有 tree/hint/filePath）

    // === Extensions 面板 ===
    QWidget*       m_extensionsPanel;

    Activity m_currentActivity = Activity::Explorer;

    // === 工作目录 ===
    QString m_workDir;                        // 当前工作目录路径（兼容旧接口，= m_workspaceFolders.first()）
    QStringList m_workspaceFolders;            // V1.9: 工作区文件夹列表（多文件夹模式）
};

#endif // SIDEBAR_H
