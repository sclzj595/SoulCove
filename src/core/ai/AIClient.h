#ifndef AICLIENT_H
#define AICLIENT_H

#include <QObject>
#include <QList>
#include <QString>
#include <QByteArray>

#include "core/ai/AIProviderStore.h"

class QNetworkAccessManager;
class QNetworkReply;

/// @brief 一条对话消息（OpenAI role: system / user / assistant）
struct AIChatMessage {
    QString role;
    QString content;
};

/// @brief OpenAI 兼容协议客户端（M8 O35）
///
/// 统一走 {BaseUrl}/chat/completions：
/// - 流式（stream=true）：SSE 解析，deltaReceived 增量回调（Trae/Copilot 式打字机体验）
/// - 非流式：整体返回后一次性回传（测试连接 / 简单问答）
class AIClient : public QObject
{
    Q_OBJECT

public:
    explicit AIClient(QObject* parent = nullptr);
    ~AIClient() override;

    /// 流式对话（deltaReceived 增量回调，finished 收尾）
    void chatStream(const AIProvider& provider, const QList<AIChatMessage>& messages);

    /// 非流式单轮（结果经 deltaReceived 一次性回传后 finished）
    void chatOnce(const AIProvider& provider, const QList<AIChatMessage>& messages);

    /// 取消当前请求（finished(false, "已取消") 收尾）
    void cancel();

    bool isBusy() const { return m_reply != nullptr; }

signals:
    void deltaReceived(const QString& delta);
    void finished(bool ok, const QString& errorOrEmpty);

private:
    void start(const AIProvider& provider, const QList<AIChatMessage>& messages, bool stream);
    void onReadyRead();
    void onFinish();
    void processSseLine(const QByteArray& line);

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_buffer;        ///< SSE 行缓冲 / 非流式响应体
    bool m_streaming = false;
    bool m_done = false;        ///< 已收到 [DONE]
    bool m_userAborted = false;
};

#endif // AICLIENT_H
