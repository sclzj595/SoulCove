#include "core/remote/SshSessionManager.h"
#include "core/remote/SshClient.h"
#include "core/config/ConfigManager.h"

#include <QUuid>

SshSessionManager& SshSessionManager::instance()
{
    static SshSessionManager mgr;
    return mgr;
}

SshSessionManager::SshSessionManager(QObject* parent)
    : IRemoteClient(parent)
{
    loadSavedConfigs();
}

QString SshSessionManager::createConnection(const SshConnectionConfig& config)
{
    auto* client = new SshClient(this);

    if (!client->connect(config)) {
        QString err = client->lastError();
        delete client;
        emit errorOccurred(QString(), err);
        return QString();
    }

    QString id = generateConnectionId();
    m_clients[id] = client;

    // 转发信号
    connect(client, &ISshClient::connectionStateChanged,
            this, [this, id, client](bool connected) {
        emit connectionStateChanged(id, connected);
        if (!connected) {
            // 连接断开，自动清理
            m_clients.remove(id);
            client->deleteLater();
            emit connectionRemoved(id);
        }
    });

    connect(client, &ISshClient::errorOccurred,
            this, [this, id](const QString& msg) {
        emit errorOccurred(id, msg);
    });

    emit connectionCreated(id);
    return id;
}

void SshSessionManager::removeConnection(const QString& connectionId)
{
    auto it = m_clients.find(connectionId);
    if (it == m_clients.end()) return;

    it.value()->disconnect();
    it.value()->deleteLater();
    m_clients.erase(it);

    emit connectionRemoved(connectionId);
}

ISshClient* SshSessionManager::client(const QString& connectionId) const
{
    return m_clients.value(connectionId, nullptr);
}

QStringList SshSessionManager::activeConnectionIds() const
{
    return m_clients.keys();
}

QList<SshConnectionConfig> SshSessionManager::savedConfigs() const
{
    return m_savedConfigs.values();
}

void SshSessionManager::saveConfig(const SshConnectionConfig& config)
{
    m_savedConfigs[config.name] = config;

    // 持久化到 ConfigManager
    auto& cm = ConfigManager::instance();
    QString prefix = QStringLiteral("SSH/%1/").arg(config.name);
    cm.setValue(prefix + QStringLiteral("host"), config.host);
    cm.setValue(prefix + QStringLiteral("port"), config.port);
    cm.setValue(prefix + QStringLiteral("username"), config.username);
    cm.setValue(prefix + QStringLiteral("password"), config.password);
    cm.setValue(prefix + QStringLiteral("privateKeyPath"), config.privateKeyPath);
    cm.setValue(prefix + QStringLiteral("authMethod"),
                config.authMethod == SshConnectionConfig::PublicKey ? QStringLiteral("publickey") : QStringLiteral("password"));
    cm.setValue(prefix + QStringLiteral("keepaliveInterval"), config.keepaliveInterval);
    // P3-M01 子项2: tmux 持久化字段
    cm.setValue(prefix + QStringLiteral("useTmux"), config.useTmux);
    cm.setValue(prefix + QStringLiteral("tmuxSessionName"), config.tmuxSessionName);

    // 更新连接名列表
    QStringList names = m_savedConfigs.keys();
    cm.setValue(QStringLiteral("SSH/connections"), names.join(QStringLiteral(",")));
}

void SshSessionManager::removeSavedConfig(const QString& name)
{
    m_savedConfigs.remove(name);

    auto& cm = ConfigManager::instance();
    QString prefix = QStringLiteral("SSH/%1/").arg(name);
    cm.remove(prefix + QStringLiteral("host"));
    cm.remove(prefix + QStringLiteral("port"));
    cm.remove(prefix + QStringLiteral("username"));
    cm.remove(prefix + QStringLiteral("password"));
    cm.remove(prefix + QStringLiteral("privateKeyPath"));
    cm.remove(prefix + QStringLiteral("authMethod"));
    cm.remove(prefix + QStringLiteral("keepaliveInterval"));
    // P3-M01 子项2: tmux 持久化字段
    cm.remove(prefix + QStringLiteral("useTmux"));
    cm.remove(prefix + QStringLiteral("tmuxSessionName"));

    // 更新连接名列表
    QStringList names = m_savedConfigs.keys();
    cm.setValue(QStringLiteral("SSH/connections"), names.join(QStringLiteral(",")));
}

SshConnectionConfig SshSessionManager::savedConfig(const QString& name) const
{
    return m_savedConfigs.value(name);
}

QString SshSessionManager::generateConnectionId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

void SshSessionManager::loadSavedConfigs()
{
    auto& cm = ConfigManager::instance();

    // 遍历已知连接名列表（存储在 SSH/connections 键下）
    QString connListStr = cm.getValue(QStringLiteral("SSH/connections")).toString();
    QStringList connNames;
    if (!connListStr.isEmpty()) {
        connNames = connListStr.split(QStringLiteral(","), Qt::SkipEmptyParts);
    }

    for (const QString& name : connNames) {
        QString prefix = QStringLiteral("SSH/%1/").arg(name);
        SshConnectionConfig config;
        config.name = name;
        config.host = cm.getValue(prefix + QStringLiteral("host")).toString();
        config.port = cm.getValue(prefix + QStringLiteral("port"), 22).toInt();
        config.username = cm.getValue(prefix + QStringLiteral("username")).toString();
        config.password = cm.getValue(prefix + QStringLiteral("password")).toString();
        config.privateKeyPath = cm.getValue(prefix + QStringLiteral("privateKeyPath")).toString();
        config.keepaliveInterval = cm.getValue(prefix + QStringLiteral("keepaliveInterval"), 30).toInt();

        QString authStr = cm.getValue(prefix + QStringLiteral("authMethod"), QStringLiteral("password")).toString();
        config.authMethod = (authStr == QStringLiteral("publickey"))
                                ? SshConnectionConfig::PublicKey
                                : SshConnectionConfig::Password;

        // P3-M01 子项2: 加载 tmux 持久化字段
        config.useTmux = cm.getValue(prefix + QStringLiteral("useTmux"), false).toBool();
        config.tmuxSessionName = cm.getValue(prefix + QStringLiteral("tmuxSessionName")).toString();

        if (!config.host.isEmpty() && !config.username.isEmpty()) {
            m_savedConfigs[name] = config;
        }
    }
}
