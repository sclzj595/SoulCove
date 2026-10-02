#ifndef REMOTEFILECACHE_H
#define REMOTEFILECACHE_H

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QByteArray>
#include <QString>
#include <QMutex>

/// @brief 远程文件缓存条目
///
/// 记录远程文件的本地缓存路径、远程元数据和缓存时间，
/// 用于 LRU 淘汰策略和 mtime 一致性校验。
struct CacheEntry {
    QString     localPath;     ///< 本地缓存文件路径
    QString     remotePath;    ///< 远程文件路径
    QDateTime   remoteMtime;   ///< 远程文件最近修改时间（用于一致性校验）
    qint64      remoteSize = 0;///< 远程文件大小（字节）
    QByteArray  content;       ///< 文件内容（小文件直接缓存内存，大文件存磁盘）
    QDateTime   cachedAt;      ///< 缓存写入时间（LRU 淘汰依据）
};

/// @brief 远程文件缓存 — 减少 SFTP 重复传输
///
/// 设计目标：
/// - 缓存命中且 mtime 一致时直接返回内容，避免重复 SFTP 下载
/// - LRU 淘汰策略：上限 100MB，超限时按 cachedAt 淘汰最旧条目
/// - 按会话名隔离缓存目录，支持整会话清空
///
/// 缓存存储位置：
///   QStandardPaths::writableLocation(CacheLocation) + "/scnb_remote_cache/<sessionName>/"
///
/// 线程安全：所有公开方法均通过 QMutex 串行化，可在多线程下使用。
class RemoteFileCache : public QObject
{
    Q_OBJECT

public:
    /// 获取全局单例
    static RemoteFileCache& instance();

    // 禁用拷贝
    RemoteFileCache(const RemoteFileCache&) = delete;
    RemoteFileCache& operator=(const RemoteFileCache&) = delete;

    /// @brief 是否存在该远程路径的缓存条目
    bool has(const QString& remotePath) const;

    /// @brief 取缓存内容
    /// @param remotePath 远程文件路径
    /// @param remoteMtime 调用方已知的远程 mtime；命中且 mtime 一致时返回内容，否则返回空 QByteArray
    QByteArray get(const QString& remotePath, const QDateTime& remoteMtime) const;

    /// @brief 写入缓存
    /// @param remotePath 远程文件路径
    /// @param remoteMtime 远程文件 mtime
    /// @param size 远程文件大小
    /// @param content 文件内容
    /// @param sessionName 所属会话名（用于按会话清空；可为空，将使用 "default"）
    void put(const QString& remotePath, const QDateTime& remoteMtime,
             qint64 size, const QByteArray& content,
             const QString& sessionName = QString());

    /// @brief 失效单文件缓存
    void invalidate(const QString& remotePath);

    /// @brief 清空指定会话的所有缓存
    void clearForSession(const QString& sessionName);

    /// @brief 清空全部缓存
    void clearAll();

    /// @brief 当前缓存总大小（字节）
    qint64 totalSize() const;

    /// @brief 缓存条目数量
    int entryCount() const;

    /// @brief 获取缓存上限（字节，默认 100MB）
    qint64 maxCacheSize() const { return m_maxCacheSize; }

    /// @brief 设置缓存上限（字节）
    void setMaxCacheSize(qint64 bytes) { m_maxCacheSize = bytes; }

private:
    explicit RemoteFileCache(QObject* parent = nullptr);
    ~RemoteFileCache() override = default;

    /// 获取会话对应的缓存根目录（自动创建）
    QString sessionCacheDir(const QString& sessionName) const;

    /// LRU 淘汰：当总大小超过 m_maxCacheSize 时按 cachedAt 升序删除最旧条目
    void evictIfNeeded();

    /// 内部清理单条目（移除内存 + 删除磁盘文件）
    void removeEntryInternal(const QString& remotePath);

    mutable QMutex m_mutex;
    QHash<QString, CacheEntry> m_entries;   ///< remotePath → CacheEntry
    qint64 m_totalSize = 0;                 ///< 当前缓存总字节数
    qint64 m_maxCacheSize = 100LL * 1024 * 1024;  ///< 默认上限 100MB
};

#endif // REMOTEFILECACHE_H
