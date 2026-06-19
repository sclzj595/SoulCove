#ifndef EDITORSPLITVIEW_H
#define EDITORSPLITVIEW_H

#include <QWidget>
#include <QSplitter>

class MyTextEdit;
class QPushButton;
class QLabel;

/// @brief V1.9: 编辑器分栏视图
/// 在主编辑器旁边显示当前文件的第二个视图，共享 QTextDocument（编辑同步）
/// 支持水平/垂直分栏，可关闭
class EditorSplitView : public QWidget
{
    Q_OBJECT
public:
    explicit EditorSplitView(QWidget* parent = nullptr);

    /// @brief 设置源编辑器（共享其 document）
    /// @param source 源编辑器实例（nullptr 则清空分栏）
    void setSourceEditor(MyTextEdit* source);

    /// @brief 获取分栏内的编辑器实例
    MyTextEdit* editor() const { return m_editor; }

    /// @brief 分栏是否处于活动状态（有源编辑器）
    bool isActive() const { return m_sourceEditor != nullptr; }

    /// @brief 获取源编辑器
    MyTextEdit* sourceEditor() const { return m_sourceEditor; }

signals:
    /// @brief 请求关闭分栏
    void closeRequested();

private:
    MyTextEdit*    m_sourceEditor = nullptr;  // 源编辑器（共享 document 来源）
    MyTextEdit*    m_editor = nullptr;        // 分栏编辑器实例
    QPushButton*   m_btnClose = nullptr;      // 关闭按钮
    QLabel*        m_titleLabel = nullptr;    // 标题标签
};

#endif // EDITORSPLITVIEW_H
