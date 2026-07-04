#ifndef GITMANAGER_H
#define GITMANAGER_H

#include "interfaces/vcs/IGitManager.h"

#include <QProcess>
#include <QDateTime>

/// @brief Git 提交信息（结构化，供历史面板使用）
struct GitCommit {
    QString   hash;     // 完整提交哈希
    QString   author;   // 作者名
    QString   email;    // 作者邮箱
    QDateTime time;     // 提交时间
    QString   message;  // 提交消息（完整，含换行）
};

/// @brief Git 版本控制管理器（轻量 git.exe 方案）
/// 通过 QProcess 调用 git 命令行工具，提供仓库状态查询和基本操作
/// 实现 IGitManager 接口（依赖倒置原则）
class GitManager : public IGitManager
{
    Q_OBJECT

public:
    static GitManager& instance();

    /// 设置工作目录（仓库路径）
    void setWorkingDirectory(const QString& path) override;

    // === 仓库状态 ===
    bool isGitRepo() const override;
    QString currentBranch() const override;
    QStringList branches() const override;
    QStringList changedFiles() const override;
    QList<GitFileStatus> fileStatuses() override;

    // === P2-H03 子项1/2: 显式指定工作目录的重载（不影响单例 m_workingDir 状态）===
    QString currentBranch(const QString& workDir) const;
    QList<GitFileStatus> fileStatuses(const QString& workDir);
    QStringList branches(const QString& workDir) const;
    bool isGitRepo(const QString& workDir) const;

    // === 操作 ===
    bool checkoutBranch(const QString& branch) override;
    bool stageFile(const QString& filePath) override;
    bool unstageFile(const QString& filePath) override;
    bool commit(const QString& message) override;
    bool discardChanges(const QString& filePath) override;
    QString diff(const QString& filePath = QString()) override;
    QString log(int count = 10) override;
    bool pull() override;
    bool push() override;

    // === P2-H03 子项2: 提交历史 ===
    /// 获取指定提交的 diff（git show <hash>）
    /// @param workDir 工作目录
    /// @param commitHash 提交哈希
    /// @return diff 文本
    QString commitDiff(const QString& workDir, const QString& commitHash);

    /// 获取结构化提交历史（git log --pretty=format）
    /// @param workDir 工作目录
    /// @param limit 最大条数（默认 100）
    /// @return GitCommit 列表（按时间倒序）
    QList<GitCommit> logDetailed(const QString& workDir, int limit = 100);

private:
    GitManager();
    QString runGitCommand(const QStringList& args, int timeout = 10000);  // 执行git命令返回输出
    /// P2-H03: 在指定工作目录下执行 git 命令（不修改 m_workingDir 状态）
    QString runGitCommandInDir(const QString& workDir, const QStringList& args, int timeout = 10000);
    QString m_workingDir;
};

#endif // GITMANAGER_H
