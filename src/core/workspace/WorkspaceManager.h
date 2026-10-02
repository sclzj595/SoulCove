#ifndef WORKSPACEMANAGER_H
#define WORKSPACEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>

/// @brief 工作区数据结构
/// 描述一个多文件夹工作区的完整状态（可序列化为 .scnb-workspace JSON 文件）
struct Workspace
{
    QString     name;             ///< 工作区名称（展示用）
    QStringList folders;          ///< 根文件夹列表（多文件夹工作区）
    QStringList openFiles;        ///< 当前打开的文件路径
    QString     activeFile;       ///< 当前活动文件
    QByteArray  splitterState;    ///< 主分割布局状态（水平/垂直分割器）
    QByteArray  sidebarState;     ///< 侧边栏状态（Explorer/大纲等面板布局）
};

/// @brief 工作区管理器 — 单例
///
/// 职责：
/// - 持有当前工作区状态（Workspace）
/// - 序列化/反序列化 .scnb-workspace JSON 文件
/// - 记录打开文件、活动文件、文件夹增删
/// - 与 ConfigManager 协作持久化最近工作区列表
///
/// P2-H04: 多窗口/工作区支持
class WorkspaceManager : public QObject
{
    Q_OBJECT

public:
    /// 获取全局单例实例
    static WorkspaceManager& instance();

    // 禁止拷贝和赋值
    WorkspaceManager(const WorkspaceManager&) = delete;
    WorkspaceManager& operator=(const WorkspaceManager&) = delete;

    // === 当前工作区状态 ===
    /// 获取当前工作区数据（副本）
    Workspace current() const;

    // === 文件序列化 ===
    /// 序列化当前工作区到 JSON 文件
    /// @param filePath 目标文件路径（建议扩展名 .scnb-workspace）
    void saveToFile(const QString& filePath) const;
    /// 从 JSON 文件反序列化工作区（替换当前工作区内容）
    /// @return 解析成功返回 true；文件不存在或格式错误返回 false
    bool loadFromFile(const QString& filePath);

    // === 工作区文件路径 ===
    /// 设置当前工作区文件路径（保存/加载后调用）
    void setWorkspaceFile(const QString& filePath);
    /// 获取当前工作区文件路径（空表示未进入工作区模式）
    QString workspaceFile() const;

    // === 文件夹管理 ===
    /// 添加根文件夹到工作区（去重，自动规范化路径）
    void addFolder(const QString& folder);
    /// 从工作区移除指定根文件夹
    void removeFolder(const QString& folder);

    // === 打开文件记录 ===
    /// 记录一个已打开文件（去重，自动规范化路径）
    void recordOpenFile(const QString& filePath);
    /// 记录当前活动文件（同时确保在 openFiles 列表中）
    void recordActiveFile(const QString& filePath);

    // === 布局状态 ===
    /// 设置主分割布局状态
    void setSplitterState(const QByteArray& state);
    /// 设置侧边栏状态
    void setSidebarState(const QByteArray& state);
    /// 设置工作区名称（展示用，保存时同步到文件）
    void setName(const QString& name);

    /// 工作区文件扩展名
    static constexpr const char* kExtension = ".scnb-workspace";

private:
    WorkspaceManager();
    ~WorkspaceManager() override = default;

    Workspace    m_current;        ///< 当前工作区数据
    QString      m_workspaceFile;  ///< 当前工作区文件路径（空 = 未进入工作区模式）
};

#endif // WORKSPACEMANAGER_H
