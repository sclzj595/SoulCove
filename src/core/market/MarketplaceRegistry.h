#ifndef MARKETPLACEREGISTRY_H
#define MARKETPLACEREGISTRY_H

#include <QObject>
#include <QList>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

/// @brief 市场条目（M9 stage1；stage2 增加可选 sha256 下载校验）
struct MarketItem
{
    QString id;             ///< 唯一标识
    QString name;           ///< 显示名
    QString version;        ///< 版本
    QString type;           ///< "plugin" / "theme" / "snippet"（stage1 仅 plugin 可安装）
    QString author;
    QString description;
    QString downloadUrl;    ///< 单文件直链
    QString fileName;       ///< 安装后的文件名（含扩展名）
    QString sha256;         ///< 可选：下载内容 SHA-256（hex 小写，缺省不校验）

    bool isValid() const
    {
        return !id.isEmpty() && !name.isEmpty() && !downloadUrl.isEmpty() && !fileName.isEmpty();
    }
};

/// @brief 扩展市场注册表（M9 stage1）
///
/// 注册表为 JSON：{"items":[{id,name,version,type,author,description,downloadUrl,fileName}]}
/// 默认源为 GitHub 仓库 raw 地址；拉取失败由 UI 层提示（后续 stage 支持多源/本地源）。
class MarketplaceRegistry : public QObject
{
    Q_OBJECT

public:
    /// 默认注册表地址（SoulCove-market 仓库）
    static QUrl defaultUrl();

    explicit MarketplaceRegistry(QObject* parent = nullptr);

    /// 异步拉取注册表（fetched / failed 回调）
    /// M9 stage2 多源回退：显式 url → 用户本地源（AppData/marketplace.json）→ 默认远程源
    void fetch(const QUrl& url = QUrl());

    /// 用户本地源路径（存在即作为远程失败后的兜底）
    static QString localFallbackPath();

    const QList<MarketItem>& items() const { return m_items; }

    /// 解析注册表 JSON（错误时返回空表并写 error）
    static QList<MarketItem> parse(const QByteArray& json, QString* error = nullptr);

signals:
    void fetched(const QList<MarketItem>& items);
    void failed(const QString& error);

private:
    QNetworkAccessManager* m_nam = nullptr;
    QList<MarketItem> m_items;
};

#endif // MARKETPLACEREGISTRY_H
