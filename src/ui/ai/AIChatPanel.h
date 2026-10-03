#ifndef AICHATPANEL_H
#define AICHATPANEL_H

#include <QWidget>
#include <QComboBox>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QList>
#include <QStringList>

#include "core/ai/AIProviderStore.h"
#include "core/ai/AIClient.h"

/// @brief AI 助手对话面板（M8 O36，标签页内嵌，对标 Trae / Copilot Chat）
///
/// 顶部：服务商下拉 + 新会话；中部：对话历史（SSE 流式渲染，打字机效果）；
/// 底部：输入区（Enter 发送 / Shift+Enter 换行）。
/// 回答支持「复制」与「插入到编辑器」。
class AIChatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AIChatPanel(QWidget* parent = nullptr);

    /// 设置页保存服务商后调用（Widget 监听 AIProviderStore::providersChanged 转发）
    void refreshProviders();

    /// M8 stage2: AI 动作入口（解释代码/修 Bug 等）。
    /// 把 contextBlock 作为上下文与 userText 一起进入会话并立即发送（Widget 负责先打开面板）。
    void sendAction(const QString& userText, const QString& contextBlock);

signals:
    /// 请求把文本插入当前编辑器光标处（Widget 负责落点）
    void insertToEditorRequested(const QString& text);

private slots:
    void onSendClicked();
    void onStopClicked();
    void onNewSessionClicked();
    void onDelta(const QString& delta);
    void onFinished(bool ok, const QString& error);

private:
    void setupUi();
    void applyTheme();
    void rebuildHtml();
    void updateButtons();
    // M8 stage4: 会话历史持久化（ai/chat_session.json，应用重启自动恢复）
    QString sessionFilePath() const;
    void saveSession();
    void restoreSession();
    AIProvider currentProvider() const;
    bool eventFilter(QObject* obj, QEvent* event) override;

    QComboBox* m_providerCombo = nullptr;
    QPushButton* m_newSessionBtn = nullptr;
    QTextBrowser* m_historyView = nullptr;
    QPlainTextEdit* m_inputEdit = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QPushButton* m_copyBtn = nullptr;
    QPushButton* m_insertBtn = nullptr;
    QLabel* m_statusLabel = nullptr;

    AIClient* m_client = nullptr;
    QStringList m_comboIds;                 ///< 下拉项 → provider id 映射
    QList<AIChatMessage> m_history;         /// 会话历史（不含 system 提示）
    QString m_pendingAnswer;                ///< 流式累积中的回答
    QTimer m_flushTimer;                    ///< delta 合并刷新（100ms，避免高频重排）
    bool m_dirty = false;                   ///< 刷新期间有新 delta
    bool m_streaming = false;
};

#endif // AICHATPANEL_H
