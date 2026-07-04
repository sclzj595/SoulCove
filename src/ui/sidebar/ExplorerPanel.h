#ifndef EXPLORERPANEL_H
#define EXPLORERPANEL_H

#include <QWidget>
#include <QStringList>
#include <QDir>
#include <QHash>
#include <QVariantMap>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QToolButton;
class QSplitter;
class QDropEvent;
class QEvent;
class OutlinePanel;

/// @brief 资源管理器面板
///
/// 职责：文件树展示/导航/右键菜单/拖拽移动，工作区文件夹由外部注入。
///       所有用户操作通过信号上报，由 SideBar 转发给 Widget 层。
///
/// V2.1 设计说明：
/// - 从 SideBar 抽取，自含 UI（标题 + 工具栏 + 文件树 + 大纲区域）与逻辑
/// - 大纲区域嵌入文件树下方（VSCode 风格），可折叠/展开
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

    /// @brief P2-H04: 添加文件夹到工作区（多文件夹模式）
    /// 更新内部文件夹列表并刷新文件树（去重，自动规范化路径）
    /// @return 是否添加成功（已存在则返回 false）
    bool addFolderToWorkspace(const QString& folder);

    /// @brief P2-H04: 从工作区移除指定文件夹
    /// 更新内部文件夹列表并刷新文件树
    /// @return 是否移除成功（不存在则返回 false）
    bool removeFolderFromWorkspace(const QString& folder);

    /// @brief 刷新文件树（ISideFileBar 接口由 SideBar 转发）
    void refreshFileList();

    /// @brief 根据文件路径高亮选中树中对应项（Tab→Sidebar 同步）
    void selectFileByPath(const QString& filePath);

    /// @brief V2.1: 更新大纲符号（LSP documentSymbol 响应）
    /// @param filePath 当前文件路径
    /// @param symbols LSP documentSymbol 响应（QVariantMap 列表）
    void updateOutline(const QString& filePath, const QList<QVariantMap>& symbols);

    /// @brief V2.1: 离线正则扫描更新大纲（无 LSP 时的 fallback）
    void updateOutlineFromText(const QString& filePath, const QString& content);

    /// @brief V2.1: 清空大纲（文件关闭时调用）
    void clearOutline();

    /// @brief P3-M01 子项4: 添加远程挂载点到文件树显示
    /// 挂载点会以云图标前缀显示，区别于本地工作区文件夹
    /// @param mountPoint 本地挂载点路径
    /// @param sessionName 关联的 SSH 会话名
    void addRemoteMount(const QString& mountPoint, const QString& sessionName);

    /// @brief P3-M01 子项4: 移除指定挂载点
    void removeRemoteMount(const QString& mountPoint);

    /// V2.1: 立即同步文件路径（LSP 异步请求期间防止错误跳转）
    void resetOutlineFilePath(const QString& filePath);

    /// @brief V2.1: 设置大纲区域展开/折叠状态
    void setOutlineExpanded(bool expanded);

    /// @brief V2.1: 判断文件类型是否支持大纲展示
    static bool isOutlineSupported(const QString& filePath);

    /// V2.1 M2 修复：持久化 splitter 高度比例到磁盘
    void saveState();
    void loadState();

    /// V2.1 M3 修复：持久化大纲折叠状态到磁盘
    void saveOutlineState();

signals:
    /// 文件被双击打开
    void fileOpenRequested(const QString& filePath);
    /// P3-M01 子项4: 远程挂载点下的文件被双击打开
    /// @param localPath 本地挂载点中的文件路径（已同步）
    /// @param remotePath 对应的远程文件路径
    /// @param sessionName 关联的 SSH 会话名
    void remoteFileOpenRequested(const QString& localPath, const QString& remotePath, const QString& sessionName);
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
    /// V2.1: 大纲符号被点击 — 参数：文件路径、行号(0-based)、列号(0-based)
    void outlineSymbolClicked(const QString& filePath, int line, int col, int endLine, int endCol);

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
    /// V2.1: 大纲标题栏点击 — 切换折叠/展开
    void onOutlineHeaderClicked();

private:
    /// 递归填充文件树
    void populateFileTree(QTreeWidgetItem* parentItem, const QDir& dir);
    /// 根据后缀获取文件图标
    QString fileIcon(const QString& suffix) const;
    /// 在树中递归查找匹配路径的项
    QTreeWidgetItem* findTreeItemByPath(QTreeWidgetItem* parent, const QString& filePath) const;
    /// 处理文件树拖拽事件
    bool handleTreeDropEvent(QDropEvent* event);
    /// V2.1: 更新大纲标题栏箭头方向
    void updateOutlineHeaderArrow();

    QWidget*        m_explorerHeader = nullptr;
    QLabel*         m_panelTitle = nullptr;
    QLabel*         m_pathLabel = nullptr;
    QPushButton*    m_btnNewFile = nullptr;
    QPushButton*    m_btnOpenFolder = nullptr;
    QPushButton*    m_btnRefresh = nullptr;
    QPushButton*    m_btnCollapseAll = nullptr;
    QTreeWidget*    m_fileTree = nullptr;

    QStringList     m_workspaceFolders;  // 由 SideBar 注入的工作区文件夹列表

    // P3-M01 子项4: 远程挂载点列表（mountPoint → sessionName）
    QHash<QString, QString> m_remoteMounts;

    // === V2.1: 内容分割器（文件树 / 大纲区域可拖拽调节高度）===
    QSplitter*      m_contentSplitter = nullptr;  // 垂直分割器：文件树(上) + 大纲(下)

    // === V2.1: 大纲区域（嵌入文件树下方，VSCode 风格）===
    QWidget*        m_outlineContainer = nullptr;   // 大纲容器（标题栏 + 符号树）
    QWidget*        m_outlineHeader = nullptr;      // 可点击折叠的标题栏
    QToolButton*    m_outlineArrow = nullptr;       // 折叠箭头（Qt 原生矢量箭头）
    QLabel*         m_outlineTitle = nullptr;       // "大纲" 标题
    OutlinePanel*   m_outlineSection = nullptr;     // 符号树组件（复用现有 OutlinePanel）
    bool            m_outlineExpanded = false;      // 当前是否展开
    /// V2.1 H4 修复：用户手动折叠过大纲区域，后续符号更新不再自动展开
    /// 切换文件时重置（新文件给予默认展开机会）
    bool            m_userCollapsedOutline = false;
};

#endif // EXPLORERPANEL_H
