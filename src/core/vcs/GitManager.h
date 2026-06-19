#ifndef GITMANAGER_H
#define GITMANAGER_H

#include "interfaces/vcs/IGitManager.h"

#include <QProcess>

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

private:
    GitManager();
    QString runGitCommand(const QStringList& args, int timeout = 10000);  // 执行git命令返回输出
    QString m_workingDir;
};

#endif // GITMANAGER_H
