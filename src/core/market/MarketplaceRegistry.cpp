#include "core/market/MarketplaceRegistry.h"
#include "Logger.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <functional>
#include <memory>

QUrl MarketplaceRegistry::defaultUrl()
{
    return QUrl(QStringLiteral(
        "https://raw.githubusercontent.com/sclzj595/SoulCove-market/main/marketplace.json"));
}

QString MarketplaceRegistry::localFallbackPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/marketplace.json");
}

MarketplaceRegistry::MarketplaceRegistry(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

// M9 stage2: 多源回退 —— fetch(url) 内部构建候选源序列，逐个尝试直到成功：
//   1) 显式传入的 url（如自定义源）
//   2) 用户本地源 AppData/marketplace.json（离线/内网场景，手动放置即可生效）
//   3) 默认远程源 SoulCove-market 仓库
void MarketplaceRegistry::fetch(const QUrl& url)
{
    QList<QUrl> sources;
    if (url.isValid() && !url.isEmpty()) sources.append(url);
    const QString localPath = localFallbackPath();
    if (QFileInfo::exists(localPath)) sources.append(QUrl::fromLocalFile(localPath));
    sources.append(defaultUrl());
    if (sources.isEmpty()) {
        emit failed(QStringLiteral("没有可用的注册表源"));
        return;
    }

    auto tryNext = std::make_shared<int>(0);
    // 自递归闭包经 shared_ptr 持有，异步回调存活期安全（不悬空引用栈上对象）
    auto attempt = std::make_shared<std::function<void()>>();
    *attempt = [this, sources, tryNext, attempt]() mutable {
        if (*tryNext >= sources.size()) {
            emit failed(QStringLiteral("全部注册表源拉取失败（远程/本地共 %1 个）").arg(sources.size()));
            return;
        }
        const QUrl src = sources[(*tryNext)++];
        const bool isLocal = src.isLocalFile();

        if (isLocal) {
            // 本地源：同步读取（毫秒级）
            QFile f(src.toLocalFile());
            if (!f.open(QIODevice::ReadOnly)) {
                (*attempt)();
                return;
            }
            QString parseError;
            m_items = parse(f.readAll(), &parseError);
            if (!parseError.isEmpty()) {
                LOG_DEBUG("[MarketplaceRegistry] 本地源解析失败:" << parseError.toStdString());
                (*attempt)();
                return;
            }
            LOG_INFO("[MarketplaceRegistry] 注册表已从本地源加载:" << src.toLocalFile().toStdString());
            emit fetched(m_items);
            return;
        }

        QNetworkRequest req(src);
        req.setTransferTimeout(15000);
        QNetworkReply* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                LOG_DEBUG("[MarketplaceRegistry] 源拉取失败:"
                          << reply->errorString().toStdString());
                (*attempt)();   // 换下一个源
                return;
            }
            QString parseError;
            m_items = parse(reply->readAll(), &parseError);
            if (!parseError.isEmpty()) {
                LOG_DEBUG("[MarketplaceRegistry]" << parseError.toStdString());
                (*attempt)();
                return;
            }
            emit fetched(m_items);
        });
    };
    (*attempt)();
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
        it.sha256 = o.value(QStringLiteral("sha256")).toString().toLower();
        if (it.isValid()) out.append(it);
    }
    return out;
}
