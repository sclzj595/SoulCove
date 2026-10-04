#include "core/ai/AIClient.h"
#include "Logger.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QUrl>

AIClient::AIClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

AIClient::~AIClient()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void AIClient::chatStream(const AIProvider& provider, const QList<AIChatMessage>& messages)
{
    start(provider, messages, true);
}

void AIClient::chatOnce(const AIProvider& provider, const QList<AIChatMessage>& messages)
{
    start(provider, messages, false);
}

void AIClient::cancel()
{
    if (m_reply) {
        m_userAborted = true;
        m_reply->abort();  // finished 信号触发收尾
    }
}

// M8 收口: 协议按 baseUrl 域名自动识别 —— 用户无需额外配置字段
AIClient::Protocol AIClient::detectProtocol(const QString& baseUrl)
{
    const QString host = QUrl(baseUrl).host().toLower();
    if (host.endsWith(QStringLiteral("anthropic.com"))) return Protocol::Anthropic;
    if (host.startsWith(QStringLiteral("generativelanguage"))) return Protocol::Gemini;
    return Protocol::OpenAI;
}

void AIClient::start(const AIProvider& provider, const QList<AIChatMessage>& messages, bool stream)
{
    if (m_reply) {
        emit finished(false, QStringLiteral("已有请求进行中"));
        return;
    }
    // M8 stage3: 仅 BaseUrl 必填 —— Ollama 等本地后端无需 API Key；
    //            云端服务商缺 Key 时由服务端返回 401，错误信息照样可见
    if (provider.baseUrl.trimmed().isEmpty()) {
        emit finished(false, QStringLiteral("请先在 设置 → AI 助手 中配置 BaseUrl"));
        return;
    }

    m_protocol = detectProtocol(provider.baseUrl.trimmed());
    const QString apiKey = provider.apiKey.trimmed();

    QUrl url(provider.baseUrl.trimmed());
    QJsonObject body;
    QNetworkRequest req;

    if (m_protocol == Protocol::Anthropic) {
        // Anthropic Messages API：POST {BaseUrl}/v1/messages
        QString path = url.path();
        if (!path.endsWith(QStringLiteral("/"))) path += QStringLiteral("/");
        url.setPath(path + QStringLiteral("v1/messages"));

        // system 是顶层独立字段；messages 仅 user/assistant；max_tokens 必填
        QJsonArray msgs;
        QString systemText;
        for (const AIChatMessage& m : messages) {
            if (m.role == QStringLiteral("system")) { systemText = m.content; continue; }
            QJsonObject o;
            o.insert(QStringLiteral("role"), m.role);
            o.insert(QStringLiteral("content"), m.content);
            msgs.append(o);
        }
        body.insert(QStringLiteral("model"), provider.model);
        body.insert(QStringLiteral("max_tokens"), 4096);
        body.insert(QStringLiteral("stream"), stream);
        body.insert(QStringLiteral("messages"), msgs);
        if (!systemText.isEmpty())
            body.insert(QStringLiteral("system"), systemText);

        req.setUrl(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!apiKey.isEmpty())
            req.setRawHeader(QByteArray("x-api-key"), apiKey.toUtf8());
        req.setRawHeader(QByteArray("anthropic-version"), QByteArray("2023-06-01"));
    } else if (m_protocol == Protocol::Gemini) {
        // Gemini Generative Language API：
        //   非流式 POST {BaseUrl}/v1beta/models/{model}:generateContent
        //   流式   POST {BaseUrl}/v1beta/models/{model}:streamGenerateContent?alt=sse
        QString path = url.path();
        while (path.endsWith(QStringLiteral("/"))) path.chop(1);
        url.setPath(path + QStringLiteral("/v1beta/models/") + provider.model
                    + (stream ? QStringLiteral(":streamGenerateContent")
                              : QStringLiteral(":generateContent")));
        if (stream) url.setQuery(QStringLiteral("alt=sse"));

        // role 映射：assistant→model；system→顶层 system_instruction
        QJsonArray contents;
        QJsonArray sysParts;
        bool hasSystem = false;
        for (const AIChatMessage& m : messages) {
            if (m.role == QStringLiteral("system")) {
                QJsonObject p; p.insert(QStringLiteral("text"), m.content);
                sysParts.append(p);
                hasSystem = true;
                continue;
            }
            QJsonObject part; part.insert(QStringLiteral("text"), m.content);
            QJsonObject o;
            o.insert(QStringLiteral("role"),
                     m.role == QStringLiteral("assistant") ? QStringLiteral("model")
                                                           : QStringLiteral("user"));
            o.insert(QStringLiteral("parts"), QJsonArray{ part });
            contents.append(o);
        }
        body.insert(QStringLiteral("contents"), contents);
        if (hasSystem) {
            QJsonObject si; si.insert(QStringLiteral("parts"), sysParts);
            body.insert(QStringLiteral("system_instruction"), si);
        }

        req.setUrl(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!apiKey.isEmpty())
            req.setRawHeader(QByteArray("x-goog-api-key"), apiKey.toUtf8());
    } else {
        // OpenAI 兼容（默认）：POST {BaseUrl}/chat/completions（容忍末尾斜杠）
        QString path = url.path();
        if (!path.endsWith(QStringLiteral("/"))) path += QStringLiteral("/");
        url.setPath(path + QStringLiteral("chat/completions"));

        QJsonArray msgs;
        for (const AIChatMessage& m : messages) {
            QJsonObject o;
            o.insert(QStringLiteral("role"), m.role);
            o.insert(QStringLiteral("content"), m.content);
            msgs.append(o);
        }
        body.insert(QStringLiteral("model"), provider.model);
        body.insert(QStringLiteral("messages"), msgs);
        body.insert(QStringLiteral("stream"), stream);

        req.setUrl(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!apiKey.isEmpty()) {
            req.setRawHeader(QByteArray("Authorization"),
                             QByteArray("Bearer ") + apiKey.toUtf8());
        }
    }
    req.setTransferTimeout(60000);  // 60s 无数据传输则超时（流式按静默期计算）

    m_buffer.clear();
    m_done = false;
    m_userAborted = false;
    m_streaming = stream;

    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &AIClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &AIClient::onFinish);
    LOG_DEBUG_S("AIClient", "start", "请求" << url.toString().toStdString()
                << " stream=" << stream
                << " protocol=" << static_cast<int>(m_protocol));
}

void AIClient::onReadyRead()
{
    if (!m_reply) return;
    m_buffer.append(m_reply->readAll());
    if (!m_streaming) return;  // 非流式：finish 时整体解析

    // SSE 逐行解析（兼容 \r\n）
    int idx;
    while ((idx = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);
        processSseLine(line.trimmed());
    }
}

void AIClient::processSseLine(const QByteArray& line)
{
    if (line.isEmpty() || !line.startsWith("data:")) return;
    const QByteArray payload = line.mid(5).trimmed();
    if (payload == "[DONE]") {
        m_done = true;
        return;
    }

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject root = doc.object();

    if (m_protocol == Protocol::Anthropic) {
        // 事件流：content_block_delta.delta.text 增量；message_stop 结束
        const QString type = root.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("content_block_delta")) {
            const QString delta = root.value(QStringLiteral("delta")).toObject()
                                      .value(QStringLiteral("text")).toString();
            if (!delta.isEmpty()) emit deltaReceived(delta);
        } else if (type == QStringLiteral("message_stop")) {
            m_done = true;
        }
        // error 事件 → onFinish 统一按 HTTP 错误/错误体处理
        return;
    }

    if (m_protocol == Protocol::Gemini) {
        // data: {candidates:[{content:{parts:[{text}...]}}]} —— 无 [DONE]，流自然结束
        const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) return;
        const QJsonArray parts = candidates.first().toObject()
                                     .value(QStringLiteral("content")).toObject()
                                     .value(QStringLiteral("parts")).toArray();
        for (const QJsonValue& p : parts) {
            const QString delta = p.toObject().value(QStringLiteral("text")).toString();
            if (!delta.isEmpty()) emit deltaReceived(delta);
        }
        return;
    }

    // OpenAI 兼容
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) return;
    const QString delta = choices.first().toObject()
                              .value(QStringLiteral("delta")).toObject()
                              .value(QStringLiteral("content")).toString();
    if (!delta.isEmpty()) emit deltaReceived(delta);
}

void AIClient::onFinish()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply) return;
    reply->deleteLater();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        if (m_userAborted) {
            emit finished(false, QStringLiteral("已取消"));
            return;
        }
        if (reply->error() == QNetworkReply::OperationCanceledError && m_done) {
            // [DONE] 已收到，取消只是提前断开 — 视为成功
            emit finished(true, QString());
            return;
        }
        // 提取服务端错误详情（{"error":{"message":...}}）
        QString detail;
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            detail = doc.object().value(QStringLiteral("error")).toObject()
                         .value(QStringLiteral("message")).toString();
        }
        emit finished(false, QStringLiteral("HTTP %1: %2%3")
                                  .arg(httpStatus)
                                  .arg(reply->errorString(),
                                       detail.isEmpty() ? QString()
                                                        : QStringLiteral(" - ") + detail));
        return;
    }

    if (m_streaming) {
        // 处理缓冲区残留（无换行结尾的最后一行）
        if (!m_buffer.isEmpty()) {
            processSseLine(m_buffer.trimmed());
            m_buffer.clear();
        }
        emit finished(true, QString());
    } else {
        // 非流式整体解析（按协议）
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QString content;
        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            if (m_protocol == Protocol::Anthropic) {
                // {content:[{type:"text",text:...},...]} —— 拼接全部 text 块
                const QJsonArray blocks = root.value(QStringLiteral("content")).toArray();
                for (const QJsonValue& b : blocks) {
                    const QJsonObject o = b.toObject();
                    if (o.value(QStringLiteral("type")).toString() == QLatin1String("text"))
                        content += o.value(QStringLiteral("text")).toString();
                }
            } else if (m_protocol == Protocol::Gemini) {
                // {candidates:[{content:{parts:[{text}...]}}]}
                const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
                if (!candidates.isEmpty()) {
                    const QJsonArray parts = candidates.first().toObject()
                                                 .value(QStringLiteral("content")).toObject()
                                                 .value(QStringLiteral("parts")).toArray();
                    for (const QJsonValue& p : parts)
                        content += p.toObject().value(QStringLiteral("text")).toString();
                }
            } else {
                const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
                if (!choices.isEmpty()) {
                    content = choices.first().toObject()
                                  .value(QStringLiteral("message")).toObject()
                                  .value(QStringLiteral("content")).toString();
                }
            }
        }
        if (content.isEmpty()) {
            emit finished(false, QStringLiteral("响应解析失败（HTTP %1）").arg(httpStatus));
        } else {
            emit deltaReceived(content);
            emit finished(true, QString());
        }
    }
}
