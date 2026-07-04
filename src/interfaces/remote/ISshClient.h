#ifndef ISSHCLIENT_H
#define ISSHCLIENT_H

#include <QObject>
#include <QString>

/// @brief SSH 连接配置
struct SshConnectionConfig {
    QString host;
    int     port = 22;
    QString username;
    QString password;           // 密码认证
    QString privateKeyPath;     // 公钥认证：私钥文件路径
    QString passphrase;         // 私钥密码（可选）
    QString name;               // 连接别名（用于UI显示）
    int     connectTimeout = 10000;  // 连接超时(ms)
    int     keepaliveInterval = 30;  // 心跳间隔(s)

    /// 认证方式
    enum AuthMethod {
        Password,       // 密码认证
        PublicKey       // 公钥认证
    };
    AuthMethod authMethod = Password;

    // P3-M01 子项2: 远程终端持久化（tmux）
    bool    useTmux = false;            ///< 是否启用 tmux 会话持久化
    QString tmuxSessionName;            ///< tmux 会话名（为空时按 "scnb_<name>" 自动生成）
};

/// @brief SSH 客户端抽象接口
/// 上层代码只依赖此接口，不依赖 libssh2 具体实现
class ISshClient : public QObject
{
    Q_OBJECT

public:
    explicit ISshClient(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ISshClient() = default;

    /// 连接到远程主机
    virtual bool connect(const SshConnectionConfig& config) = 0;

    /// 断开连接
    virtual void disconnect() = 0;

    /// 是否已连接
    virtual bool isConnected() const = 0;

    /// 打开 Shell 通道（交互式终端）
    /// @return 通道ID，失败返回-1
    virtual int openShellChannel() = 0;

    /// 向 Shell 通道写入数据
    virtual bool writeShell(int channelId, const QByteArray& data) = 0;

    /// 读取 Shell 通道输出（非阻塞）
    virtual QByteArray readShell(int channelId) = 0;

    /// 关闭 Shell 通道
    virtual void closeShell(int channelId) = 0;

    /// 执行远程命令（非交互式，等待返回）
    virtual QString executeCommand(const QString& command, int timeoutMs = 30000) = 0;

    /// 获取连接配置
    virtual SshConnectionConfig config() const = 0;

    /// 获取最近一次错误信息
    virtual QString lastError() const = 0;

    /// 调整 Shell 通道的终端大小
    virtual bool resizeShellPty(int channelId, int cols, int rows) = 0;

signals:
    /// Shell 通道有数据可读
    void shellDataReady(int channelId, const QByteArray& data);

    /// Shell 通道已关闭
    void shellClosed(int channelId);

    /// 连接状态变化
    void connectionStateChanged(bool connected);

    /// 错误信息
    void errorOccurred(const QString& message);
};

#endif // ISSHCLIENT_H
