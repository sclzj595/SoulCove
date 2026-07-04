#include "ui/remote/RemoteFileTree.h"
#include "core/remote/SftpClient.h"
#include "core/remote/RemoteFileCache.h"
#include "ui/dialog/ModernDialog.h"
#include "Logger.hpp"

#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QDateTime>

// 数据角色约定（与 SideBar 保持一致）：
//   Qt::UserRole       → 远程完整路径（占位子节点存 "placeholder"，加载标记存 "loaded"）
//   Qt::UserRole + 1   → 节点类型："dir" / "file" / "placeholder" / "loaded"
//
// 懒加载机制：
//   - 文件夹节点初始添加一个 "placeholder" 占位子节点（显示展开箭头）
//   - 展开时清除占位节点，加载真实子项，并在首位插入隐藏的 "loaded" 标记节点
//   - 再次展开时检测首子节点为 "loaded" 即跳过，避免重复加载

namespace {
/// 文件夹 emoji 图标
const QString kDirIcon = QString::fromUtf8("\xF0\x9F\x93\x81");   // 📁
/// 文件 emoji 图标
const QString kFileIcon = QString::fromUtf8("\xF0\x9F\x93\x84");  // 📄
}

RemoteFileTree::RemoteFileTree(QWidget* parent)
    : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("remoteFileTree"));
    setHeaderHidden(true);
    setAnimated(true);
    setIndentation(16);
    setRootIsDecorated(true);
    setSortingEnabled(false);
    setColumnCount(1);
    setContextMenuPolicy(Qt::CustomContextMenu);

    // 强制使用支持 emoji 的字体，确保文件/文件夹图标正确渲染
    QFont emojiFont = font();
    emojiFont.setFamilies({QStringLiteral("Segoe UI Emoji"),
                           QStringLiteral("Apple Color Emoji"),
                           QStringLiteral("Noto Color Emoji")});
    setFont(emojiFont);

    // 信号连接
    connect(this, &QTreeWidget::itemExpanded,
            this, &RemoteFileTree::onItemExpanded);
    connect(this, &QTreeWidget::itemDoubleClicked,
            this, &RemoteFileTree::onItemDoubleClicked);
    connect(this, &QTreeWidget::customContextMenuRequested,
            this, &RemoteFileTree::onCustomContextMenuRequested);

    LOG_DEBUG("[RemoteFileTree] 远程文件树组件已创建");
}

void RemoteFileTree::setSftpClient(SftpClient* client)
{
    m_sftp = client;
    if (m_sftp && !m_rootPath.isEmpty()) {
        LOG_INFO("[RemoteFileTree] SFTP 客户端已设置，开始加载根路径:" << m_rootPath);
        refresh();
    }
}

void RemoteFileTree::setRootPath(const QString& path)
{
    m_rootPath = path;
    if (m_sftp && !m_rootPath.isEmpty()) {
        refresh();
    }
}

void RemoteFileTree::refresh()
{
    clear();

    if (!m_sftp) {
        LOG_WARN("[RemoteFileTree] 无法刷新：SFTP 客户端未设置");
        return;
    }
    if (m_rootPath.isEmpty()) {
        LOG_WARN("[RemoteFileTree] 无法刷新：根路径为空");
        return;
    }

    // 创建根节点并加载其子项
    loadDir(m_rootPath, nullptr);
    LOG_DEBUG("[RemoteFileTree] 刷新完成，根路径:" << m_rootPath);
}

void RemoteFileTree::loadDir(const QString& path, QTreeWidgetItem* parentItem)
{
    if (!m_sftp) {
        LOG_WARN("[RemoteFileTree] loadDir 失败：SFTP 客户端无效");
        return;
    }

    QList<SftpFileInfo> entries = m_sftp->listDir(path);
    if (entries.isEmpty()) {
        LOG_DEBUG("[RemoteFileTree] 目录为空或读取失败:" << path);
        return;
    }

    for (const SftpFileInfo& info : entries) {
        // 跳过 "." 和 ".."
        if (info.name == QStringLiteral(".") || info.name == QStringLiteral("..")) {
            continue;
        }

        if (info.isDirectory) {
            createDirItem(info, parentItem);
        } else {
            createFileItem(info, parentItem);
        }
    }
}

QTreeWidgetItem* RemoteFileTree::createDirItem(const SftpFileInfo& info, QTreeWidgetItem* parent)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, kDirIcon + QStringLiteral(" ") + info.name);
    item->setData(0, Qt::UserRole, info.fullPath);
    item->setData(0, Qt::UserRole + 1, QStringLiteral("dir"));
    item->setToolTip(0, info.fullPath);
    item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);

    // 添加占位子节点（展开时才真正加载，实现懒加载）
    auto* placeholder = new QTreeWidgetItem(item);
    placeholder->setText(0, QString());
    placeholder->setData(0, Qt::UserRole, QStringLiteral("placeholder"));
    placeholder->setData(0, Qt::UserRole + 1, QStringLiteral("placeholder"));

    return item;
}

QTreeWidgetItem* RemoteFileTree::createFileItem(const SftpFileInfo& info, QTreeWidgetItem* parent)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, kFileIcon + QStringLiteral(" ") + info.name);
    item->setData(0, Qt::UserRole, info.fullPath);
    item->setData(0, Qt::UserRole + 1, QStringLiteral("file"));
    item->setToolTip(0, info.fullPath);

    return item;
}

QString RemoteFileTree::itemPath(QTreeWidgetItem* item) const
{
    if (!item) return QString();
    // 直接使用存储在 UserRole 的完整远程路径
    return item->data(0, Qt::UserRole).toString();
}

void RemoteFileTree::onItemExpanded(QTreeWidgetItem* item)
{
    if (!item || !m_sftp) return;

    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type != QStringLiteral("dir")) return;

    // 懒加载：若第一个子节点标记为 "loaded" 则跳过（已加载过）
    if (item->childCount() > 0) {
        QString firstChildState = item->child(0)->data(0, Qt::UserRole).toString();
        if (firstChildState == QStringLiteral("loaded")) {
            return;
        }
    }

    QString path = itemPath(item);
    if (path.isEmpty()) return;

    // 清除占位子节点
    item->takeChildren();

    // 加载真实子项
    loadDir(path, item);

    // 标记为已加载：插入一个隐藏的标记节点作为第一个子节点
    // 这样后续再次展开时，第一个子节点的 data == "loaded" 即可跳过
    auto* loadedMarker = new QTreeWidgetItem();
    loadedMarker->setText(0, QString());
    loadedMarker->setData(0, Qt::UserRole, QStringLiteral("loaded"));
    loadedMarker->setData(0, Qt::UserRole + 1, QStringLiteral("loaded"));
    loadedMarker->setHidden(true);
    item->insertChild(0, loadedMarker);

    LOG_DEBUG("[RemoteFileTree] 懒加载目录完成:" << path);
}

void RemoteFileTree::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item || !m_sftp) return;

    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type != QStringLiteral("file")) return;

    QString remotePath = itemPath(item);
    if (remotePath.isEmpty()) return;

    // P3-M01 子项1: 优先查询缓存
    // 1. 获取远程文件 mtime（用于一致性校验）
    QDateTime remoteMtime = m_sftp->fileMtime(remotePath);
    if (remoteMtime.isValid()) {
        QByteArray cached = RemoteFileCache::instance().get(remotePath, remoteMtime);
        if (!cached.isEmpty()) {
            // 缓存命中：直接落盘到临时文件供编辑器打开
            QString localTempPath = QDir::tempPath() + QStringLiteral("/") +
                                    QFileInfo(remotePath).fileName();
            QFile f(localTempPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(cached);
                f.close();
                LOG_INFO("[RemoteFileTree] 缓存命中，跳过 SFTP 下载:" << remotePath);
                emit fileDownloaded(remotePath, localTempPath);
                emit fileOpenRequested(remotePath);
                return;
            }
        }
    }

    // 缓存未命中或失效：SFTP 下载
    QString localTempPath = QDir::tempPath() + QStringLiteral("/") +
                            QFileInfo(remotePath).fileName();
    if (!m_sftp->download(remotePath, localTempPath)) {
        LOG_ERROR("[RemoteFileTree] 下载失败:" << remotePath << " 错误:" << m_sftp->lastError());
        ModernDialog::warning(this, tr("下载失败"),
                              tr("无法下载文件:\n%1\n\n错误: %2").arg(remotePath, m_sftp->lastError()));
        return;
    }

    LOG_INFO("[RemoteFileTree] 文件已下载到临时目录:" << localTempPath);

    // P3-M01 子项1: 写入缓存（若 mtime 有效且文件不大）
    if (remoteMtime.isValid()) {
        QFile localFile(localTempPath);
        if (localFile.open(QIODevice::ReadOnly)) {
            QByteArray content = localFile.readAll();
            localFile.close();
            // 仅缓存 < 16MB 的文件，避免占用过多内存
            const qint64 kMaxInMemorySize = 16 * 1024 * 1024;
            if (content.size() <= kMaxInMemorySize) {
                RemoteFileCache::instance().put(remotePath, remoteMtime,
                                                content.size(), content, m_sessionName);
            }
        }
    }

    // 发出信号：主窗口可据此打开本地临时文件
    emit fileDownloaded(remotePath, localTempPath);
    emit fileOpenRequested(remotePath);
}

void RemoteFileTree::onCustomContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = itemAt(pos);
    QMenu menu(this);

    if (item) {
        QString type = item->data(0, Qt::UserRole + 1).toString();
        QString remotePath = itemPath(item);

        // 跳过占位/标记节点
        if (type == QStringLiteral("placeholder") || type == QStringLiteral("loaded")) {
            return;
        }

        // 复制路径（通用）
        QAction* actCopyPath = menu.addAction(tr("复制远程路径"));
        connect(actCopyPath, &QAction::triggered, this, [remotePath]() {
            QApplication::clipboard()->setText(remotePath);
        });

        menu.addSeparator();

        if (type == QStringLiteral("file")) {
            // 文件操作
            QAction* actOpen = menu.addAction(tr("打开"));
            connect(actOpen, &QAction::triggered, this, [this, item]() {
                onItemDoubleClicked(item, 0);
            });

            QAction* actDownload = menu.addAction(tr("下载到本地..."));
            connect(actDownload, &QAction::triggered, this, [this, item]() {
                downloadFile(item);
            });

            menu.addSeparator();

            QAction* actRename = menu.addAction(tr("重命名"));
            connect(actRename, &QAction::triggered, this, [this, item]() {
                renameItem(item);
            });

            QAction* actDelete = menu.addAction(tr("删除"));
            connect(actDelete, &QAction::triggered, this, [this, item]() {
                deleteItem(item);
            });
        } else if (type == QStringLiteral("dir")) {
            // 文件夹操作
            QAction* actUpload = menu.addAction(tr("上传文件到这里..."));
            connect(actUpload, &QAction::triggered, this, [this, item]() {
                Q_UNUSED(item)
                uploadFile();
            });

            QAction* actNewFolder = menu.addAction(tr("新建文件夹"));
            connect(actNewFolder, &QAction::triggered, this, [this, item]() {
                createFolder(item);
            });

            menu.addSeparator();

            QAction* actRename = menu.addAction(tr("重命名"));
            connect(actRename, &QAction::triggered, this, [this, item]() {
                renameItem(item);
            });

            QAction* actDelete = menu.addAction(tr("删除"));
            connect(actDelete, &QAction::triggered, this, [this, item]() {
                deleteItem(item);
            });
        }

        menu.addSeparator();
    }

    // 始终可用的操作
    QAction* actRefresh = menu.addAction(tr("刷新"));
    connect(actRefresh, &QAction::triggered, this, [this]() {
        refresh();
    });

    menu.exec(mapToGlobal(pos));
}

void RemoteFileTree::downloadFile(QTreeWidgetItem* item)
{
    if (!item || !m_sftp) return;

    QString remotePath = itemPath(item);
    if (remotePath.isEmpty()) return;

    QString localPath = QFileDialog::getSaveFileName(
        this, tr("下载文件到..."),
        QFileInfo(remotePath).fileName(),
        tr("所有文件 (*)"));

    if (localPath.isEmpty()) return;

    if (!m_sftp->download(remotePath, localPath)) {
        LOG_ERROR("[RemoteFileTree] 下载失败:" << remotePath << " → " << localPath
                  << " 错误:" << m_sftp->lastError());
        ModernDialog::warning(this, tr("下载失败"),
                              tr("无法下载文件:\n%1\n\n错误: %2").arg(remotePath, m_sftp->lastError()));
        return;
    }

    LOG_INFO("[RemoteFileTree] 下载成功:" << remotePath << " → " << localPath);
    ModernDialog::information(this, tr("下载完成"), tr("文件已下载到:\n%1").arg(localPath));
}

void RemoteFileTree::uploadFile()
{
    if (!m_sftp) return;

    // 选择本地文件
    QString localPath = QFileDialog::getOpenFileName(
        this, tr("选择要上传的文件"), QString(), tr("所有文件 (*)"));
    if (localPath.isEmpty()) return;

    // 确定上传目标目录：当前选中项所在目录
    QTreeWidgetItem* current = currentItem();
    QString targetDir;
    if (current) {
        QString type = current->data(0, Qt::UserRole + 1).toString();
        if (type == QStringLiteral("dir")) {
            targetDir = itemPath(current);
        } else if (type == QStringLiteral("file")) {
            // 上传到文件所在目录
            QString filePath = itemPath(current);
            int idx = filePath.lastIndexOf(QLatin1Char('/'));
            targetDir = (idx >= 0) ? filePath.left(idx) : m_rootPath;
        }
    }
    if (targetDir.isEmpty()) {
        targetDir = m_rootPath;
    }

    QString remotePath = targetDir + QStringLiteral("/") + QFileInfo(localPath).fileName();

    if (!m_sftp->upload(localPath, remotePath)) {
        LOG_ERROR("[RemoteFileTree] 上传失败:" << localPath << " → " << remotePath
                  << " 错误:" << m_sftp->lastError());
        ModernDialog::warning(this, tr("上传失败"),
                              tr("无法上传文件:\n%1\n\n错误: %2").arg(remotePath, m_sftp->lastError()));
        return;
    }

    LOG_INFO("[RemoteFileTree] 上传成功:" << localPath << " → " << remotePath);
    ModernDialog::information(this, tr("上传完成"), tr("文件已上传到:\n%1").arg(remotePath));

    // 刷新目标目录
    refresh();
}

void RemoteFileTree::deleteItem(QTreeWidgetItem* item)
{
    if (!item || !m_sftp) return;

    QString remotePath = itemPath(item);
    if (remotePath.isEmpty()) return;

    // 确认删除
    int ret = ModernDialog::question(this, tr("确认删除"),
                                     tr("确定要删除远程文件/文件夹吗？\n%1\n\n此操作不可撤销。").arg(remotePath));
    if (ret != ModernDialog::ROLE_ACCEPT) {
        return;
    }

    if (!m_sftp->remove(remotePath)) {
        LOG_ERROR("[RemoteFileTree] 删除失败:" << remotePath << " 错误:" << m_sftp->lastError());
        ModernDialog::warning(this, tr("删除失败"),
                              tr("无法删除:\n%1\n\n错误: %2").arg(remotePath, m_sftp->lastError()));
        return;
    }

    LOG_INFO("[RemoteFileTree] 删除成功:" << remotePath);

    // 从树中移除节点
    QTreeWidgetItem* parent = item->parent();
    if (parent) {
        parent->removeChild(item);
    } else {
        int idx = indexOfTopLevelItem(item);
        if (idx >= 0) {
            takeTopLevelItem(idx);
        }
    }
}

void RemoteFileTree::renameItem(QTreeWidgetItem* item)
{
    if (!item || !m_sftp) return;

    QString remotePath = itemPath(item);
    if (remotePath.isEmpty()) return;

    // 获取当前文件名
    int idx = remotePath.lastIndexOf(QLatin1Char('/'));
    QString currentName = (idx >= 0) ? remotePath.mid(idx + 1) : remotePath;
    QString parentDir = (idx >= 0) ? remotePath.left(idx) : QString();

    // 弹出输入对话框
    bool ok = false;
    QString newName = ModernDialog::getText(this, tr("重命名"),
                                            tr("请输入新名称:"), currentName, &ok);
    if (!ok || newName.isEmpty() || newName == currentName) {
        return;
    }

    QString newPath = parentDir.isEmpty() ? newName : (parentDir + QStringLiteral("/") + newName);

    if (!m_sftp->rename(remotePath, newPath)) {
        LOG_ERROR("[RemoteFileTree] 重命名失败:" << remotePath << " → " << newPath
                  << " 错误:" << m_sftp->lastError());
        ModernDialog::warning(this, tr("重命名失败"),
                              tr("无法重命名:\n%1 → %2\n\n错误: %3")
                                  .arg(remotePath, newPath, m_sftp->lastError()));
        return;
    }

    LOG_INFO("[RemoteFileTree] 重命名成功:" << remotePath << " → " << newPath);

    // 更新树节点显示
    QString type = item->data(0, Qt::UserRole + 1).toString();
    QString icon = (type == QStringLiteral("dir")) ? kDirIcon : kFileIcon;
    item->setText(0, icon + QStringLiteral(" ") + newName);
    item->setData(0, Qt::UserRole, newPath);
    item->setToolTip(0, newPath);
}

void RemoteFileTree::createFolder(QTreeWidgetItem* parentItem)
{
    if (!m_sftp) return;

    // 确定父目录路径
    QString parentDir;
    if (parentItem) {
        QString type = parentItem->data(0, Qt::UserRole + 1).toString();
        if (type == QStringLiteral("dir")) {
            parentDir = itemPath(parentItem);
        }
    }
    if (parentDir.isEmpty()) {
        parentDir = m_rootPath;
    }

    // 弹出输入对话框
    bool ok = false;
    QString folderName = ModernDialog::getText(this, tr("新建文件夹"),
                                               tr("请输入文件夹名称:"),
                                               QStringLiteral("new_folder"), &ok);
    if (!ok || folderName.isEmpty()) return;

    QString newPath = parentDir + QStringLiteral("/") + folderName;

    if (!m_sftp->mkdir(newPath)) {
        LOG_ERROR("[RemoteFileTree] 新建文件夹失败:" << newPath << " 错误:" << m_sftp->lastError());
        ModernDialog::warning(this, tr("新建文件夹失败"),
                              tr("无法创建文件夹:\n%1\n\n错误: %2").arg(newPath, m_sftp->lastError()));
        return;
    }

    LOG_INFO("[RemoteFileTree] 新建文件夹成功:" << newPath);

    // 刷新父目录：若已展开则重新加载
    if (parentItem) {
        // 移除已加载标记节点，使下次展开能重新加载
        if (parentItem->childCount() > 0 &&
            parentItem->child(0)->data(0, Qt::UserRole).toString() == QStringLiteral("loaded")) {
            parentItem->takeChild(0);
        }
        if (parentItem->isExpanded()) {
            // 已展开：直接重新加载
            parentItem->takeChildren();
            loadDir(itemPath(parentItem), parentItem);
            auto* loadedMarker = new QTreeWidgetItem();
            loadedMarker->setData(0, Qt::UserRole, QStringLiteral("loaded"));
            loadedMarker->setData(0, Qt::UserRole + 1, QStringLiteral("loaded"));
            loadedMarker->setHidden(true);
            parentItem->insertChild(0, loadedMarker);
        }
    } else {
        refresh();
    }
}
