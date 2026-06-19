#ifndef GITMANAGER_H
#define GITMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QProcess>

/// @brief Git 版本控制管理器（轻量 git.exe 方案）
/// 通过 QProcess 调用 git 命令行工具，提供仓库状态查询和基本操作
class GitManager : public QObject
{
    Q_OBJECT

public:
    static GitManager& instance();

    /// 设置工作目录（仓库路径）
    void setWorkingDirectory(const QString& path);

    // === 仓库状态 ===
    bool isGitRepo() const;
    QString currentBranch() const;
    QStringList branches() const;           // 所有分支
    QStringList changedFiles() const;       // 修改的文件列表

    struct FileStatus {
        QString filePath;
        enum Status { Unmodified, Modified, Added, Deleted, Renamed, Untracked } status;
    };
    QList<FileStatus> fileStatuses();      // 详细文件状态

    // === 操作 ===
    bool checkoutBranch(const QString& branch);
    bool stageFile(const QString& filePath);
    bool unstageFile(const QString& filePath);
    bool commit(const QString& message);
    bool discardChanges(const QString& filePath);
    QString diff(const QString& filePath = QString());  // 返回diff文本
    QString log(int count = 10);              // 最近N条提交
    bool pull();
    bool push();

signals:
    void repoChanged();          // 仓库状态变化
    void operationStarted(const QString& op);
    void operationFinished(const QString& op, bool success, const QString& output);

private:
    GitManager();
    QString runGitCommand(const QStringList& args, int timeout = 10000);  // 执行git命令返回输出
    QString m_workingDir;
};

#endif // GITMANAGER_H
