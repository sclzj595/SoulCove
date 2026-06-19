#include "core/remote/SshClient.h"

#include <QHostAddress>
#include <QFile>
#include <QCoreApplication>
#include <QElapsedTimer>

// ============================================================
// 构造 / 析构
// ============================================================

SshClient::SshClient(QObject* parent)
    : ISshClient(parent)
{
    // 全局初始化 libssh2（只需一次，内部有引用计数）
    libssh2_init(0);

    m_socket = new QTcpSocket(this);
    m_keepaliveTimer = new QTimer(this);
    m_keepaliveTimer->setSingleShot(false);

    QObject::connect(m_socket, &QTcpSocket::readyRead,
            this, &SshClient::onSocketReadyRead);
    QObject::connect(m_socket, &QTcpSocket::disconnected,
            this, &SshClient::onSocketDisconnected);
    QObject::connect(m_keepaliveTimer, &QTimer::timeout,
            this, &SshClient::onKeepaliveTimer);
}

SshClient::~SshClient()
{
    disconnect();
    libssh2_exit();
}

// ============================================================
// ISshClient 接口实现
// ============================================================

bool SshClient::connect(const SshConnectionConfig& config)
{
    if (m_connected) {
        setError(QStringLiteral("Already connected"));
        return false;
    }

    m_config = config;
    m_lastError.clear();

    // 1. TCP 连接
    m_socket->connectToHost(config.host, config.port);
    if (!m_socket->waitForConnected(config.connectTimeout)) {
        setError(QStringLiteral("TCP connect failed: %1").arg(m_socket->errorString()));
        return false;
    }

    // 2. 创建 SSH 会话
    m_session = libssh2_session_init();
    if (!m_session) {
        setError(QStringLiteral("Failed to create SSH session"));
        m_socket->disconnectFromHost();
        return false;
    }

    // 设置非阻塞模式
    libssh2_session_set_blocking(m_session, 0);

    // 3. SSH 握手
    if (!doHandshake()) {
        return false;
    }

    // 4. 认证
    if (!doAuthenticate()) {
        return false;
    }

    m_connected = true;

    // 启动心跳
    if (m_config.keepaliveInterval > 0) {
        m_keepaliveTimer->start(m_config.keepaliveInterval * 1000);
    }

    emit connectionStateChanged(true);
    return true;
}

void SshClient::disconnect()
{
    if (!m_connected && !m_session) return;

    m_keepaliveTimer->stop();

    // 关闭所有通道
    {
        QMutexLocker locker(&m_mutex);
        for (auto& ch : m_channels) {
            if (ch.channel && ch.isOpen) {
                libssh2_channel_send_eof(ch.channel);
                libssh2_channel_close(ch.channel);
                libssh2_channel_free(ch.channel);
                ch.channel = nullptr;
                ch.isOpen = false;
            }
        }
        m_channels.clear();
    }

    // 断开 SSH 会话
    if (m_session) {
        libssh2_session_disconnect(m_session, "Normal shutdown");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }

    // 断开 TCP
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->waitForDisconnected(2000);
        }
    }

    m_connected = false;
    emit connectionStateChanged(false);
}

bool SshClient::isConnected() const
{
    return m_connected && m_socket->state() == QAbstractSocket::ConnectedState;
}

int SshClient::openShellChannel()
{
    if (!m_connected || !m_session) {
        setError(QStringLiteral("Not connected"));
        return -1;
    }

    // 设置阻塞模式创建通道（简化创建流程）
    libssh2_session_set_blocking(m_session, 1);

    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(m_session);
    if (!channel) {
        char* errMsg = nullptr;
        int errCode = libssh2_session_last_error(m_session, &errMsg, nullptr, 0);
        setError(QStringLiteral("Failed to open channel: %1 (%2)").arg(errMsg).arg(errCode));
        libssh2_session_set_blocking(m_session, 0);
        return -1;
    }

    // 请求 PTY
    int rc = libssh2_channel_request_pty(channel, "xterm");
    if (rc) {
        setError(QStringLiteral("Failed to request PTY: %1").arg(rc));
        libssh2_channel_free(channel);
        libssh2_session_set_blocking(m_session, 0);
        return -1;
    }

    // 设置初始终端大小
    libssh2_channel_request_pty_size(channel, 80, 24);

    // 启动 Shell
    rc = libssh2_channel_shell(channel);
    if (rc) {
        setError(QStringLiteral("Failed to start shell: %1").arg(rc));
        libssh2_channel_free(channel);
        libssh2_session_set_blocking(m_session, 0);
        return -1;
    }

    // 恢复非阻塞
    libssh2_session_set_blocking(m_session, 0);

    // 注册通道
    QMutexLocker locker(&m_mutex);
    m_channelCounter++;
    SshChannel ch;
    ch.channel = channel;
    ch.id = m_channelCounter;
    ch.isOpen = true;
    m_channels.append(ch);

    return ch.id;
}

bool SshClient::writeShell(int channelId, const QByteArray& data)
{
    QMutexLocker locker(&m_mutex);
    SshChannel* ch = findChannel(channelId);
    if (!ch || !ch->isOpen || !ch->channel) {
        setError(QStringLiteral("Invalid channel: %1").arg(channelId));
        return false;
    }

    // 设置阻塞写入
    libssh2_session_set_blocking(m_session, 1);
    ssize_t written = libssh2_channel_write(ch->channel, data.constData(), data.size());
    libssh2_session_set_blocking(m_session, 0);

    if (written < 0) {
        setError(QStringLiteral("Write failed: %1").arg(written));
        return false;
    }
    return true;
}

QByteArray SshClient::readShell(int channelId)
{
    QMutexLocker locker(&m_mutex);
    SshChannel* ch = findChannel(channelId);
    if (!ch || !ch->isOpen || !ch->channel) {
        return QByteArray();
    }

    QByteArray result;
    char buf[4096];
    ssize_t n;
    while ((n = libssh2_channel_read(ch->channel, buf, sizeof(buf))) > 0) {
        result.append(buf, static_cast<int>(n));
    }

    // 检查通道是否已关闭
    if (n == LIBSSH2_ERROR_EAGAIN) {
        // 正常：没有更多数据
    } else if (n < 0) {
        // 错误
    }

    if (libssh2_channel_eof(ch->channel)) {
        ch->isOpen = false;
        locker.unlock();
        emit shellClosed(channelId);
    }

    return result;
}

void SshClient::closeShell(int channelId)
{
    QMutexLocker locker(&m_mutex);
    SshChannel* ch = findChannel(channelId);
    if (!ch || !ch->channel) return;

    libssh2_channel_send_eof(ch->channel);
    libssh2_channel_close(ch->channel);
    libssh2_channel_free(ch->channel);
    ch->channel = nullptr;
    ch->isOpen = false;

    m_channels.removeOne(*ch);
}

QString SshClient::executeCommand(const QString& command, int timeoutMs)
{
    if (!m_connected || !m_session) {
        setError(QStringLiteral("Not connected"));
        return QString();
    }

    // 设置阻塞模式
    libssh2_session_set_blocking(m_session, 1);

    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(m_session);
    if (!channel) {
        setError(QStringLiteral("Failed to open exec channel"));
        libssh2_session_set_blocking(m_session, 0);
        return QString();
    }

    // 执行命令
    int rc = libssh2_channel_exec(channel, command.toUtf8().constData());
    if (rc) {
        setError(QStringLiteral("Failed to exec command: %1").arg(rc));
        libssh2_channel_free(channel);
        libssh2_session_set_blocking(m_session, 0);
        return QString();
    }

    // 读取输出
    QByteArray output;
    char buf[4096];
    ssize_t n;
    QElapsedTimer timer;
    timer.start();
    while (!libssh2_channel_eof(channel)) {
        while ((n = libssh2_channel_read(channel, buf, sizeof(buf))) > 0) {
            output.append(buf, static_cast<int>(n));
        }
        if (timer.elapsed() > timeoutMs) {
            setError(QStringLiteral("Command execution timeout"));
            break;
        }
        QCoreApplication::processEvents();
    }

    // 读取 stderr
    while ((n = libssh2_channel_read_stderr(channel, buf, sizeof(buf))) > 0) {
        output.append(buf, static_cast<int>(n));
    }

    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    libssh2_session_set_blocking(m_session, 0);

    return QString::fromUtf8(output);
}

SshConnectionConfig SshClient::config() const
{
    return m_config;
}

QString SshClient::lastError() const
{
    return m_lastError;
}

bool SshClient::resizeShellPty(int channelId, int cols, int rows)
{
    QMutexLocker locker(&m_mutex);
    SshChannel* ch = findChannel(channelId);
    if (!ch || !ch->channel) return false;

    int rc = libssh2_channel_request_pty_size(ch->channel, cols, rows);
    return rc == 0;
}

// ============================================================
// 私有方法
// ============================================================

bool SshClient::waitLibssh2(int(*callback)(LIBSSH2_SESSION*), const char* action)
{
    int rc;
    QElapsedTimer timer;
    timer.start();

    while ((rc = callback(m_session)) == LIBSSH2_ERROR_EAGAIN) {
        if (timer.elapsed() > m_config.connectTimeout) {
            setError(QStringLiteral("%1 timeout").arg(action));
            return false;
        }
        // 等待 socket 可读/可写
        m_socket->waitForReadyRead(50);
        QCoreApplication::processEvents();
    }

    if (rc) {
        char* errMsg = nullptr;
        libssh2_session_last_error(m_session, &errMsg, nullptr, 0);
        setError(QStringLiteral("%1 failed: %2 (%3)").arg(action).arg(errMsg).arg(rc));
        return false;
    }
    return true;
}

bool SshClient::doHandshake()
{
    return waitLibssh2(
        [](LIBSSH2_SESSION* s) -> int {
            return libssh2_session_handshake(s, -1);
        },
        "SSH handshake"
    );
}

bool SshClient::doAuthenticate()
{
    int rc;

    if (m_config.authMethod == SshConnectionConfig::PublicKey) {
        // 公钥认证
        rc = libssh2_userauth_publickey_fromfile(
            m_session,
            m_config.username.toUtf8().constData(),
            nullptr,  // 公钥（nullptr = 从私钥推导）
            m_config.privateKeyPath.toUtf8().constData(),
            m_config.passphrase.toUtf8().constData()
        );

        // 非阻塞轮询
        QElapsedTimer timer;
        timer.start();
        while (rc == LIBSSH2_ERROR_EAGAIN) {
            if (timer.elapsed() > m_config.connectTimeout) {
                setError(QStringLiteral("Public key auth timeout"));
                return false;
            }
            m_socket->waitForReadyRead(50);
            QCoreApplication::processEvents();
            rc = libssh2_userauth_publickey_fromfile(
                m_session,
                m_config.username.toUtf8().constData(),
                nullptr,
                m_config.privateKeyPath.toUtf8().constData(),
                m_config.passphrase.toUtf8().constData()
            );
        }

        if (rc) {
            char* errMsg = nullptr;
            libssh2_session_last_error(m_session, &errMsg, nullptr, 0);
            setError(QStringLiteral("Public key auth failed: %1").arg(errMsg));
            return false;
        }
    } else {
        // 密码认证
        rc = libssh2_userauth_password(
            m_session,
            m_config.username.toUtf8().constData(),
            m_config.password.toUtf8().constData()
        );

        QElapsedTimer timer;
        timer.start();
        while (rc == LIBSSH2_ERROR_EAGAIN) {
            if (timer.elapsed() > m_config.connectTimeout) {
                setError(QStringLiteral("Password auth timeout"));
                return false;
            }
            m_socket->waitForReadyRead(50);
            QCoreApplication::processEvents();
            rc = libssh2_userauth_password(
                m_session,
                m_config.username.toUtf8().constData(),
                m_config.password.toUtf8().constData()
            );
        }

        if (rc) {
            char* errMsg = nullptr;
            libssh2_session_last_error(m_session, &errMsg, nullptr, 0);
            setError(QStringLiteral("Password auth failed: %1").arg(errMsg));
            return false;
        }
    }

    return true;
}

SshChannel* SshClient::findChannel(int channelId)
{
    for (auto& ch : m_channels) {
        if (ch.id == channelId) return &ch;
    }
    return nullptr;
}

void SshClient::pollChannels()
{
    QMutexLocker locker(&m_mutex);
    for (auto& ch : m_channels) {
        if (!ch.isOpen || !ch.channel) continue;

        QByteArray data;
        char buf[4096];
        ssize_t n;
        while ((n = libssh2_channel_read(ch.channel, buf, sizeof(buf))) > 0) {
            data.append(buf, static_cast<int>(n));
        }

        if (!data.isEmpty()) {
            int id = ch.id;
            locker.unlock();
            emit shellDataReady(id, data);
            locker.relock();
        }

        if (libssh2_channel_eof(ch.channel)) {
            ch.isOpen = false;
            int id = ch.id;
            locker.unlock();
            emit shellClosed(id);
            locker.relock();
        }
    }
}

void SshClient::setError(const QString& err)
{
    m_lastError = err;
    emit errorOccurred(err);
}

// ============================================================
// 槽函数
// ============================================================

void SshClient::onSocketReadyRead()
{
    if (!m_session || !m_connected) return;
    pollChannels();
}

void SshClient::onSocketDisconnected()
{
    m_connected = false;
    m_keepaliveTimer->stop();
    emit connectionStateChanged(false);
}

void SshClient::onKeepaliveTimer()
{
    if (!m_connected || !m_session) return;

    // 发送 keepalive
    libssh2_keepalive_send(m_session, nullptr);

    // 顺便轮询通道数据
    pollChannels();
}
