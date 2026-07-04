#ifndef SFTPCLIENT_H
#define SFTPCLIENT_H

#include "interfaces/remote/ISftpClient.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

class SshClient;  // 前向声明

/// @brief SFTP 客户端 — 基于 libssh2 SFTP 通道
///
/// 架构：
///   SftpClient 依赖 SshClient 已建立的 SSH 会话
///   通过 LIBSSH2_SFTP* 通道执行文件操作
///
/// 特性：
///   - 目录浏览（listDir）
///   - 文件上传/下载（分块传输）
///   - 文件读写（readFile/writeFile）
///   - 目录创建/删除/重命名
///   - 进度回调
///
/// 实现 ISftpClient 接口（依赖倒置原则）
class SftpClient : public ISftpClient
{
    Q_OBJECT
public:
    explicit SftpClient(SshClient* sshClient, QObject* parent = nullptr);
    ~SftpClient() override;

    // 初始化 SFTP 会话（在 SSH 连接建立后调用）
    bool init() override;
    bool isAvailable() const override;
    void close() override;
    QString lastError() const override;

    // 目录操作
    QList<SftpFileInfo> listDir(const QString& path) override;
    bool mkdir(const QString& path) override;
    bool remove(const QString& path) override;
    bool rename(const QString& oldPath, const QString& newPath) override;
    bool exists(const QString& path) override;

    // 文件传输
    bool download(const QString& remotePath, const QString& localPath,
                  std::function<void(qint64, qint64)> progress = nullptr) override;
    bool upload(const QString& localPath, const QString& remotePath,
                std::function<void(qint64, qint64)> progress = nullptr) override;

    // 流式读写（小文件直接内存操作）
    QByteArray readFile(const QString& remotePath, qint64 maxSize = 10 * 1024 * 1024) override;
    bool writeFile(const QString& remotePath, const QByteArray& data) override;

    // P3-M01 子项1: 获取远程文件 mtime（用于缓存一致性校验）
    QDateTime fileMtime(const QString& remotePath);

private:
    SshClient* m_sshClient = nullptr;
    LIBSSH2_SFTP* m_sftp = nullptr;
    QString m_lastError;

    void setError(const QString& err);
    QString modeToPermissions(unsigned long mode);
};

#endif // SFTPCLIENT_H
