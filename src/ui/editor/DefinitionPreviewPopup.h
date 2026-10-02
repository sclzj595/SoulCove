#ifndef DEFINITIONPREVIEWPOPUP_H
#define DEFINITIONPREVIEWPOPUP_H

#include <QFrame>
#include <QString>
#include <QTimer>

class QTextEdit;
class QLabel;

/// @file DefinitionPreviewPopup.h
/// @brief C03-6: 定义预览弹窗 — Ctrl+Alt+Click 悬浮预览定义代码片段
///
/// 设计要点：
/// - 继承 QFrame，始终作为独立 ToolTip 顶级窗口弹出（参考 HoverPopup 实现）
///   （任务规格「无父窗口时为 Qt::ToolTip | Qt::FramelessWindowHint」的最小行为已满足；
///    实际实现始终设置 ToolTip 以保证 move(globalPos) 使用全局坐标）
/// - 顶部小标题栏显示文件名 + 行号
/// - 内部 QTextEdit（只读）显示定义代码片段
/// - 5 秒自动隐藏（QTimer），鼠标进入取消计时器，离开重启
/// - Esc 关闭
class DefinitionPreviewPopup : public QFrame
{
    Q_OBJECT

public:
    /// @brief 构造预览弹窗
    /// @param parent 父窗口（仅用于内存管理，弹窗仍为 ToolTip 顶级窗口）
    explicit DefinitionPreviewPopup(QWidget* parent = nullptr);

    /// @brief 设置定义内容并准备显示
    /// @param filePath 目标文件绝对路径
    /// @param line     目标行（1-based，与编辑器一致）
    /// @param col      目标列（1-based，与编辑器一致）
    /// @param codeSnippet 目标行 ±5 行的代码片段（已含换行）
    void showDefinition(const QString& filePath, int line, int col,
                        const QString& codeSnippet);

    /// @brief 在指定全局坐标显示弹窗（自动校正屏幕边界）
    void showAt(const QPoint& globalPos);

    /// @brief 立即隐藏弹窗
    void hidePopup();

protected:
    /// Esc 关闭弹窗
    void keyPressEvent(QKeyEvent* event) override;
    /// 鼠标进入 → 取消自动隐藏计时器
    void enterEvent(QEnterEvent* event) override;
    /// 鼠标离开 → 重启自动隐藏计时器
    void leaveEvent(QEvent* event) override;

private:
    /// 构建标题栏文本：文件名:行号
    QString buildTitle(const QString& filePath, int line) const;

    /// 根据内容自适应弹窗大小（限制最大尺寸，避免过长片段撑爆屏幕）
    void adjustSizeToFit();

    /// 校正位置使弹窗不超出当前屏幕可用区域
    QPoint adjustPosition(const QPoint& globalPos) const;

    QLabel*    m_titleLabel   = nullptr;  // 顶部标题（文件名:行号）
    QTextEdit* m_codeEdit     = nullptr;  // 代码片段只读显示
    QTimer     m_autoHideTimer;           // 5 秒自动隐藏计时器
};

#endif // DEFINITIONPREVIEWPOPUP_H
