#ifndef SSHSESSIONMANAGER_H
#define SSHSESSIONMANAGER_H

#include "interfaces/ISshClient.h"

#include <QObject>
#include <QMap>
#include <QList>

class SshClient;

/// @brief SSH 会话管理器（单例）
///
/// 职责：
/// - 管理多个 SSH 连接的生命周期
/// - 持久化连接配置（通过 ConfigManager）
/// - 提供连接列表查询
/// - 统一信号转发
class SshSessionManager : public QObject
{
    Q_OBJECT

public:
    static SshSessionManager& instance();

    /// 创建新的 SSH 连接
    /// @return 连接ID，失败返回空字符串
    QString createConnection(const SshConnectionConfig& config);

    /// 断开并移除连接
    void removeConnection(const QString& connectionId);

    /// 获取指定连接的客户端
    SshClient* client(const QString& connectionId) const;

    /// 获取所有活跃连接ID
    QStringList activeConnectionIds() const;

    /// 获取所有保存的连接配置
    QList<SshConnectionConfig> savedConfigs() const;

    /// 保存连接配置到持久化存储
    void saveConfig(const SshConnectionConfig& config);

    /// 删除保存的连接配置
    void removeSavedConfig(const QString& name);

    /// 获取指定连接的配置
    SshConnectionConfig savedConfig(const QString& name) const;

signals:
    /// 新连接已建立
    void connectionCreated(const QString& connectionId);

    /// 连接已断开
    void connectionRemoved(const QString& connectionId);

    /// 连接状态变化
    void connectionStateChanged(const QString& connectionId, bool connected);

    /// 错误
    void errorOccurred(const QString& connectionId, const QString& message);

private:
    explicit SshSessionManager(QObject* parent = nullptr);

    /// 生成唯一连接ID
    QString generateConnectionId() const;

    /// 加载保存的配置
    void loadSavedConfigs();

    QMap<QString, SshClient*>        m_clients;       // 连接ID → 客户端
    QMap<QString, SshConnectionConfig> m_savedConfigs; // 别名 → 配置
    int m_idCounter = 0;
};

#endif // SSHSESSIONMANAGER_H
