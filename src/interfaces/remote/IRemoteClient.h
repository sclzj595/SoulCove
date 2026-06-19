#ifndef IREMOTECLIENT_H
#define IREMOTECLIENT_H

#include "interfaces/remote/ISshClient.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

/// @brief 远程连接会话管理器抽象接口
/// 管理 SSH/SFTP 连接生命周期与配置持久化
/// 上层代码只依赖此接口，不依赖 SshSessionManager 具体实现
/// 设计模式：Facade（聚合 ISshClient + ISftpClient 的会话管理）
class IRemoteClient : public QObject
{
    Q_OBJECT

public:
    explicit IRemoteClient(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IRemoteClient() = default;

    /// 创建新的 SSH 连接
    /// @return 连接ID，失败返回空字符串
    virtual QString createConnection(const SshConnectionConfig& config) = 0;

    /// 断开并移除连接
    virtual void removeConnection(const QString& connectionId) = 0;

    /// 获取指定连接的 SSH 客户端
    virtual ISshClient* client(const QString& connectionId) const = 0;

    /// 获取所有活跃连接ID
    virtual QStringList activeConnectionIds() const = 0;

    // === 配置持久化 ===
    virtual QList<SshConnectionConfig> savedConfigs() const = 0;
    virtual void saveConfig(const SshConnectionConfig& config) = 0;
    virtual void removeSavedConfig(const QString& name) = 0;
    virtual SshConnectionConfig savedConfig(const QString& name) const = 0;

signals:
    void connectionCreated(const QString& connectionId);
    void connectionRemoved(const QString& connectionId);
    void connectionStateChanged(const QString& connectionId, bool connected);
    void errorOccurred(const QString& connectionId, const QString& message);
};

#endif // IREMOTECLIENT_H
