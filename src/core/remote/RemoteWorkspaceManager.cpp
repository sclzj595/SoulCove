#include "core/remote/RemoteWorkspaceManager.h"
#include "core/remote/SftpClient.h"
#include "Logger.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QSet>
#include <QFile>
#include <QFileDevice>

// ============================================================
// 单例
// ============================================================

RemoteWorkspaceManager& RemoteWorkspaceManager::instance()
{
    static RemoteWorkspaceManager mgr;
    return mgr;
}

RemoteWorkspaceManager::RemoteWorkspaceManager(QObject* parent)
    : QObject(parent)
{
    // 后台同步定时器
    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(m_syncIntervalMs);
    connect(m_syncTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker locker(&m_mutex);
        // 复制条目，避免同步过程中 m_mounts 被修改
        QList<MountEntry> entries = m_mounts.values();
        locker.unlock();

        for (const auto& entry : entries) {
            SftpClient* sftp = m_sftpClients.value(entry.sessionName);
            if (!sftp) {
                LOG_DEBUG("[RemoteWorkspaceManager] 会话 " << entry.sessionName.toStdString()
                          << " 无 SFTP 客户端，跳过同步");
                continue;
            }
            emit syncStarted(entry.localMountPoint);
            int updated = syncMount(entry, sftp);
            emit syncFinished(entry.localMountPoint, updated);
        }
    });

    LOG_INFO("[RemoteWorkspaceManager] 远程工作区挂载管理器已初始化，同步间隔=" << m_syncIntervalMs << "ms");
}

RemoteWorkspaceManager::~RemoteWorkspaceManager()
{
    if (m_syncTimer) {
        m_syncTimer->stop();
    }
    // 清理所有本地挂载目录
    QMutexLocker locker(&m_mutex);
    for (auto it = m_mounts.begin(); it != m_mounts.end(); ++it) {
        QDir d(it.value().localMountPoint);
        if (d.exists()) {
            d.removeRecursively();
        }
    }
    m_mounts.clear();
}

// ============================================================
// 私有辅助
// ============================================================

QString RemoteWorkspaceManager::mountsRoot() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    return base + QStringLiteral("/scnb_mounts");
}

QString RemoteWorkspaceManager::normalizeMountPoint(const QString& path)
{
    if (path.isEmpty()) return path;
    QString p = QDir(path).absolutePath();
    // 去除尾部分隔符（保留根路径 "/"）
    while (p.length() > 1 && p.endsWith(QLatin1Char('/'))) {
        p.chop(1);
    }
    return p;
}

// ============================================================
// 公开 API
// ============================================================

QString RemoteWorkspaceManager::mountRemote(const QString& sessionName, const QString& remoteDir,
                                             const QString& localMountPoint)
{
    if (sessionName.isEmpty() || remoteDir.isEmpty()) {
        LOG_WARN("[RemoteWorkspaceManager] mountRemote 参数为空: session=" << sessionName.toStdString()
                  << " remote=" << remoteDir.toStdString());
        return QString();
    }

    // 确定本地挂载点
    QString mountPoint = localMountPoint.isEmpty()
        ? mountsRoot() + QStringLiteral("/") + sessionName
        : localMountPoint;
    mountPoint = normalizeMountPoint(mountPoint);

    QMutexLocker locker(&m_mutex);

    // 已挂载则直接返回
    if (m_mounts.contains(mountPoint)) {
        LOG_INFO("[RemoteWorkspaceManager] 挂载点已存在: " << mountPoint.toStdString());
        return mountPoint;
    }

    // 创建本地挂载目录
    QDir().mkpath(mountPoint);
    if (!QDir(mountPoint).exists()) {
        LOG_ERROR("[RemoteWorkspaceManager] 无法创建本地挂载目录: " << mountPoint.toStdString());
        return QString();
    }

    MountEntry entry;
    entry.sessionName = sessionName;
    entry.remoteDir = remoteDir;
    entry.localMountPoint = mountPoint;
    m_mounts.insert(mountPoint, entry);

    LOG_INFO("[RemoteWorkspaceManager] 已挂载: " << sessionName.toStdString()
             << " → " << mountPoint.toStdString()
             << " (remote=" << remoteDir.toStdString() << ")");

    locker.unlock();

    // 启动定时同步（如未启动）
    if (!m_syncTimer->isActive()) {
        m_syncTimer->start();
    }

    emit mountCreated(mountPoint, remoteDir);
    return mountPoint;
}

void RemoteWorkspaceManager::unmount(const QString& mountPoint)
{
    QString mp = normalizeMountPoint(mountPoint);
    QMutexLocker locker(&m_mutex);
    auto it = m_mounts.find(mp);
    if (it == m_mounts.end()) {
        LOG_DEBUG("[RemoteWorkspaceManager] 卸载失败：未找到挂载点 " << mp.toStdString());
        return;
    }

    // 删除本地挂载目录
    QDir d(it.value().localMountPoint);
    if (d.exists()) {
        d.removeRecursively();
    }
    m_mounts.erase(it);

    LOG_INFO("[RemoteWorkspaceManager] 已卸载: " << mp.toStdString());

    // 无活跃挂载时停止定时器
    if (m_mounts.isEmpty() && m_syncTimer) {
        m_syncTimer->stop();
    }

    locker.unlock();
    emit mountRemoved(mp);
}

void RemoteWorkspaceManager::unmountForSession(const QString& sessionName)
{
    QMutexLocker locker(&m_mutex);
    QStringList toRemove;
    for (auto it = m_mounts.begin(); it != m_mounts.end(); ++it) {
        if (it.value().sessionName == sessionName) {
            toRemove.append(it.key());
        }
    }
    locker.unlock();

    for (const QString& mp : toRemove) {
        unmount(mp);
    }
}

bool RemoteWorkspaceManager::isMounted(const QString& mountPoint) const
{
    QMutexLocker locker(&m_mutex);
    return m_mounts.contains(normalizeMountPoint(mountPoint));
}

QStringList RemoteWorkspaceManager::activeMounts() const
{
    QMutexLocker locker(&m_mutex);
    return m_mounts.keys();
}

QString RemoteWorkspaceManager::remoteDirForMount(const QString& mountPoint) const
{
    QMutexLocker locker(&m_mutex);
    auto it = m_mounts.constFind(normalizeMountPoint(mountPoint));
    return it != m_mounts.constEnd() ? it.value().remoteDir : QString();
}

QString RemoteWorkspaceManager::sessionForMount(const QString& mountPoint) const
{
    QMutexLocker locker(&m_mutex);
    auto it = m_mounts.constFind(normalizeMountPoint(mountPoint));
    return it != m_mounts.constEnd() ? it.value().sessionName : QString();
}

void RemoteWorkspaceManager::setSftpClient(const QString& sessionName, SftpClient* client)
{
    QMutexLocker locker(&m_mutex);
    m_sftpClients[sessionName] = client;
    LOG_DEBUG("[RemoteWorkspaceManager] 已关联 SFTP 客户端: session=" << sessionName.toStdString());
}

// ============================================================
// 同步逻辑
// ============================================================

int RemoteWorkspaceManager::syncMount(const MountEntry& entry, SftpClient* sftp)
{
    if (!sftp || !sftp->isAvailable()) {
        emit syncError(entry.localMountPoint, QStringLiteral("SFTP 不可用"));
        return 0;
    }

    int updated = 0;
    try {
        updated = syncDirectory(sftp, entry.remoteDir, entry.localMountPoint);
    } catch (...) {
        emit syncError(entry.localMountPoint, QStringLiteral("同步过程异常"));
        return 0;
    }
    return updated;
}

int RemoteWorkspaceManager::syncDirectory(SftpClient* sftp,
                                          const QString& remoteDir, const QString& localDir)
{
    int updated = 0;

    // 1. 列出远程目录
    QList<SftpFileInfo> remoteEntries = sftp->listDir(remoteDir);

    // 2. 收集本地现有文件名集合，用于检测本地新增文件
    QDir localQDir(localDir);
    if (!localQDir.exists()) {
        QDir().mkpath(localDir);
    }
    QSet<QString> localNames;
    QHash<QString, QString> localFilePaths;  // name → absolute path
    QHash<QString, QDateTime> localMtimes;   // name → mtime
    for (const QFileInfo& fi : localQDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        localNames.insert(fi.fileName());
        localFilePaths[fi.fileName()] = fi.absoluteFilePath();
        localMtimes[fi.fileName()] = fi.lastModified();
    }

    // 3. 遍历远程条目，按 mtime 比较下载
    QSet<QString> remoteNames;
    for (const SftpFileInfo& rf : remoteEntries) {
        if (rf.name == "." || rf.name == "..") continue;
        remoteNames.insert(rf.name);

        QString remotePath = rf.fullPath.isEmpty()
            ? remoteDir + QStringLiteral("/") + rf.name
            : rf.fullPath;
        QString localPath = localDir + QStringLiteral("/") + rf.name;

        if (rf.isDirectory) {
            // 递归同步子目录
            QDir().mkpath(localPath);
            updated += syncDirectory(sftp, remotePath, localPath);
        } else if (rf.isSymlink) {
            // 符号链接跳过（避免循环）
            continue;
        } else {
            // 文件：按 mtime 比较
            QDateTime remoteMtime = rf.lastModified;
            QDateTime localMtime = localMtimes.value(rf.name);

            // 跳过大文件（>10MB）
            const qint64 kMaxSyncSize = 10 * 1024 * 1024;
            if (rf.size > kMaxSyncSize) {
                continue;
            }

            bool needDownload = false;
            if (!localNames.contains(rf.name)) {
                needDownload = true;  // 本地不存在
            } else if (remoteMtime.isValid() && localMtime.isValid() &&
                       remoteMtime.toSecsSinceEpoch() > localMtime.toSecsSinceEpoch()) {
                needDownload = true;  // 远程较新
            }

            if (needDownload) {
                if (sftp->download(remotePath, localPath)) {
                    // 同步本地 mtime 为远程 mtime（避免下次重复下载）
                    if (remoteMtime.isValid()) {
                        QFile f(localPath);
                        f.open(QIODevice::ReadWrite);
                        f.setFileTime(remoteMtime, QFileDevice::FileModificationTime);
                    }
                    updated++;
                }
            }
        }
    }

    // 4. 检测本地新增/修改文件（远程不存在的）→ 上传
    for (const QString& name : localNames) {
        if (remoteNames.contains(name)) continue;

        QString localPath = localFilePaths.value(name);
        QString remotePath = remoteDir + QStringLiteral("/") + name;
        QFileInfo fi(localPath);

        if (fi.isDir()) {
            // 本地新增目录 → 远程 mkdir
            if (sftp->mkdir(remotePath)) {
                updated += syncDirectory(sftp, remotePath, localPath);
            }
        } else {
            // 跳过大文件
            const qint64 kMaxSyncSize = 10 * 1024 * 1024;
            if (fi.size() > kMaxSyncSize) continue;

            if (sftp->upload(localPath, remotePath)) {
                updated++;
            }
        }
    }

    return updated;
}
