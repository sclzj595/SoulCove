#include "core/vcs/MergeConflictResolver.h"

#include <QRegularExpression>

QList<MergeConflictResolver::ConflictRegion> MergeConflictResolver::detectConflicts(const QString& content)
{
    QList<ConflictRegion> regions;
    QStringList lines = content.split(QLatin1Char('\n'));

    int i = 0;
    while (i < lines.size()) {
        // 查找冲突开始标记 <<<<<<<
        if (lines[i].startsWith(QStringLiteral("<<<<<<<"))) {
            ConflictRegion region;
            region.startLine = i;

            // 查找分隔标记 =======
            int sep = -1;
            for (int j = i + 1; j < lines.size(); ++j) {
                if (lines[j].trimmed() == QStringLiteral("=======")) {
                    sep = j;
                    break;
                }
                // 如果遇到新的 <<<<<<<，说明冲突标记不完整，跳过
                if (lines[j].startsWith(QStringLiteral("<<<<<<<"))) {
                    break;
                }
            }

            if (sep < 0) {
                // 不完整的冲突标记，跳过
                ++i;
                continue;
            }

            region.separatorLine = sep;

            // 查找冲突结束标记 >>>>>>>
            int end = -1;
            for (int j = sep + 1; j < lines.size(); ++j) {
                if (lines[j].startsWith(QStringLiteral(">>>>>>>"))) {
                    end = j;
                    break;
                }
            }

            if (end < 0) {
                // 不完整的冲突标记，跳过
                ++i;
                continue;
            }

            region.endLine = end;

            // 提取 ours 内容（startLine+1 到 separatorLine-1）
            QStringList oursLines;
            for (int j = region.startLine + 1; j < region.separatorLine; ++j) {
                oursLines.append(lines[j]);
            }
            region.oursText = oursLines.join(QLatin1Char('\n'));

            // 提取 theirs 内容（separatorLine+1 到 endLine-1）
            QStringList theirsLines;
            for (int j = region.separatorLine + 1; j < region.endLine; ++j) {
                theirsLines.append(lines[j]);
            }
            region.theirsText = theirsLines.join(QLatin1Char('\n'));

            // 提取分支名（>>>>>>> 后的部分）
            QString endLine = lines[end];
            region.branchName = endLine.mid(7).trimmed();  // 跳过 ">>>>>>>"

            regions.append(region);
            i = end + 1;
        } else {
            ++i;
        }
    }

    return regions;
}

QString MergeConflictResolver::resolveConflict(const ConflictRegion& region, Resolution choice)
{
    switch (choice) {
    case Resolution::AcceptOurs:
        return region.oursText;
    case Resolution::AcceptTheirs:
        return region.theirsText;
    case Resolution::AcceptBoth:
        if (region.oursText.isEmpty()) return region.theirsText;
        if (region.theirsText.isEmpty()) return region.oursText;
        return region.oursText + QStringLiteral("\n") + region.theirsText;
    case Resolution::AcceptBothReversed:
        if (region.oursText.isEmpty()) return region.theirsText;
        if (region.theirsText.isEmpty()) return region.oursText;
        return region.theirsText + QStringLiteral("\n") + region.oursText;
    }
    return region.oursText;
}

QString MergeConflictResolver::resolveAllConflicts(const QString& content, Resolution choice)
{
    QList<ConflictRegion> conflicts = detectConflicts(content);
    if (conflicts.isEmpty()) return content;

    QStringList lines = content.split(QLatin1Char('\n'));
    QStringList result;

    int lastEnd = 0;
    for (const auto& region : conflicts) {
        // 添加冲突前的正常内容
        for (int i = lastEnd; i < region.startLine; ++i) {
            result.append(lines[i]);
        }

        // 添加解决后的内容
        QString resolved = resolveConflict(region, choice);
        if (!resolved.isEmpty()) {
            QStringList resolvedLines = resolved.split(QLatin1Char('\n'));
            for (const QString& rl : resolvedLines) {
                result.append(rl);
            }
        }

        lastEnd = region.endLine + 1;
    }

    // 添加最后一段正常内容
    for (int i = lastEnd; i < lines.size(); ++i) {
        result.append(lines[i]);
    }

    return result.join(QLatin1Char('\n'));
}

bool MergeConflictResolver::hasConflicts(const QString& content)
{
    return content.contains(QStringLiteral("<<<<<<<")) &&
           content.contains(QStringLiteral("=======")) &&
           content.contains(QStringLiteral(">>>>>>>"));
}

QString MergeConflictResolver::resolutionName(Resolution choice)
{
    switch (choice) {
    case Resolution::AcceptOurs:         return QStringLiteral("接受当前 (Ours)");
    case Resolution::AcceptTheirs:       return QStringLiteral("接受传入 (Theirs)");
    case Resolution::AcceptBoth:         return QStringLiteral("接受两者 (Ours + Theirs)");
    case Resolution::AcceptBothReversed: return QStringLiteral("接受两者 (Theirs + Ours)");
    }
    return QStringLiteral("未知");
}
