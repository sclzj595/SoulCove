#ifndef REMOTEWORKSPACEMANAGER_H
#define REMOTEWORKSPACEMANAGER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QMutex>

class SshClient;
class SftpClient;

/// @brief 远程工作区挂载管理器
///
/// 设计目标：
/// - 用 SFTP 双向同步替代真正的挂载（Qt 无原生挂载支持）
/// - 后台线程定期同步远程目录到本地临时目录（双向：远程→本地下载、本地→远程上传）
/// - 通过 mountRemote/unmount API 管理挂载生命周期
///
/// 本地挂载点位置：
///   QStandardPaths::writableLocation(CacheLocation) + "/scnb_mounts/<sessionName>/"
///
/// 同步策略：
/// - 周期性轮询（默认 5s），通过 SFTP listDir 比较文件 mtime 触发增量同步
/// - 大文件（>10MB）不参与同步，避免阻塞
/// - 远程 mtime 较新 → 下载；本地 mtime 较新 → 上传
///
/// 线程安全：同步逻辑通过 QMutex 串行化，避免并发 SFTP 调用冲突
class RemoteWorkspaceManager : public QObject
{
    Q_OBJECT

public:
    /// 获取全局单例
    static RemoteWorkspaceManager& instance();

    // 禁用拷贝
    RemoteWorkspaceManager(const RemoteWorkspaceManager&) = delete;
    RemoteWorkspaceManager& operator=(const RemoteWorkspaceManager&) = delete;

    /// @brief 挂载远程目录到本地
    /// @param sessionName SSH 会话名（用于查找 SshClient/SftpClient 与本地挂载点命名）
    /// @param remoteDir 远程目录绝对路径（如 /home/user/project）
    /// @param localMountPoint 本地挂载点路径（为空时自动生成 scnb_mounts/<sessionName>/）
    /// @return 实际使用的本地挂载点路径，失败返回空字符串
    QString mountRemote(const QString& sessionName, const QString& remoteDir,
                        const QString& localMountPoint = QString());

    /// @brief 卸载指定挂载点
    void unmount(const QString& mountPoint);

    /// @brief 卸载指定会话的所有挂载
    void unmountForSession(const QString& sessionName);

    /// @brief 检查指定挂载点是否已挂载
    bool isMounted(const QString& mountPoint) const;

    /// @brief 获取当前所有活跃挂载点路径
    QStringList activeMounts() const;

    /// @brief 获取指定挂载点对应的远程目录路径
    QString remoteDirForMount(const QString& mountPoint) const;

    /// @brief 获取指定挂载点对应的会话名
    QString sessionForMount(const QString& mountPoint) const;

    /// @brief 设置同步间隔（毫秒，默认 5000）
    void setSyncInterval(int ms) { m_syncIntervalMs = ms; }

    /// @brief 设置关联的 SFTP 客户端（按会话名索引）
    /// @note 由 SSH 终端/文件树组件在连接成功后调用
    void setSftpClient(const QString& sessionName, SftpClient* client);

signals:
    /// 挂载成功
    void mountCreated(const QString& mountPoint, const QString& remoteDir);
    /// 挂载已卸载
    void mountRemoved(const QString& mountPoint);
    /// 同步开始
    void syncStarted(const QString& mountPoint);
    /// 同步完成
    void syncFinished(const QString& mountPoint, int filesUpdated);
    /// 同步出错
    void syncError(const QString& mountPoint, const QString& error);

private:
    explicit RemoteWorkspaceManager(QObject* parent = nullptr);
    ~RemoteWorkspaceManager() override;

    /// 单个挂载条目
    struct MountEntry {
        QString     sessionName;
        QString     remoteDir;
        QString     localMountPoint;
        QString     sshHost;     // 用于显示
    };

    /// 同步单个挂载点（远程→本地 + 本地→远程）
    int syncMount(const MountEntry& entry, SftpClient* sftp);

    /// 递归同步目录
    int syncDirectory(SftpClient* sftp,
                      const QString& remoteDir, const QString& localDir);

    /// 获取本地挂载点根目录
    QString mountsRoot() const;

    /// 规范化挂载点路径（绝对化 + 去除尾部分隔符）
    static QString normalizeMountPoint(const QString& path);

    mutable QMutex m_mutex;
    QHash<QString, MountEntry> m_mounts;        ///< mountPoint → MountEntry
    QHash<QString, SftpClient*> m_sftpClients;  ///< sessionName → SftpClient
    QTimer* m_syncTimer = nullptr;
    int m_syncIntervalMs = 5000;  ///< 默认 5 秒同步一次
};

#endif // REMOTEWORKSPACEMANAGER_H
