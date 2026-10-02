#ifndef REMOTEFILETREE_H
#define REMOTEFILETREE_H

#include "interfaces/remote/ISftpClient.h"

#include <QTreeWidget>
#include <QMap>

class SftpClient;
class SshClient;

/// @brief 远程文件树 — SFTP 远程文件浏览器
///
/// 功能：
/// - 树形展示远程服务器目录结构
/// - 双击文件 → 发出 fileOpenRequested 信号
/// - 右键菜单：打开/下载/上传/删除/重命名/新建文件夹/刷新
/// - 异步加载（点击展开时才请求 SFTP 列目录）
/// - 断连时显示警告
class RemoteFileTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit RemoteFileTree(QWidget* parent = nullptr);
    ~RemoteFileTree() override = default;

    /// 设置 SFTP 客户端（连接成功后调用）
    void setSftpClient(SftpClient* client);

    /// 设置远程根路径（如 "/home/user"）
    void setRootPath(const QString& path);

    /// P3-M01 子项1: 设置当前会话名（用于 RemoteFileCache 隔离）
    /// 必须在 setSftpClient 之后调用，影响后续打开文件的缓存归属
    void setSessionName(const QString& sessionName) { m_sessionName = sessionName; }

    /// 刷新当前目录
    void refresh();

signals:
    /// 双击文件时发出（主窗口连接此信号打开远程文件）
    void fileOpenRequested(const QString& remotePath);
    /// 下载文件到本地临时目录后发出
    void fileDownloaded(const QString& remotePath, const QString& localTempPath);
    /// 连接断开
    void disconnected();

private slots:
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCustomContextMenuRequested(const QPoint& pos);

private:
    SftpClient* m_sftp = nullptr;
    QString m_rootPath;
    QString m_sessionName;  ///< P3-M01 子项1: 当前会话名（缓存归属）

    /// 加载指定路径的子目录到指定 tree item
    void loadDir(const QString& path, QTreeWidgetItem* parentItem);
    /// 创建文件夹节点
    QTreeWidgetItem* createDirItem(const SftpFileInfo& info, QTreeWidgetItem* parent);
    /// 创建文件节点
    QTreeWidgetItem* createFileItem(const SftpFileInfo& info, QTreeWidgetItem* parent);
    /// 获取 item 的完整远程路径
    QString itemPath(QTreeWidgetItem* item) const;
    /// 右键菜单动作
    void downloadFile(QTreeWidgetItem* item);
    void uploadFile();
    void deleteItem(QTreeWidgetItem* item);
    void renameItem(QTreeWidgetItem* item);
    void createFolder(QTreeWidgetItem* parentItem);
};

#endif // REMOTEFILETREE_H
