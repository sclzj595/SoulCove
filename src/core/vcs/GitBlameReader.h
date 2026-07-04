#ifndef GITBLAMEREADER_H
#define GITBLAMEREADER_H

#include <QString>
#include <QList>
#include <QDateTime>

/// @brief 单行 Git blame 信息（P2-H03 子项3）
struct GitBlameLine {
    int       lineNumber;    // 行号（1-based）
    QString   commitHash;    // 该行所属提交哈希
    QString   author;        // 该行最后修改者
    QDateTime time;          // 该行最后修改时间
};

/// @brief Git blame 解析器（P2-H03 子项3）
///
/// 通过 `git blame --line-porcelain <file>` 解析每行的提交归属，
/// 供编辑器行号栏绘制彩色标注条（同一提交同色，不同提交切换颜色）。
///
/// 设计为静态工具类，无状态，可在 QtConcurrent 工作线程中安全调用。
class GitBlameReader
{
public:
    /// 解析指定文件的 blame 信息
    /// @param filePath 文件绝对路径
    /// @return GitBlameLine 列表（按行号升序，lineNumber 1-based）；非 git 文件或失败返回空列表
    static QList<GitBlameLine> blame(const QString& filePath);

private:
    GitBlameReader() = delete;  // 纯静态工具类，禁止实例化
};

#endif // GITBLAMEREADER_H
