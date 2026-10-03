#ifndef GHOSTTEXT_H
#define GHOSTTEXT_H

#include <QObject>
#include <QTextEdit>
#include <QTimer>
#include <QTextCursor>
#include <functional>

class AIClient;
class QKeyEvent;

/// @brief M8 stage5: AI 内联补全 ghost text（灰色幽灵文本 + Tab 接受 / Esc 取消）
///
/// 实现：将补全文本真实插入光标处（编辑器光标保持在 ghost 之前），
/// 以 ExtraSelection 灰色前景渲染；Tab=接受（保留文本为正常编辑一步），
/// Esc/方向键/普通输入=取消（joinPreviousEditBlock 撤销合并，不留 undo 痕迹）。
/// 触发：输入停顿 800ms 后自动请求；补全弹窗可见时通过 triggerGuard 让位。
class GhostText : public QObject
{
    Q_OBJECT

public:
    explicit GhostText(QTextEdit* editor, QObject* parent = nullptr);
    ~GhostText() override;

    bool isActive() const;
    /// 本类插入/撤下造成的 textChanged（供编辑器抑制补全弹窗联动）
    bool isInternalEdit() const { return m_inserting; }
    bool isBusy() const;

    /// 编辑器 keyPressEvent 最前哨：返回 true=按键已消费（Tab 接受 / Esc 取消）；
    /// 返回 false=ghost 已撤下、按键按默认逻辑继续处理
    bool handleKeyPress(QKeyEvent* event);

    /// 鼠标点击等光标重定位场景：撤下 ghost（撤销合并）
    void dismiss();

    /// 触发让位条件注入（返回 true 表示当前不宜请求，如补全弹窗可见）
    void setTriggerGuard(std::function<bool()> guard) { m_triggerGuard = std::move(guard); }

    /// 编辑器 extra selections 重建管线调用：ghost 活动时提供灰色前景选择
    void appendSelection(QList<QTextEdit::ExtraSelection>& out) const;

signals:
    /// ghost 插入/撤下后请求编辑器刷新 extra selections
    void refreshRequested();

private slots:
    void requestCompletion();

private:
    void showGhost(const QString& rawReply);
    void removeGhostText(bool joinUndo);
    void clearGhostMarkers();

    QTextEdit* m_editor = nullptr;
    AIClient* m_client = nullptr;
    QTimer m_debounce;           ///< 输入停顿防抖（800ms）

    QTextCursor m_ghostCursor;   ///< 覆盖 ghost 文本的选择区（随文档编辑自动调整）
    QString m_ghostText;
    QString m_replyBuffer;       ///< 非流式响应累积
    bool m_inserting = false;    ///< 本类插入/撤下引发的 textChanged 抑制标志
    std::function<bool()> m_triggerGuard;
};

#endif // GHOSTTEXT_H
