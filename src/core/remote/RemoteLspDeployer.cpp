#include "core/remote/RemoteLspDeployer.h"
#include "core/remote/SshClient.h"
#include "core/remote/SftpClient.h"
#include "Logger.hpp"

#include <QFileInfo>
#include <QStandardPaths>
#include <QProcessEnvironment>
#include <QDir>

// ============================================================
// 构造
// ============================================================

RemoteLspDeployer::RemoteLspDeployer(QObject* parent)
    : QObject(parent)
{
}

// ============================================================
// 私有辅助
// ============================================================

QString RemoteLspDeployer::findLocalClangd() const
{
    // 1. 通过 PATH 搜索
    QStringList candidateNames{ QStringLiteral("clangd"), QStringLiteral("clangd.exe") };
    for (const QString& name : candidateNames) {
        QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty() && QFileInfo(found).isExecutable()) {
            return found;
        }
    }

    // 2. 搜索常见安装路径（Windows / Unix）
#ifdef Q_OS_WIN
    QStringList commonDirs;
    commonDirs << QStringLiteral("C:/Program Files/LLVM/bin")
               << QStringLiteral("C:/Program Files (x86)/LLVM/bin")
               << QStringLiteral("C:/Program Files/clangd/bin")
               << QStringLiteral("C:/llvm/bin");
    QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    if (!localAppData.isEmpty()) {
        commonDirs << (localAppData + QStringLiteral("/Programs/clangd/bin"));
    }
    for (const QString& dir : commonDirs) {
        for (const QString& name : candidateNames) {
            QString candidate = dir + QStringLiteral("/") + name;
            if (QFileInfo::exists(candidate)) {
                return candidate;
            }
        }
    }
#else
    QStringList commonDirs;
    commonDirs << QStringLiteral("/usr/bin") << QStringLiteral("/usr/local/bin")
               << QStringLiteral("/opt/homebrew/bin") << QStringLiteral("/usr/local/opt/llvm/bin");
    for (const QString& dir : commonDirs) {
        for (const QString& name : candidateNames) {
            QString candidate = dir + QStringLiteral("/") + name;
            if (QFileInfo(candidate).isExecutable()) {
                return candidate;
            }
        }
    }
#endif
    return QString();
}

QString RemoteLspDeployer::defaultRemoteDir(SshClient& ssh)
{
    // 获取远程 HOME 目录
    QString home = ssh.executeCommand(QStringLiteral("echo $HOME"), 3000).trimmed();
    if (home.isEmpty()) {
        home = QStringLiteral("/tmp");  // 兜底
    }
    return home + QStringLiteral("/.local/share/scnb");
}

bool RemoteLspDeployer::ensureRemoteDir(SshClient& ssh, const QString& dir)
{
    // mkdir -p 创建目录
    QString cmd = QStringLiteral("mkdir -p '%1' && echo SCNB_MKDIR_OK").arg(dir);
    QString out = ssh.executeCommand(cmd, 5000);
    return out.contains(QStringLiteral("SCNB_MKDIR_OK"));
}

// ============================================================
// 公开 API
// ============================================================

QString RemoteLspDeployer::checkClangdInstalled(SshClient& ssh)
{
    if (!ssh.isConnected()) return QString();

    // 优先 which clangd，回退 command -v clangd
    QString out = ssh.executeCommand(QStringLiteral("which clangd 2>/dev/null"), 3000).trimmed();
    if (out.isEmpty() || out.contains(QStringLiteral("no clangd"), Qt::CaseInsensitive)) {
        out = ssh.executeCommand(QStringLiteral("command -v clangd 2>/dev/null"), 3000).trimmed();
    }
    if (!out.isEmpty() && !out.contains(QStringLiteral("not found"), Qt::CaseInsensitive)) {
        // 校验路径可执行
        QString check = ssh.executeCommand(
            QStringLiteral("test -x '%1' && echo SCNB_EXEC_OK").arg(out), 3000);
        if (check.contains(QStringLiteral("SCNB_EXEC_OK"))) {
            LOG_INFO("[RemoteLspDeployer] 检测到远程 clangd: " << out.toStdString());
            return out;
        }
    }
    return QString();
}

bool RemoteLspDeployer::uploadClangdBinary(SshClient& ssh, SftpClient& sftp,
                                            const QString& remotePath)
{
    // 1. 查找本地 clangd 二进制
    QString localPath = findLocalClangd();
    if (localPath.isEmpty()) {
        LOG_WARN("[RemoteLspDeployer] 本地未找到 clangd 二进制，无法上传");
        return false;
    }
    LOG_INFO("[RemoteLspDeployer] 本地 clangd 路径: " << localPath.toStdString());

    // 2. SFTP 上传（upload 非 const：涉及 libssh2 阻塞模式切换）
    if (!sftp.upload(localPath, remotePath)) {
        LOG_ERROR("[RemoteLspDeployer] 上传失败: " << localPath.toStdString()
                  << " → " << remotePath.toStdString()
                  << " err=" << sftp.lastError().toStdString());
        return false;
    }

    // 3. 通过 SSH 设置可执行权限
    ssh.executeCommand(QStringLiteral("chmod +x '%1'").arg(remotePath), 3000);

    LOG_INFO("[RemoteLspDeployer] 上传成功: " << remotePath.toStdString());
    return true;
}

QString RemoteLspDeployer::remoteClangdPath(SshClient& ssh)
{
    // 优先检测系统安装，回退到 ~/.local/share/scnb/clangd
    QString path = checkClangdInstalled(ssh);
    if (!path.isEmpty()) return path;

    // 检测用户目录下的部署版本
    QString home = ssh.executeCommand(QStringLiteral("echo $HOME"), 3000).trimmed();
    if (!home.isEmpty()) {
        QString userPath = home + QStringLiteral("/.local/share/scnb/clangd");
        QString check = ssh.executeCommand(
            QStringLiteral("test -x '%1' && echo SCNB_EXEC_OK").arg(userPath), 3000);
        if (check.contains(QStringLiteral("SCNB_EXEC_OK"))) {
            return userPath;
        }
    }
    return QString();
}

QString RemoteLspDeployer::deploy(SshClient& ssh, SftpClient& sftp, QString& errMessage)
{
    if (!ssh.isConnected()) {
        errMessage = QStringLiteral("SSH 未连接");
        return QString();
    }

    // 1. 检测远程是否已安装
    QString existing = checkClangdInstalled(ssh);
    if (!existing.isEmpty()) {
        LOG_INFO("[RemoteLspDeployer] 远程已安装 clangd: " << existing.toStdString());
        return existing;
    }

    // 2. 准备远程目录
    QString remoteDir = defaultRemoteDir(ssh);
    if (!ensureRemoteDir(ssh, remoteDir)) {
        errMessage = QStringLiteral("无法创建远程目录: %1").arg(remoteDir);
        return QString();
    }
    QString remotePath = remoteDir + QStringLiteral("/clangd");

    // 3. 上传二进制
    if (!uploadClangdBinary(ssh, sftp, remotePath)) {
        errMessage = QStringLiteral("上传 clangd 二进制失败");
        return QString();
    }

    // 4. 验证可执行
    QString verify = ssh.executeCommand(
        QStringLiteral("'%1' --version 2>&1 | head -n 1").arg(remotePath), 5000);
    if (!verify.contains(QStringLiteral("clangd"), Qt::CaseInsensitive)) {
        errMessage = QStringLiteral("远程 clangd 启动失败: %1").arg(verify);
        return QString();
    }

    LOG_INFO("[RemoteLspDeployer] 部署成功: " << remotePath.toStdString());
    return remotePath;
}
