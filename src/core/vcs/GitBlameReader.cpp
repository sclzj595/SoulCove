#include "core/vcs/GitBlameReader.h"
#include "Logger.hpp"

#include <QProcess>
#include <QFileInfo>

// ========== P2-H03 子项3: Git blame 解析 ==========

QList<GitBlameLine> GitBlameReader::blame(const QString& filePath)
{
    QList<GitBlameLine> result;

    if (filePath.isEmpty())
        return result;

    QFileInfo fi(filePath);
    if (!fi.exists() || fi.isDir())
        return result;

    // 工作目录设为文件所在目录（git blame 需要在仓库内执行）
    QString workDir = fi.absolutePath();

    QProcess process;
    process.setProgram(QStringLiteral("git"));
    // --line-porcelain: 每行输出完整 header，便于解析
    // --: 显式分隔参数与文件路径（路径含空格/特殊字符也安全）
    process.setArguments({QStringLiteral("blame"),
                          QStringLiteral("--line-porcelain"),
                          QStringLiteral("--"),
                          fi.absoluteFilePath()});
    process.setWorkingDirectory(workDir);

    process.start();
    if (!process.waitForStarted(3000)) {
        LOG_DEBUG_S("GitBlameReader", "blame", "git 进程启动失败");
        return result;
    }

    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        LOG_DEBUG_S("GitBlameReader", "blame", "git blame 超时: " << filePath.toStdString());
        return result;
    }

    if (process.exitCode() != 0) {
        // 非仓库文件 / 未跟踪文件 → 静默返回空（正常场景，不打日志）
        return result;
    }

    QString output = QString::fromUtf8(process.readAllStandardOutput());
    if (output.isEmpty())
        return result;

    // === 解析 --line-porcelain 输出 ===
    // 每条记录结构:
    //   <40-char-sha> <orig-line> <final-line>
    //   author <name>
    //   author-mail <email>
    //   author-time <unix-ts>
    //   author-tz <tz>
    //   ... (committer-* / summary / filename 等，本场景不需要)
    //   \t<line content>
    const QStringList lines = output.split(QLatin1Char('\n'));

    QString curHash;
    QString curAuthor;
    qint64  curTime = 0;
    int     curFinalLine = 0;

    for (const QString& line : lines) {
        // 内容行以 TAB 开头 → 当前记录结束
        if (line.startsWith(QLatin1Char('\t'))) {
            if (!curHash.isEmpty() && curFinalLine > 0) {
                GitBlameLine bl;
                bl.lineNumber  = curFinalLine;
                bl.commitHash  = curHash;
                bl.author      = curAuthor;
                bl.time        = (curTime > 0)
                                 ? QDateTime::fromSecsSinceEpoch(curTime)
                                 : QDateTime();
                result.append(bl);
            }
            // 重置状态（下一行会先出现新的 header）
            curHash.clear();
            curAuthor.clear();
            curTime = 0;
            curFinalLine = 0;
            continue;
        }

        // header 行: <sha> <orig-line> <final-line> (无前缀，以十六进制字符开头)
        if (line.length() >= 40 && line[0].isLetterOrNumber()) {
            // 验证像 SHA (40 个十六进制字符)
            bool isHash = true;
            for (int i = 0; i < 40; ++i) {
                const QChar c = line[i];
                if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                      (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
                      (c >= QLatin1Char('A') && c <= QLatin1Char('F')))) {
                    isHash = false;
                    break;
                }
            }
            if (isHash) {
                // 格式: <40-sha> <orig> <final>
                QStringList parts = line.mid(40).trimmed().split(QLatin1Char(' '),
                                                                 Qt::SkipEmptyParts);
                curHash = line.left(40);
                curFinalLine = (parts.size() >= 2) ? parts[1].toInt() : 0;
                continue;
            }
        }

        // key-value 行
        if (line.startsWith(QStringLiteral("author "))) {
            curAuthor = line.mid(7);
        } else if (line.startsWith(QStringLiteral("author-mail "))) {
            // author-mail 含 <email>，不覆盖 author 名字
        } else if (line.startsWith(QStringLiteral("author-time "))) {
            curTime = line.mid(12).trimmed().toLongLong();
        }
        // 其他字段（committer-*/summary/filename/filename）忽略
    }

    // 文件末尾若无 \t 行（理论上不会发生），已由循环内逐条处理
    return result;
}
