#ifndef REMOTELSPDEPLOYER_H
#define REMOTELSPDEPLOYER_H

#include <QObject>
#include <QString>

class SshClient;
class SftpClient;

/// @brief 远程 LSP 自动部署器
///
/// 职责：
/// - 检测远程主机是否已安装 clangd
/// - 上传本地 clangd 二进制到远程（若未安装且用户同意）
/// - 返回远程 clangd 路径，供 LspManager 在远程模式下使用
///
/// 通信方式说明：
///   远程模式下 LSP 通过 SSH 通道与远程 clangd 通信。
///   实现 A：SSH 端口转发（forwarding），客户端通过本地端口与远程 stdio clangd 通信
///   实现 B：通过 SSH Shell 通道直接 stdio over SSH（本文档采用此方案）
///
/// 部署路径约定：
///   远程默认安装目录：~/.local/share/scnb/clangd
///   本地 clangd 搜索路径：与 LspManager::autoDetectServer("cpp") 一致
class RemoteLspDeployer : public QObject
{
    Q_OBJECT

public:
    explicit RemoteLspDeployer(QObject* parent = nullptr);

    /// @brief 检测远程主机是否已安装 clangd
    /// @param ssh 已连接的 SSH 客户端（非 const：executeCommand 会管理 SSH 通道）
    /// @return 远程 clangd 路径（如 /usr/bin/clangd），未安装返回空字符串
    QString checkClangdInstalled(SshClient& ssh);

    /// @brief 上传本地 clangd 二进制到远程
    /// @param ssh 已连接的 SSH 客户端
    /// @param sftp 已初始化的 SFTP 客户端（非 const：upload 会切换阻塞模式）
    /// @param remotePath 远程目标路径（如 ~/.local/share/scnb/clangd）
    /// @return 上传是否成功
    bool uploadClangdBinary(SshClient& ssh, SftpClient& sftp,
                            const QString& remotePath);

    /// @brief 返回远程 clangd 路径（若已检测到）
    /// @param ssh 已连接的 SSH 客户端
    /// @return 远程 clangd 路径，未检测到返回空
    QString remoteClangdPath(SshClient& ssh);

    /// @brief 一键部署：检测 → 上传（若需要）→ 返回远程路径
    /// @param ssh 已连接的 SSH 客户端
    /// @param sftp 已初始化的 SFTP 客户端
    /// @param[out] errMessage 失败时填入错误信息
    /// @return 远程 clangd 路径，失败返回空字符串
    QString deploy(SshClient& ssh, SftpClient& sftp, QString& errMessage);

private:
    /// 查找本地 clangd 二进制路径（复用 LspManager 的检测逻辑）
    QString findLocalClangd() const;

    /// 远程默认安装目录（~/.local/share/scnb/）
    QString defaultRemoteDir(SshClient& ssh);

    /// 在远程创建目录（mkdir -p）
    bool ensureRemoteDir(SshClient& ssh, const QString& dir);
};

#endif // REMOTELSPDEPLOYER_H
