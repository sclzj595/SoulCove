#ifndef KEYSEQUENCEEDIT_H
#define KEYSEQUENCEEDIT_H

#include <QLineEdit>
#include <QKeySequence>

/// @brief 快捷键录制输入框（P2-H05 子项3）
///
/// 继承 QLineEdit，重写 keyPressEvent 捕获用户按键并转换为
/// QKeySequence 文本（如 "Ctrl+S"、"Ctrl+Shift+P"）。
///
/// 交互行为：
/// - 按下任意组合键（含修饰键 + 主键）即追加到当前序列
/// - Backspace：清除最后一个按键
/// - Esc：取消录制，恢复进入录制前的原值并清空焦点
/// - Enter/Return：结束录制并提交
/// - 失去焦点：提交当前序列
///
/// 支持最多 4 个按键的组合（与 QKeySequence 容量一致）。
class KeySequenceEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit KeySequenceEdit(QWidget* parent = nullptr);

    /// 设置初始快捷键（进入录制模式前的原值）
    void setKeySequence(const QKeySequence& seq);

    /// 获取当前录制的快捷键
    QKeySequence keySequence() const;

signals:
    /// 录制完成（用户按 Enter 或失去焦点时发射）
    void editingFinished(const QKeySequence& seq);

    /// 用户取消录制（按 Esc）
    void editingCanceled();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    /// 将当前 m_keys 列表刷新到 QLineEdit 文本
    void refreshText();

    /// 单个按键（修饰键 + 主键）的封装
    struct KeyCombination {
        Qt::KeyboardModifiers mods;
        int key;
    };

    QList<KeyCombination> m_keys;     ///< 已录制的按键列表（最多 4 个）
    QKeySequence          m_original; ///< 进入录制前的原值
    bool                  m_finished = false;  ///< 是否已发射过结束信号（防重入）
};

#endif // KEYSEQUENCEEDIT_H
