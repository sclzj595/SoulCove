#ifndef DIFFVIEWER_H
#define DIFFVIEWER_H

#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QList>
#include <QTextEdit>
#include <QFile>
#include <QFileInfo>

/// @brief Diff 视图（并排对比）
/// 使用 LCS (Longest Common Subsequence) 算法计算逐行差异
/// 并排显示两个版本的文本，高亮增删改行
class DiffViewer : public QWidget
{
    Q_OBJECT

public:
    explicit DiffViewer(QWidget* parent = nullptr);

    /// 设置对比内容
    void setDiffContent(const QString& originalText, const QString& modifiedText,
                        const QString& originalLabel = QStringLiteral("Original"),
                        const QString& modifiedLabel = QStringLiteral("Modified"));

    /// 从文件加载对比
    void loadFileDiff(const QString& originalPath, const QString& modifiedPath);

    /// 统计信息：新增行数
    int addedLines() const { return m_addedLines; }
    /// 统计信息：删除行数
    int removedLines() const { return m_removedLines; }
    /// 统计信息：修改行数
    int modifiedLines() const { return m_modifiedLines; }

signals:
    void diffClosed();

private:
    /// 差异行数据结构
    struct DiffLine {
        enum Type { Unchanged, Added, Removed, Modified };
        Type type;
        int originalLineNo;   // 原始文件行号 (-1表示无对应行)
        int modifiedLineNo;   // 修改后文件行号
        QString originalText;
        QString modifiedText;
    };

    /// 使用 LCS 算法计算差异
    QList<DiffLine> computeDiff(const QStringList& origLines, const QStringList& modLines);

    /// 构建 UI
    void setupUI();

    // === UI 组件 ===
    QLabel*       m_statsLabel;      // 统计信息标签
    QScrollArea*  m_leftArea;        // 左侧原始版本
    QScrollArea*  m_rightArea;       // 右侧修改版本
    QTextEdit*    m_leftEdit;        // 左侧文本显示（只读）
    QTextEdit*    m_rightEdit;       // 右侧文本显示（只读）

    // === 统计数据 ===
    int m_addedLines = 0;
    int m_removedLines = 0;
    int m_modifiedLines = 0;

    /// 颜色常量
    static constexpr const char* COLOR_ADDED     = "#294d24";   // 新增行: 绿色背景
    static constexpr const char* COLOR_REMOVED   = "#5a1d1d";   // 删除行: 红色背景
    static constexpr const char* COLOR_MODIFIED  = "#3a3a00";   // 修改行: 黄色背景
};

#endif // DIFFVIEWER_H
