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

    // URL：BaseUrl + /chat/completions（容忍末尾斜杠）
    QUrl url(provider.baseUrl.trimmed());
    QString path = url.path();
    if (!path.endsWith(QStringLiteral("/"))) path += QStringLiteral("/");
    url.setPath(path + QStringLiteral("chat/completions"));

    QJsonObject body;
    body.insert(QStringLiteral("model"), provider.model);
    QJsonArray msgs;
    for (const AIChatMessage& m : messages) {
        QJsonObject o;
        o.insert(QStringLiteral("role"), m.role);
        o.insert(QStringLiteral("content"), m.content);
        msgs.append(o);
    }
    body.insert(QStringLiteral("messages"), msgs);
    body.insert(QStringLiteral("stream"), stream);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!provider.apiKey.trimmed().isEmpty()) {
        req.setRawHeader(QByteArray("Authorization"),
                         QByteArray("Bearer ") + provider.apiKey.toUtf8());
    }
    req.setTransferTimeout(60000);  // 60s 无数据传输则超时（流式按静默期计算）

    m_buffer.clear();
    m_done = false;
    m_userAborted = false;
    m_streaming = stream;

    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &AIClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &AIClient::onFinish);
    LOG_DEBUG_S("AIClient", "start", "请求" << url.toString().toStdString() << " stream=" << stream);
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

    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
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
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QString content;
        if (doc.isObject()) {
            const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty()) {
                content = choices.first().toObject()
                              .value(QStringLiteral("message")).toObject()
                              .value(QStringLiteral("content")).toString();
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
