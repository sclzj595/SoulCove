#include "core/remote/RemoteFileCache.h"
#include "Logger.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QDateTime>
#include <QCryptographicHash>
#include <QRegularExpression>

// ============================================================
// 单例
// ============================================================

RemoteFileCache& RemoteFileCache::instance()
{
    static RemoteFileCache cache;
    return cache;
}

RemoteFileCache::RemoteFileCache(QObject* parent)
    : QObject(parent)
{
    LOG_INFO("[RemoteFileCache] 远程文件缓存已初始化，上限=" << m_maxCacheSize << " 字节");
}

// ============================================================
// 私有辅助
// ============================================================

QString RemoteFileCache::sessionCacheDir(const QString& sessionName) const
{
    // 缓存根：QStandardPaths::CacheLocation + "/scnb_remote_cache/<sessionName>/"
    // Windows 下 CacheLocation 通常为 %LOCALAPPDATA%/<APPNAME>/cache
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath();  // 兜底
    }
    QString safe = sessionName.isEmpty() ? QStringLiteral("default") : sessionName;
    // 替换路径非法字符，避免会话名包含 / \ : * ? " < > |
    safe.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral("_"));
    return base + QStringLiteral("/scnb_remote_cache/") + safe;
}

void RemoteFileCache::removeEntryInternal(const QString& remotePath)
{
    auto it = m_entries.find(remotePath);
    if (it == m_entries.end()) return;

    const CacheEntry& entry = it.value();
    m_totalSize -= entry.content.size();

    // 删除磁盘缓存文件（若存在）
    if (!entry.localPath.isEmpty() && QFile::exists(entry.localPath)) {
        QFile::remove(entry.localPath);
    }

    m_entries.erase(it);
}

void RemoteFileCache::evictIfNeeded()
{
    // LRU：按 cachedAt 升序排列，从最旧开始删除直到总大小 <= 上限
    while (m_totalSize > m_maxCacheSize && !m_entries.isEmpty()) {
        // 找出 cachedAt 最旧的条目
        auto oldest = m_entries.begin();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it.value().cachedAt < oldest.value().cachedAt) {
                oldest = it;
            }
        }
        LOG_DEBUG("[RemoteFileCache] LRU 淘汰: " << oldest.key().toStdString()
                  << " (cachedAt=" << oldest.value().cachedAt.toString().toStdString() << ")");
        removeEntryInternal(oldest.key());
    }
}

// ============================================================
// 公开 API
// ============================================================

bool RemoteFileCache::has(const QString& remotePath) const
{
    QMutexLocker locker(&m_mutex);
    return m_entries.contains(remotePath);
}

QByteArray RemoteFileCache::get(const QString& remotePath, const QDateTime& remoteMtime) const
{
    QMutexLocker locker(&m_mutex);
    auto it = m_entries.constFind(remotePath);
    if (it == m_entries.constEnd()) {
        return QByteArray();  // 未命中
    }
    const CacheEntry& entry = it.value();

    // 一致性校验：mtime 必须完全一致（精确到秒）
    // 不同 SFTP 服务器返回的 mtime 精度可能不同，统一截断到秒级比较
    if (entry.remoteMtime.toSecsSinceEpoch() != remoteMtime.toSecsSinceEpoch()) {
        LOG_DEBUG("[RemoteFileCache] 缓存失效（mtime 不一致）: " << remotePath.toStdString());
        return QByteArray();
    }

    return entry.content;
}

void RemoteFileCache::put(const QString& remotePath, const QDateTime& remoteMtime,
                          qint64 size, const QByteArray& content,
                          const QString& sessionName)
{
    QMutexLocker locker(&m_mutex);

    // 若已存在同名条目，先移除旧的（避免重复计数）
    if (m_entries.contains(remotePath)) {
        removeEntryInternal(remotePath);
    }

    CacheEntry entry;
    entry.remotePath = remotePath;
    entry.remoteMtime = remoteMtime;
    entry.remoteSize = size;
    entry.content = content;
    entry.cachedAt = QDateTime::currentDateTime();

    // 同时落盘，便于跨会话复用 / 大文件场景（这里仅记录路径，不做 IO 优化）
    // 直接将内容写入本地缓存目录，便于 clearForSession 时一次性 rm -rf
    QString dir = sessionCacheDir(sessionName);
    QDir().mkpath(dir);
    // 用 remotePath 的 hash 作为本地文件名，避免路径分隔符问题
    QString hash = QString::fromLatin1(
        QCryptographicHash::hash(remotePath.toUtf8(), QCryptographicHash::Sha1).toHex());
    QString localPath = dir + QStringLiteral("/") + hash;
    QFile f(localPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(content);
        f.close();
        entry.localPath = localPath;
    } else {
        LOG_WARN("[RemoteFileCache] 无法写入磁盘缓存: " << localPath.toStdString());
    }

    m_totalSize += content.size();
    m_entries.insert(remotePath, entry);

    LOG_DEBUG("[RemoteFileCache] 写入缓存: " << remotePath.toStdString()
              << " size=" << size << " total=" << m_totalSize);

    // LRU 淘汰
    evictIfNeeded();
}

void RemoteFileCache::invalidate(const QString& remotePath)
{
    QMutexLocker locker(&m_mutex);
    removeEntryInternal(remotePath);
    LOG_DEBUG("[RemoteFileCache] 失效单文件缓存: " << remotePath.toStdString());
}

void RemoteFileCache::clearForSession(const QString& sessionName)
{
    QMutexLocker locker(&m_mutex);

    // 收集该会话的所有 remotePath（通过 localPath 前缀匹配）
    QString dir = sessionCacheDir(sessionName);
    QStringList toRemove;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it.value().localPath.startsWith(dir)) {
            toRemove.append(it.key());
        }
    }
    for (const QString& key : toRemove) {
        removeEntryInternal(key);
    }

    // 删除会话缓存目录
    QDir d(dir);
    if (d.exists()) {
        d.removeRecursively();
    }

    LOG_INFO("[RemoteFileCache] 已清空会话缓存: " << sessionName.toStdString()
             << " (" << toRemove.size() << " 条)");
}

void RemoteFileCache::clearAll()
{
    QMutexLocker locker(&m_mutex);
    m_entries.clear();
    m_totalSize = 0;

    // 删除整个 scnb_remote_cache 根目录
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    QString root = base + QStringLiteral("/scnb_remote_cache");
    QDir(root).removeRecursively();

    LOG_INFO("[RemoteFileCache] 已清空全部缓存");
}

qint64 RemoteFileCache::totalSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_totalSize;
}

int RemoteFileCache::entryCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_entries.size();
}
