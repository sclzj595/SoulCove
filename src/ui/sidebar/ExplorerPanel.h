#ifndef EXPLORERPANEL_H
#define EXPLORERPANEL_H

#include <QWidget>
#include <QStringList>
#include <QDir>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QDropEvent;
class QEvent;

/// @brief 资源管理器面板
///
/// 职责：文件树展示/导航/右键菜单/拖拽移动，工作区文件夹由外部注入。
///       所有用户操作通过信号上报，由 SideBar 转发给 Widget 层。
///
/// 设计说明：
/// - 从 SideBar 抽取，自含 UI（标题 + 工具栏 + 文件树）与逻辑
/// - 通过 setWorkspaceFolders 注入工作区文件夹列表（搜索范围）
/// - 自装 eventFilter 拦截文件树 viewport 的 Drop 事件（MinimapRenderer 模式）
/// - 不拥有工作区状态，仅持有注入的文件夹列表用于文件树展示
class ExplorerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ExplorerPanel(QWidget* parent = nullptr);

    /// @brief 设置工作区文件夹列表（由 SideBar 在工作区变更时注入）
    void setWorkspaceFolders(const QStringList& folders);

    /// @brief 刷新文件树（ISideFileBar 接口由 SideBar 转发）
    void refreshFileList();

    /// @brief 根据文件路径高亮选中树中对应项（Tab→Sidebar 同步）
    void selectFileByPath(const QString& filePath);

signals:
    /// 文件被双击打开
    void fileOpenRequested(const QString& filePath);
    /// 请求新建文件
    void fileCreateRequested();
    /// 请求新建文件夹
    void folderCreateRequested();
    /// 请求移动文件（拖拽）— 参数：源路径、目标目录
    void fileMoveRequested(const QString& sourcePath, const QString& targetDir);
    /// 请求删除文件
    void fileDeleteRequested(const QString& filePath);
    /// 请求重命名文件
    void fileRenameRequested(const QString& filePath);
    /// 请求在文件管理器中打开
    void openInFolderRequested(const QString& filePath);
    /// 用户点击"打开文件夹"按钮（SideBar 处理 QFileDialog）
    void openFolderClicked();
    /// 请求添加文件夹到工作区
    void addFolderToWorkspaceRequested();
    /// 请求从工作区移除文件夹（参数：索引）
    void removeWorkspaceFolderRequested(int index);

protected:
    /// 拦截文件树 viewport 的 Drop 事件，自定义移动逻辑
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onContextMenu(const QPoint& pos);
    void onNewFile();
    void onRefresh();
    void onCollapseAll();
    void onOpenFolderClicked();

private:
    /// 递归填充文件树
    void populateFileTree(QTreeWidgetItem* parentItem, const QDir& dir);
    /// 根据后缀获取文件图标
    QString fileIcon(const QString& suffix) const;
    /// 在树中递归查找匹配路径的项
    QTreeWidgetItem* findTreeItemByPath(QTreeWidgetItem* parent, const QString& filePath) const;
    /// 处理文件树拖拽事件
    bool handleTreeDropEvent(QDropEvent* event);

    QWidget*      m_explorerHeader = nullptr;
    QLabel*       m_panelTitle = nullptr;
    QLabel*       m_pathLabel = nullptr;
    QPushButton*  m_btnNewFile = nullptr;
    QPushButton*  m_btnOpenFolder = nullptr;
    QPushButton*  m_btnRefresh = nullptr;
    QPushButton*  m_btnCollapseAll = nullptr;
    QTreeWidget*  m_fileTree = nullptr;

    QStringList   m_workspaceFolders;  // 由 SideBar 注入的工作区文件夹列表
};

#endif // EXPLORERPANEL_H
