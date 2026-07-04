#ifdef _WIN32
#include <winsock2.h>
#endif

#ifndef SSHCLIENT_H
#define SSHCLIENT_H

#include "interfaces/remote/ISshClient.h"
#include <libssh2.h>
#include <QTcpSocket>
#include <QTimer>
#include <QMutex>

/// @brief SSH Shell 通道
struct SshChannel {
    LIBSSH2_CHANNEL* channel = nullptr;
    int              id = -1;
    bool             isOpen = false;

    bool operator==(const SshChannel& other) const { return id == other.id; }
};

/// @brief libssh2 封装的 SSH 客户端实现
///
/// 架构：
///   SshClient (ISshClient)
///   ├── QTcpSocket（TCP传输层）
///   ├── LIBSSH2_SESSION（SSH会话层）
///   └── SshChannel[] × N（Shell通道）
///
/// 特性：
///   - 非阻塞 I/O（Qt事件循环驱动）
///   - 密码/公钥双认证
///   - 多 Shell 通道
///   - PTY 终端尺寸调整
///   - RAII 资源管理
class SshClient : public ISshClient
{
    Q_OBJECT

public:
    explicit SshClient(QObject* parent = nullptr);
    ~SshClient() override;

    // 禁用拷贝
    SshClient(const SshClient&) = delete;
    SshClient& operator=(const SshClient&) = delete;

    // === ISshClient 接口实现 ===
    bool connect(const SshConnectionConfig& config) override;
    void disconnect() override;
    bool isConnected() const override;
    int openShellChannel() override;
    bool writeShell(int channelId, const QByteArray& data) override;
    QByteArray readShell(int channelId) override;
    void closeShell(int channelId) override;
    QString executeCommand(const QString& command, int timeoutMs = 30000) override;
    SshConnectionConfig config() const override;
    QString lastError() const override;
    bool resizeShellPty(int channelId, int cols, int rows) override;

    /// 暴露原始 SSH 会话指针（供 SftpClient 等内部模块使用）
    LIBSSH2_SESSION* rawSession() const { return m_session; }
    /// 暴露原始 socket（供 SFTP 等待数据用）
    QTcpSocket* rawSocket() const { return m_socket; }

private slots:
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onKeepaliveTimer();

private:
    /// 等待 libssh2 操作完成（非阻塞轮询）
    bool waitLibssh2(int(*callback)(LIBSSH2_SESSION*), const char* action);

    /// 执行 SSH 握手
    bool doHandshake();

    /// 执行认证
    bool doAuthenticate();

    /// 查找通道
    SshChannel* findChannel(int channelId);

    /// 轮询所有通道读取数据
    void pollChannels();

    /// 设置错误信息
    void setError(const QString& err);

private:
    QTcpSocket*         m_socket = nullptr;
    LIBSSH2_SESSION*    m_session = nullptr;
    SshConnectionConfig m_config;
    QString             m_lastError;
    bool                m_connected = false;

    // Shell 通道管理
    QList<SshChannel>   m_channels;
    int                 m_channelCounter = 0;

    // 心跳定时器
    QTimer*             m_keepaliveTimer = nullptr;

    // 互斥锁（保护通道列表）
    mutable QMutex      m_mutex;
};

#endif // SSHCLIENT_H
