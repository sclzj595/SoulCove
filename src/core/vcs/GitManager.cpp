#include "core/vcs/GitManager.h"
#include "Logger.hpp"

#include <QCoreApplication>
#include <QDebug>

// ========== 单例 ==========

GitManager::GitManager()
    : IGitManager(nullptr)
{
}

GitManager& GitManager::instance()
{
    static GitManager inst;
    return inst;
}

// ========== 工作目录 ==========

void GitManager::setWorkingDirectory(const QString& path)
{
    m_workingDir = path;
}

// ========== 核心命令执行 ==========

QString GitManager::runGitCommand(const QStringList& args, int timeout)
{
    QProcess process;
    process.setProgram(QStringLiteral("git"));
    process.setArguments(args);

    if (!m_workingDir.isEmpty()) {
        process.setWorkingDirectory(m_workingDir);
    }

    process.start();
    if (!process.waitForStarted()) {
        LOG_DEBUG_S("GitManager", "runGitCommand", "git 进程启动失败，请确认已安装 git 并加入 PATH");
        return QString();
    }

    if (!process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished(2000);
        LOG_DEBUG_S("GitManager", "runGitCommand", "命令超时:" << args.join(QLatin1Char(' ')));
        return QString();
    }

    QByteArray output = process.readAllStandardOutput();
    QByteArray errorOutput = process.readAllStandardError();

    if (process.exitCode() != 0 && errorOutput.isEmpty()) {
        // 某些git命令非零退出码但无错误输出时仍返回输出（如 status 有修改文件）
        return QString::fromUtf8(output).trimmed();
    }

    if (!errorOutput.isEmpty()) {
        LOG_DEBUG_S("GitManager", "runGitCommand", "stderr:" << QString::fromUtf8(errorOutput).trimmed());
    }

    return QString::fromUtf8(output).trimmed();
}

// ========== P2-H03: 指定工作目录执行命令（不修改单例状态）==========

QString GitManager::runGitCommandInDir(const QString& workDir, const QStringList& args, int timeout)
{
    QProcess process;
    process.setProgram(QStringLiteral("git"));
    process.setArguments(args);

    if (!workDir.isEmpty()) {
        process.setWorkingDirectory(workDir);
    }

    process.start();
    if (!process.waitForStarted()) {
        LOG_DEBUG_S("GitManager", "runGitCommandInDir", "git 进程启动失败");
        return QString();
    }

    if (!process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished(2000);
        LOG_DEBUG_S("GitManager", "runGitCommandInDir", "命令超时:" << args.join(QLatin1Char(' ')));
        return QString();
    }

    QByteArray output = process.readAllStandardOutput();
    QByteArray errorOutput = process.readAllStandardError();

    if (process.exitCode() != 0 && errorOutput.isEmpty()) {
        return QString::fromUtf8(output).trimmed();
    }

    if (!errorOutput.isEmpty()) {
        LOG_DEBUG_S("GitManager", "runGitCommandInDir", "stderr:" << QString::fromUtf8(errorOutput).trimmed());
    }

    return QString::fromUtf8(output).trimmed();
}

// ========== 仓库状态 ==========

bool GitManager::isGitRepo() const
{
    return isGitRepo(m_workingDir);
}

QString GitManager::currentBranch() const
{
    return currentBranch(m_workingDir);
}

QStringList GitManager::branches() const
{
    return branches(m_workingDir);
}

QStringList GitManager::changedFiles() const
{
    QList<GitFileStatus> statuses = const_cast<GitManager*>(this)->fileStatuses(m_workingDir);
    QStringList files;
    for (const GitFileStatus& fs : statuses) {
        files.append(fs.filePath);
    }
    return files;
}

QList<GitFileStatus> GitManager::fileStatuses()
{
    return fileStatuses(m_workingDir);
}

// === P2-H03: 显式 workDir 重载 ===

bool GitManager::isGitRepo(const QString& workDir) const
{
    auto result = const_cast<GitManager*>(this)->runGitCommandInDir(workDir,
        {QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")});
    return result.trimmed() == QStringLiteral("true");
}

QString GitManager::currentBranch(const QString& workDir) const
{
    auto result = const_cast<GitManager*>(this)->runGitCommandInDir(workDir,
        {QStringLiteral("branch"), QStringLiteral("--show-current")});
    if (result.isEmpty())
        return tr("(未命名分支)");
    return result;
}

QStringList GitManager::branches(const QString& workDir) const
{
    auto result = const_cast<GitManager*>(this)->runGitCommandInDir(workDir,
        {QStringLiteral("branch"), QStringLiteral("-a")});
    if (result.isEmpty())
        return {};

    QStringList list;
    for (const QString& line : result.split(QLatin1Char('\n'))) {
        QString branch = line.trimmed();
        // 去掉开头的 * 或空格
        if (branch.startsWith(QLatin1Char('*')))
            branch = branch.mid(1).trimmed();
        if (!branch.isEmpty())
            list.append(branch);
    }
    return list;
}

QList<GitFileStatus> GitManager::fileStatuses(const QString& workDir)
{
    QList<GitFileStatus> result;

    auto output = runGitCommandInDir(workDir,
        {QStringLiteral("status"), QStringLiteral("--porcelain")});
    if (output.isEmpty())
        return result;

    for (const QString& line : output.split(QLatin1Char('\n'))) {
        if (line.length() < 3) continue;

        GitFileStatus fs;
        char indexStatus = line[0].toLatin1();   // 暂存区状态
        char workStatus  = line[1].toLatin1();   // 工作区状态
        QString filePath = line.mid(3).trimmed();

        // 解析重命名/复制: R/M/C 后面跟百分比和路径
        if ((indexStatus == 'R' || indexStatus == 'C' || indexStatus == 'M') &&
            filePath.contains(QStringLiteral(" -> "))) {
            filePath = filePath.section(QStringLiteral(" -> "), -1).trimmed();
        }

        fs.filePath = filePath;

        // 判断最终状态（优先暂存区状态）
        if (workStatus == '?') {
            fs.status = GitFileStatus::Untracked;
        } else if (indexStatus == 'D' || workStatus == 'D') {
            fs.status = GitFileStatus::Deleted;
        } else if (indexStatus == 'A') {
            fs.status = GitFileStatus::Added;
        } else if (indexStatus == 'R' || indexStatus == 'C') {
            fs.status = GitFileStatus::Renamed;
        } else if (indexStatus == 'M' || workStatus == 'M') {
            fs.status = GitFileStatus::Modified;
        } else {
            fs.status = GitFileStatus::Unmodified;
        }

        result.append(fs);
    }

    return result;
}

// ========== 操作 ==========

bool GitManager::checkoutBranch(const QString& branch)
{
    emit operationStarted(tr("切换分支"));
    auto output = runGitCommand({QStringLiteral("checkout"), branch});
    bool ok = !output.contains(QStringLiteral("error"), Qt::CaseInsensitive) &&
              !output.contains(QStringLiteral("fatal"), Qt::CaseInsensitive);
    emit operationFinished(tr("切换分支"), ok, output);
    if (ok) emit repoChanged();
    return ok;
}

bool GitManager::stageFile(const QString& filePath)
{
    emit operationStarted(tr("暂存文件"));
    auto output = runGitCommand({QStringLiteral("add"), QStringLiteral("--"), filePath});
    bool ok = !output.contains(QStringLiteral("error"), Qt::CaseInsensitive) &&
              !output.contains(QStringLiteral("fatal"), Qt::CaseInsensitive);
    emit operationFinished(tr("暂存文件"), ok, output);
    if (ok) emit repoChanged();
    return ok;
}

bool GitManager::unstageFile(const QString& filePath)
{
    emit operationStarted(tr("取消暂存"));
    auto output = runGitCommand({QStringLiteral("reset"), QStringLiteral("HEAD"), QStringLiteral("--"), filePath});
    bool ok = !output.contains(QStringLiteral("error"), Qt::CaseInsensitive) &&
              !output.contains(QStringLiteral("fatal"), Qt::CaseInsensitive);
    emit operationFinished(tr("取消暂存"), ok, output);
    if (ok) emit repoChanged();
    return ok;
}

bool GitManager::commit(const QString& message)
{
    emit operationStarted(tr("提交"));
    auto output = runGitCommand({QStringLiteral("commit"), QStringLiteral("-m"), message});
    bool ok = !output.contains(QStringLiteral("error"), Qt::CaseInsensitive) &&
              !output.contains(QStringLiteral("fatal"), Qt::CaseInsensitive);
    emit operationFinished(tr("提交"), ok, output);
    if (ok) emit repoChanged();
    return ok;
}

bool GitManager::discardChanges(const QString& filePath)
{
    emit operationStarted(tr("放弃更改"));
    auto output = runGitCommand({QStringLiteral("checkout"), QStringLiteral("--"), filePath});
    bool ok = !output.contains(QStringLiteral("error"), Qt::CaseInsensitive) &&
              !output.contains(QStringLiteral("fatal"), Qt::CaseInsensitive);
    emit operationFinished(tr("放弃更改"), ok, output);
    if (ok) emit repoChanged();
    return ok;
}

QString GitManager::diff(const QString& filePath)
{
    QStringList args{QStringLiteral("diff")};
    if (!filePath.isEmpty()) {
        args.append(QStringLiteral("--"));
        args.append(filePath);
    }
    auto output = runGitCommand(args);
    if (output.isEmpty()) {
        // 尝试 --cached（已暂存的更改）
        args.clear();
        args << QStringLiteral("diff") << QStringLiteral("--cached");
        if (!filePath.isEmpty()) {
            args << QStringLiteral("--") << filePath;
        }
        output = runGitCommand(args);
    }
    return output;
}

QString GitManager::log(int count)
{
    auto output = runGitCommand({
        QStringLiteral("log"),
        QStringLiteral("-n") + QString::number(count),
        QStringLiteral("--oneline"),
        QStringLiteral("--decorate")
    });
    return output;
}

bool GitManager::pull()
{
    emit operationStarted(tr("拉取"));
    auto output = runGitCommand({QStringLiteral("pull")}, 30000);
    bool ok = !output.contains(QStringLiteral("error"), Qt::CaseInsensitive) &&
              !output.contains(QStringLiteral("fatal"), Qt::CaseInsensitive) &&
              !output.contains(QStringLiteral("conflict"), Qt::CaseInsensitive);
    emit operationFinished(tr("拉取"), ok, output);
    if (ok) emit repoChanged();
    return ok;
}

bool GitManager::push()
{
    emit operationStarted(tr("推送"));
    auto output = runGitCommand({QStringLiteral("push")}, 30000);
    bool ok = !output.contains(QStringLiteral("error"), Qt::CaseInsensitive) &&
              !output.contains(QStringLiteral("fatal"), Qt::CaseInsensitive);
    emit operationFinished(tr("推送"), ok, output);
    if (ok) emit repoChanged();
    return ok;
}

// ========== P2-H03 子项2: 提交历史 ==========

QString GitManager::commitDiff(const QString& workDir, const QString& commitHash)
{
    if (commitHash.isEmpty())
        return QString();

    // git show <hash> — 显示提交的元信息 + diff
    return runGitCommandInDir(workDir,
        {QStringLiteral("show"), commitHash}, 15000);
}

QList<GitCommit> GitManager::logDetailed(const QString& workDir, int limit)
{
    QList<GitCommit> result;

    // 使用唯一分隔符避免与提交消息中的字符冲突
    // 字段顺序: hash | author | email | timestamp(unix) | message
    const QString sep = QStringLiteral("\x1f");  // ASCII Unit Separator
    const QString fmt = QStringLiteral("%H") + sep +
                        QStringLiteral("%an") + sep +
                        QStringLiteral("%ae") + sep +
                        QStringLiteral("%at") + sep +
                        QStringLiteral("%B") + QStringLiteral("\x1e");  // ASCII Record Separator 作为提交结束符

    auto output = runGitCommandInDir(workDir, {
        QStringLiteral("log"),
        QStringLiteral("-n") + QString::number(limit),
        QStringLiteral("--pretty=format:") + fmt
    });

    if (output.isEmpty())
        return result;

    const QStringList records = output.split(QStringLiteral("\x1e"), Qt::SkipEmptyParts);
    for (const QString& record : records) {
        const QStringList fields = record.split(sep);
        if (fields.size() < 5) continue;

        GitCommit commit;
        commit.hash    = fields[0].trimmed();
        commit.author  = fields[1];
        commit.email   = fields[2];
        bool tsOk = false;
        qint64 ts = fields[3].toLongLong(&tsOk);
        if (tsOk) {
            commit.time = QDateTime::fromSecsSinceEpoch(ts);
        }
        // 提交消息保留原始换行，去除首尾空白
        commit.message = fields[4].trimmed();

        if (!commit.hash.isEmpty())
            result.append(commit);
    }

    return result;
}
