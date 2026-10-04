#include "core/market/MarketplaceRegistry.h"
#include "Logger.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

QUrl MarketplaceRegistry::defaultUrl()
{
    return QUrl(QStringLiteral(
        "https://raw.githubusercontent.com/sclzj595/SoulCove-market/main/marketplace.json"));
}

MarketplaceRegistry::MarketplaceRegistry(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void MarketplaceRegistry::fetch(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const QString err = QStringLiteral("注册表拉取失败（HTTP %1）: %2")
                                    .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                    .arg(reply->errorString());
            LOG_DEBUG("[MarketplaceRegistry]" << err.toStdString());
            emit failed(err);
            return;
        }
        QString parseError;
        m_items = parse(reply->readAll(), &parseError);
        if (!parseError.isEmpty()) {
            emit failed(parseError);
            return;
        }
        emit fetched(m_items);
    });
}

QList<MarketItem> MarketplaceRegistry::parse(const QByteArray& json, QString* error)
{
    QList<MarketItem> out;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("注册表 JSON 解析失败: %1").arg(perr.errorString());
        return out;
    }
    const QJsonArray arr = doc.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        MarketItem it;
        it.id = o.value(QStringLiteral("id")).toString();
        it.name = o.value(QStringLiteral("name")).toString();
        it.version = o.value(QStringLiteral("version")).toString();
        it.type = o.value(QStringLiteral("type")).toString();
        it.author = o.value(QStringLiteral("author")).toString();
        it.description = o.value(QStringLiteral("description")).toString();
        it.downloadUrl = o.value(QStringLiteral("downloadUrl")).toString();
        it.fileName = o.value(QStringLiteral("fileName")).toString();
        if (it.isValid()) out.append(it);
    }
    return out;
}
