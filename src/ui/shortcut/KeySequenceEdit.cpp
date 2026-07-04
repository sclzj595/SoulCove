#include "ui/shortcut/KeySequenceEdit.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QFocusEvent>

KeySequenceEdit::KeySequenceEdit(QWidget* parent)
    : QLineEdit(parent)
{
    // 录制框只读：所有按键由 keyPressEvent 处理，不接受普通文本输入
    setReadOnly(true);
    setPlaceholderText(QObject::tr("按下组合键... (Backspace 删除, Esc 取消)"));
    setAttribute(Qt::WA_MacShowFocusRect, true);
    setClearButtonEnabled(false);
}

void KeySequenceEdit::setKeySequence(const QKeySequence& seq)
{
    m_original = seq;
    m_keys.clear();

    for (int i = 0; i < seq.count(); ++i) {
        QKeyCombination kc = seq[i];
        int combined = kc.toCombined();
        Qt::KeyboardModifiers mods = Qt::NoModifier;
        if (combined & Qt::CTRL)  mods |= Qt::ControlModifier;
        if (combined & Qt::SHIFT) mods |= Qt::ShiftModifier;
        if (combined & Qt::ALT)   mods |= Qt::AltModifier;
        if (combined & Qt::META)  mods |= Qt::MetaModifier;
        int key = kc.key();
        m_keys.append({mods, key});
    }

    refreshText();
}

QKeySequence KeySequenceEdit::keySequence() const
{
    if (m_keys.isEmpty()) return QKeySequence();

    // 将 m_keys 拼接为 QKeySequence 支持的字符串格式："Ctrl+S, Ctrl+Shift+P"
    QStringList parts;
    for (const auto& kc : m_keys) {
        QStringList seg;
        if (kc.mods & Qt::ControlModifier) seg << QStringLiteral("Ctrl");
        if (kc.mods & Qt::ShiftModifier)   seg << QStringLiteral("Shift");
        if (kc.mods & Qt::AltModifier)     seg << QStringLiteral("Alt");
        if (kc.mods & Qt::MetaModifier)    seg << QStringLiteral("Meta");

        QString keyName = QKeySequence(kc.key).toString();
        // QKeySequence(int key) 单键序列化会返回如 "S"、"F12"、"/" 等
        if (keyName.isEmpty()) {
            keyName = QString::number(kc.key);
        }
        seg << keyName;
        parts << seg.join(QLatin1Char('+'));
    }

    return QKeySequence(parts.join(QStringLiteral(", ")));
}

void KeySequenceEdit::keyPressEvent(QKeyEvent* event)
{
    int key = event->key();

    // Esc：取消录制，恢复原值
    if (key == Qt::Key_Escape) {
        m_keys.clear();
        for (int i = 0; i < m_original.count(); ++i) {
            QKeyCombination kc = m_original[i];
            int combined = kc.toCombined();
            Qt::KeyboardModifiers mods = Qt::NoModifier;
            if (combined & Qt::CTRL)  mods |= Qt::ControlModifier;
            if (combined & Qt::SHIFT) mods |= Qt::ShiftModifier;
            if (combined & Qt::ALT)   mods |= Qt::AltModifier;
            if (combined & Qt::META)  mods |= Qt::MetaModifier;
            int rawKey = kc.key();
            m_keys.append({mods, rawKey});
        }
        refreshText();
        if (!m_finished) {
            m_finished = true;
            // 延迟发射：槽函数会通过 removeCellWidget 删除本对象，
            // 同步发射后 clearFocus() 将访问已释放的 this
            QMetaObject::invokeMethod(this, [this]() {
                emit editingCanceled();
            }, Qt::QueuedConnection);
        }
        clearFocus();
        return;
    }

    // Backspace：清除最后一个按键
    if (key == Qt::Key_Backspace) {
        if (!m_keys.isEmpty()) {
            m_keys.removeLast();
            refreshText();
        }
        return;
    }

    // Enter/Return：结束录制并提交
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (!m_finished) {
            m_finished = true;
            QKeySequence seq = keySequence();
            QMetaObject::invokeMethod(this, [this, seq]() {
                emit editingFinished(seq);
            }, Qt::QueuedConnection);
        }
        clearFocus();  // 触发 focusOutEvent，但 m_finished 已置位不会重复发射
        return;
    }

    // Tab：交由父类处理焦点切换
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        QLineEdit::keyPressEvent(event);
        return;
    }

    // 纯修饰键不处理（等待主键）
    if (key == Qt::Key_Control || key == Qt::Key_Shift ||
        key == Qt::Key_Alt     || key == Qt::Key_Meta) {
        return;
    }

    // 容量上限：QKeySequence 最多 4 个按键
    if (m_keys.size() >= 4) {
        return;
    }

    // 过滤掉无法识别的按键（如单独的媒体键）
    if (key == 0 || key == Qt::Key_unknown) {
        return;
    }

    Qt::KeyboardModifiers mods = event->modifiers();
    // 简化处理：单键 F1-F12、方向键、PageUp/Down 等允许无修饰键
    bool isFunctionKey =
        (key >= Qt::Key_F1 && key <= Qt::Key_F35) ||
        key == Qt::Key_Home || key == Qt::Key_End ||
        key == Qt::Key_PageUp || key == Qt::Key_PageDown ||
        key == Qt::Key_Insert || key == Qt::Key_Delete ||
        key == Qt::Key_Left || key == Qt::Key_Right ||
        key == Qt::Key_Up || key == Qt::Key_Down;

    // 普通可打印字符必须配合至少一个修饰键（避免误触）
    if (!isFunctionKey && mods == Qt::NoModifier) {
        return;
    }

    m_keys.append({mods, key});
    refreshText();
}

void KeySequenceEdit::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    // 失去焦点时自动提交当前录制结果
    // 使用 QueuedConnection 延迟发射：槽函数可能通过 removeCellWidget 删除本对象，
    // 同步发射会在事件处理器返回后访问已释放的 this
    if (!m_finished) {
        m_finished = true;
        QKeySequence seq = keySequence();
        QMetaObject::invokeMethod(this, [this, seq]() {
            emit editingFinished(seq);
        }, Qt::QueuedConnection);
    }
}

void KeySequenceEdit::refreshText()
{
    if (m_keys.isEmpty()) {
        setText(QString());
        return;
    }
    setText(keySequence().toString(QKeySequence::NativeText));
}
