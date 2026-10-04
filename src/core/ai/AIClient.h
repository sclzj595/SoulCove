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

/// @brief 多协议 AI 客户端（M8 O35；M8 收口扩展 Claude/Gemini 原生协议）
///
/// 协议按 BaseUrl 域名自动识别（detectProtocol），无需额外配置：
/// - OpenAI 兼容（默认）：{BaseUrl}/chat/completions，Bearer 鉴权
///   （智谱/DeepSeek/Kimi/通义/OpenAI/Ollama 均属此类）
/// - Anthropic：{BaseUrl}/v1/messages，x-api-key + anthropic-version 头，
///   system 独立字段、必填 max_tokens，SSE content_block_delta
/// - Gemini：{BaseUrl}/v1beta/models/{model}:generateContent[:streamGenerateContent?alt=sse]，
///   x-goog-api-key 头，system_instruction/contents 结构，parts[] 拼接
///
/// 流式：deltaReceived 增量回调（打字机体验）；非流式：整体回传（测试连接/简单问答）
class AIClient : public QObject
{
    Q_OBJECT

public:
    /// 协议族（按 baseUrl 域名自动识别）
    enum class Protocol { OpenAI, Anthropic, Gemini };

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
    static Protocol detectProtocol(const QString& baseUrl);

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_buffer;        ///< SSE 行缓冲 / 非流式响应体
    Protocol m_protocol = Protocol::OpenAI;  ///< 当前请求协议（start 时按 baseUrl 识别）
    bool m_streaming = false;
    bool m_done = false;        ///< 已收到 [DONE]
    bool m_userAborted = false;
};

#endif // AICLIENT_H
