#ifndef MERGECONFLICTRESOLVER_H
#define MERGECONFLICTRESOLVER_H

#include <QString>
#include <QList>

/// @brief V1.9: Git 合并冲突解决器
/// 检测文件中的 Git 冲突标记（<<<<<<< / ======= / >>>>>>>），
/// 提供接受当前/传入/两者等解决方案
class MergeConflictResolver
{
public:
    /// @brief 冲突区域
    struct ConflictRegion {
        int startLine;      // <<<<<<< 标记所在行（0-based）
        int separatorLine;  // ======= 标记所在行（0-based）
        int endLine;        // >>>>>>> 标记所在行（0-based）
        QString oursText;   // 当前分支（HEAD）的内容
        QString theirsText; // 传入分支的内容
        QString branchName; // >>>>>>> 后的分支名
    };

    /// @brief 解决方案
    enum class Resolution {
        AcceptOurs,     // 接受当前（保留 HEAD 的代码）
        AcceptTheirs,   // 接受传入（保留其他分支的代码）
        AcceptBoth,     // 接受两者（先 ours 后 theirs）
        AcceptBothReversed  // 接受两者（先 theirs 后 ours）
    };

    /// @brief 检测文本中的所有冲突区域
    /// @param content 文件内容
    /// @return 冲突区域列表（空列表表示无冲突）
    static QList<ConflictRegion> detectConflicts(const QString& content);

    /// @brief 解决单个冲突区域
    /// @param region 冲突区域
    /// @param choice 解决方案
    /// @return 解决后的文本（替换冲突区域的内容）
    static QString resolveConflict(const ConflictRegion& region, Resolution choice);

    /// @brief 解决文件中的所有冲突
    /// @param content 文件内容
    /// @param choice 解决方案（对所有冲突应用相同方案）
    /// @return 解决后的完整文件内容
    static QString resolveAllConflicts(const QString& content, Resolution choice);

    /// @brief 检查文件内容是否包含冲突标记
    static bool hasConflicts(const QString& content);

    /// @brief 获取解决方案的显示名称
    static QString resolutionName(Resolution choice);
};

#endif // MERGECONFLICTRESOLVER_H
