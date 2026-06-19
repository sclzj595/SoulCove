#ifndef IGITMANAGER_H
#define IGITMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

/// @brief Git 文件状态信息
struct GitFileStatus {
    QString filePath;
    enum Status { Unmodified, Modified, Added, Deleted, Renamed, Untracked } status;
};

/// @brief Git 版本控制管理器抽象接口
/// 上层代码只依赖此接口，不依赖 GitManager 具体实现
/// 设计模式：Strategy（可替换为 libgit2 / git.exe / 其他 VCS 后端）
class IGitManager : public QObject
{
    Q_OBJECT

public:
    explicit IGitManager(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IGitManager() = default;

    /// 设置工作目录（仓库路径）
    virtual void setWorkingDirectory(const QString& path) = 0;

    // === 仓库状态 ===
    virtual bool isGitRepo() const = 0;
    virtual QString currentBranch() const = 0;
    virtual QStringList branches() const = 0;
    virtual QStringList changedFiles() const = 0;
    virtual QList<GitFileStatus> fileStatuses() = 0;

    // === 操作 ===
    virtual bool checkoutBranch(const QString& branch) = 0;
    virtual bool stageFile(const QString& filePath) = 0;
    virtual bool unstageFile(const QString& filePath) = 0;
    virtual bool commit(const QString& message) = 0;
    virtual bool discardChanges(const QString& filePath) = 0;
    virtual QString diff(const QString& filePath = QString()) = 0;
    virtual QString log(int count = 10) = 0;
    virtual bool pull() = 0;
    virtual bool push() = 0;

signals:
    void repoChanged();
    void operationStarted(const QString& op);
    void operationFinished(const QString& op, bool success, const QString& output);
};

#endif // IGITMANAGER_H
