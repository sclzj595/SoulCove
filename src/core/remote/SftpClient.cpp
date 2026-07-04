#include "core/remote/SftpClient.h"
#include "core/remote/SshClient.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>

// ============================================================
// 内部辅助：阻塞模式守卫
// ============================================================

namespace {

/// RAII 守卫：进入时设置阻塞模式，离开时恢复非阻塞模式
/// SshClient 默认使用非阻塞模式驱动 Shell 通道轮询，
/// SFTP 操作期间临时切换为阻塞模式，操作结束后恢复，
/// 避免影响 SshClient 的非阻塞 I/O 流程。
class ScopedBlocking {
public:
    explicit ScopedBlocking(LIBSSH2_SESSION* session) : m_session(session) {
        if (m_session) {
            libssh2_session_set_blocking(m_session, 1);
        }
    }
    ~ScopedBlocking() {
        if (m_session) {
            libssh2_session_set_blocking(m_session, 0);
        }
    }
    Q_DISABLE_COPY(ScopedBlocking)
private:
    LIBSSH2_SESSION* m_session;
};

} // namespace

// ============================================================
// 构造 / 析构
// ============================================================

SftpClient::SftpClient(SshClient* sshClient, QObject* parent)
    : ISftpClient(parent)
    , m_sshClient(sshClient)
{
}

SftpClient::~SftpClient()
{
    close();
}

// ============================================================
// 会话管理
// ============================================================

bool SftpClient::init()
{
    if (!m_sshClient || !m_sshClient->isConnected()) {
        setError(QStringLiteral("SSH 未连接"));
        return false;
    }

    if (m_sftp) {
        return true;  // 已初始化
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    if (!session) {
        setError(QStringLiteral("SSH 会话无效"));
        return false;
    }

    // SFTP 初始化需要阻塞模式
    ScopedBlocking guard(session);

    m_sftp = libssh2_sftp_init(session);
    if (!m_sftp) {
        char* errMsg = nullptr;
        int errCode = libssh2_session_last_error(session, &errMsg, nullptr, 0);
        setError(QStringLiteral("SFTP 初始化失败: %1 (%2)")
                 .arg(QString::fromUtf8(errMsg)).arg(errCode));
        return false;
    }

    return true;
}

bool SftpClient::isAvailable() const
{
    return m_sftp != nullptr && m_sshClient && m_sshClient->isConnected();
}

void SftpClient::close()
{
    if (!m_sftp) {
        return;
    }

    LIBSSH2_SESSION* session = m_sshClient ? m_sshClient->rawSession() : nullptr;
    ScopedBlocking guard(session);

    libssh2_sftp_shutdown(m_sftp);
    m_sftp = nullptr;
}

QString SftpClient::lastError() const
{
    return m_lastError;
}

// ============================================================
// 目录操作
// ============================================================

QList<SftpFileInfo> SftpClient::listDir(const QString& path)
{
    QList<SftpFileInfo> result;

    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        return result;
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_opendir(m_sftp, path.toUtf8().constData());
    if (!handle) {
        unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
        setError(QStringLiteral("打开目录失败: %1 (SFTP错误码: %2)").arg(path).arg(sftpErr));
        return result;
    }

    char nameBuf[512];
    char longentry[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    int rc;
    while ((rc = libssh2_sftp_readdir_ex(handle,
                                         nameBuf, sizeof(nameBuf),
                                         longentry, sizeof(longentry),
                                         &attrs)) > 0) {
        QString name = QString::fromUtf8(nameBuf);

        // 跳过 "." 和 ".."
        if (name == "." || name == "..") {
            continue;
        }

        SftpFileInfo info;
        info.name = name;

        // 构建完整路径
        QString base = path;
        if (!base.endsWith('/')) {
            base += '/';
        }
        info.fullPath = base + info.name;

        // 解析属性
        if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
            info.isSymlink   = LIBSSH2_SFTP_S_ISLNK(attrs.permissions);
            info.isDirectory = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
            info.permissions = modeToPermissions(attrs.permissions);
        }

        if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
            info.size = attrs.filesize;
        }

        if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
            info.lastModified = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(attrs.mtime));
        }

        result.append(info);
    }

    if (rc < 0) {
        setError(QStringLiteral("读取目录条目失败: %1").arg(rc));
    }

    libssh2_sftp_close_handle(handle);
    return result;
}

bool SftpClient::mkdir(const QString& path)
{
    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        return false;
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    int rc = libssh2_sftp_mkdir(m_sftp, path.toUtf8().constData(),
                                LIBSSH2_SFTP_DEFAULT_MODE);
    if (rc) {
        unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
        setError(QStringLiteral("创建目录失败: %1 (SFTP错误码: %2)").arg(path).arg(sftpErr));
        return false;
    }
    return true;
}

bool SftpClient::remove(const QString& path)
{
    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        return false;
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    // 先尝试作为文件删除（unlink），失败再尝试作为目录删除（rmdir）
    int rc = libssh2_sftp_unlink(m_sftp, path.toUtf8().constData());
    if (rc) {
        rc = libssh2_sftp_rmdir(m_sftp, path.toUtf8().constData());
        if (rc) {
            unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
            setError(QStringLiteral("删除失败: %1 (SFTP错误码: %2)").arg(path).arg(sftpErr));
            return false;
        }
    }
    return true;
}

bool SftpClient::rename(const QString& oldPath, const QString& newPath)
{
    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        return false;
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    int rc = libssh2_sftp_rename(m_sftp,
                                 oldPath.toUtf8().constData(),
                                 newPath.toUtf8().constData());
    if (rc) {
        unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
        setError(QStringLiteral("重命名失败: %1 -> %2 (SFTP错误码: %3)")
                 .arg(oldPath).arg(newPath).arg(sftpErr));
        return false;
    }
    return true;
}

bool SftpClient::exists(const QString& path)
{
    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        return false;
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc = libssh2_sftp_stat(m_sftp, path.toUtf8().constData(), &attrs);
    return rc == 0;
}

// ============================================================
// 文件传输
// ============================================================

bool SftpClient::download(const QString& remotePath, const QString& localPath,
                          std::function<void(qint64, qint64)> progress)
{
    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        emit transferError(remotePath, m_lastError);
        return false;
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    // 打开远程文件（读模式）
    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(m_sftp,
        remotePath.toUtf8().constData(),
        LIBSSH2_FXF_READ, 0);
    if (!handle) {
        unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
        setError(QStringLiteral("打开远程文件失败: %1 (SFTP错误码: %2)")
                 .arg(remotePath).arg(sftpErr));
        emit transferError(remotePath, m_lastError);
        return false;
    }

    // 通过 fstat 获取文件大小
    qint64 totalSize = 0;
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    if (libssh2_sftp_fstat(handle, &attrs) == 0 &&
        (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)) {
        totalSize = static_cast<qint64>(attrs.filesize);
    }

    // 打开本地文件
    QFile localFile(localPath);
    if (!localFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(QStringLiteral("打开本地文件失败: %1").arg(localPath));
        libssh2_sftp_close_handle(handle);
        emit transferError(remotePath, m_lastError);
        return false;
    }

    // 分块传输（每 4KB 触发进度回调）
    char buffer[4096];
    ssize_t n;
    qint64 done = 0;

    while ((n = libssh2_sftp_read(handle, buffer, sizeof(buffer))) > 0) {
        localFile.write(buffer, static_cast<int>(n));
        done += n;

        if (progress) {
            progress(done, totalSize);
        }
        emit transferProgress(done, totalSize);
    }

    localFile.close();
    libssh2_sftp_close_handle(handle);

    if (n < 0) {
        setError(QStringLiteral("下载读取失败: %1").arg(n));
        emit transferError(remotePath, m_lastError);
        return false;
    }

    emit transferComplete(remotePath);
    return true;
}

bool SftpClient::upload(const QString& localPath, const QString& remotePath,
                        std::function<void(qint64, qint64)> progress)
{
    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        emit transferError(remotePath, m_lastError);
        return false;
    }

    // 打开本地文件
    QFile localFile(localPath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("打开本地文件失败: %1").arg(localPath));
        emit transferError(remotePath, m_lastError);
        return false;
    }

    qint64 totalSize = localFile.size();

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    // 打开远程文件（写模式：写|创建|截断）
    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(m_sftp,
        remotePath.toUtf8().constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        0644);
    if (!handle) {
        unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
        setError(QStringLiteral("打开远程文件失败: %1 (SFTP错误码: %2)")
                 .arg(remotePath).arg(sftpErr));
        localFile.close();
        emit transferError(remotePath, m_lastError);
        return false;
    }

    // 分块传输（每 4KB 触发进度回调）
    char buffer[4096];
    qint64 n;
    qint64 done = 0;

    while ((n = localFile.read(buffer, sizeof(buffer))) > 0) {
        ssize_t written = 0;
        // 循环写入直到本块全部完成
        while (written < n) {
            ssize_t rc = libssh2_sftp_write(handle,
                                            buffer + written,
                                            static_cast<size_t>(n - written));
            if (rc < 0) {
                setError(QStringLiteral("上传写入失败: %1").arg(rc));
                localFile.close();
                libssh2_sftp_close_handle(handle);
                emit transferError(remotePath, m_lastError);
                return false;
            }
            written += rc;
        }
        done += n;

        if (progress) {
            progress(done, totalSize);
        }
        emit transferProgress(done, totalSize);
    }

    localFile.close();
    libssh2_sftp_close_handle(handle);

    if (n < 0) {
        setError(QStringLiteral("读取本地文件失败"));
        emit transferError(remotePath, m_lastError);
        return false;
    }

    emit transferComplete(remotePath);
    return true;
}

// ============================================================
// 流式读写
// ============================================================

QByteArray SftpClient::readFile(const QString& remotePath, qint64 maxSize)
{
    QByteArray result;

    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        return result;
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(m_sftp,
        remotePath.toUtf8().constData(),
        LIBSSH2_FXF_READ, 0);
    if (!handle) {
        unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
        setError(QStringLiteral("打开远程文件失败: %1 (SFTP错误码: %2)")
                 .arg(remotePath).arg(sftpErr));
        return result;
    }

    char buffer[4096];
    ssize_t n;
    qint64 total = 0;

    while (total < maxSize) {
        size_t toRead = static_cast<size_t>(std::min<qint64>(
            static_cast<qint64>(sizeof(buffer)), maxSize - total));
        n = libssh2_sftp_read(handle, buffer, toRead);
        if (n > 0) {
            result.append(buffer, static_cast<int>(n));
            total += n;
        } else {
            break;  // EOF (0) 或错误 (<0)
        }
    }

    libssh2_sftp_close_handle(handle);

    if (n < 0) {
        setError(QStringLiteral("读取文件失败: %1").arg(n));
        return QByteArray();
    }

    return result;
}

bool SftpClient::writeFile(const QString& remotePath, const QByteArray& data)
{
    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        return false;
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(m_sftp,
        remotePath.toUtf8().constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        0644);
    if (!handle) {
        unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
        setError(QStringLiteral("打开远程文件失败: %1 (SFTP错误码: %2)")
                 .arg(remotePath).arg(sftpErr));
        return false;
    }

    ssize_t written = 0;
    while (written < data.size()) {
        ssize_t rc = libssh2_sftp_write(handle,
                                        data.constData() + written,
                                        static_cast<size_t>(data.size() - written));
        if (rc < 0) {
            setError(QStringLiteral("写入文件失败: %1").arg(rc));
            libssh2_sftp_close_handle(handle);
            return false;
        }
        written += rc;
    }

    libssh2_sftp_close_handle(handle);
    return true;
}

// ============================================================
// P3-M01 子项1: 远程文件 mtime 查询（缓存一致性校验）
// ============================================================

QDateTime SftpClient::fileMtime(const QString& remotePath)
{
    if (!isAvailable()) {
        setError(QStringLiteral("SFTP 未初始化"));
        return QDateTime();
    }

    LIBSSH2_SESSION* session = m_sshClient->rawSession();
    ScopedBlocking guard(session);

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc = libssh2_sftp_stat(m_sftp, remotePath.toUtf8().constData(), &attrs);
    if (rc != 0) {
        unsigned long sftpErr = libssh2_sftp_last_error(m_sftp);
        setError(QStringLiteral("获取文件属性失败: %1 (SFTP错误码: %2)")
                 .arg(remotePath).arg(sftpErr));
        return QDateTime();
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        // SFTP mtime 为秒级时间戳
        return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(attrs.mtime));
    }
    return QDateTime();
}

// ============================================================
// 私有辅助
// ============================================================

void SftpClient::setError(const QString& err)
{
    m_lastError = err;
}

QString SftpClient::modeToPermissions(unsigned long mode)
{
    // 转换为 "rwxr-xr-x" 格式（9 字符）
    QString result;
    result.reserve(9);

    // 用户权限
    result += (mode & LIBSSH2_SFTP_S_IRUSR) ? QLatin1Char('r') : QLatin1Char('-');
    result += (mode & LIBSSH2_SFTP_S_IWUSR) ? QLatin1Char('w') : QLatin1Char('-');
    result += (mode & LIBSSH2_SFTP_S_IXUSR) ? QLatin1Char('x') : QLatin1Char('-');
    // 组权限
    result += (mode & LIBSSH2_SFTP_S_IRGRP) ? QLatin1Char('r') : QLatin1Char('-');
    result += (mode & LIBSSH2_SFTP_S_IWGRP) ? QLatin1Char('w') : QLatin1Char('-');
    result += (mode & LIBSSH2_SFTP_S_IXGRP) ? QLatin1Char('x') : QLatin1Char('-');
    // 其他权限
    result += (mode & LIBSSH2_SFTP_S_IROTH) ? QLatin1Char('r') : QLatin1Char('-');
    result += (mode & LIBSSH2_SFTP_S_IWOTH) ? QLatin1Char('w') : QLatin1Char('-');
    result += (mode & LIBSSH2_SFTP_S_IXOTH) ? QLatin1Char('x') : QLatin1Char('-');

    return result;
}
