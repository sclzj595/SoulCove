#ifndef ISFTPCLIENT_H
#define ISFTPCLIENT_H

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>
#include <QDateTime>
#include <functional>

/// @brief SFTP 文件信息
struct SftpFileInfo {
    QString name;
    QString fullPath;
    bool isDirectory = false;
    bool isSymlink = false;
    quint64 size = 0;
    QDateTime lastModified;
    QString permissions;  // "rwxr-xr-x"
};

/// @brief SFTP 客户端抽象接口
/// 上层代码只依赖此接口，不依赖 SftpClient（libssh2）具体实现
/// 设计模式：Strategy（可替换为其他 SFTP 后端）
class ISftpClient : public QObject
{
    Q_OBJECT

public:
    explicit ISftpClient(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ISftpClient() = default;

    // === 会话管理 ===
    virtual bool init() = 0;
    virtual bool isAvailable() const = 0;
    virtual void close() = 0;
    virtual QString lastError() const = 0;

    // === 目录操作 ===
    virtual QList<SftpFileInfo> listDir(const QString& path) = 0;
    virtual bool mkdir(const QString& path) = 0;
    virtual bool remove(const QString& path) = 0;
    virtual bool rename(const QString& oldPath, const QString& newPath) = 0;
    virtual bool exists(const QString& path) = 0;

    // === 文件传输 ===
    virtual bool download(const QString& remotePath, const QString& localPath,
                          std::function<void(qint64, qint64)> progress = nullptr) = 0;
    virtual bool upload(const QString& localPath, const QString& remotePath,
                        std::function<void(qint64, qint64)> progress = nullptr) = 0;

    // === 流式读写 ===
    virtual QByteArray readFile(const QString& remotePath, qint64 maxSize = 10 * 1024 * 1024) = 0;
    virtual bool writeFile(const QString& remotePath, const QByteArray& data) = 0;

signals:
    void transferProgress(qint64 done, qint64 total);
    void transferComplete(const QString& path);
    void transferError(const QString& path, const QString& error);
};

#endif // ISFTPCLIENT_H
