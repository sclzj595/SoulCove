#include "ui/market/MarketplacePanel.h"
#include "core/plugin/PluginManager.h"
#include "core/snippet/SnippetManager.h"
#include "core/config/ThemeManager.h"
#include "Logger.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>

MarketplacePanel::MarketplacePanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    applyTheme();
    onRefreshClicked();
}

void MarketplacePanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // === 顶部：刷新 + 搜索 ===
    auto* topRow = new QHBoxLayout();
    m_btnRefresh = new QPushButton(tr("刷新"), this);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("搜索扩展..."));
    m_searchEdit->setClearButtonEnabled(true);
    topRow->addWidget(m_btnRefresh);
    topRow->addWidget(m_searchEdit, 1);
    layout->addLayout(topRow);

    // === 条目列表 ===
    m_itemList = new QListWidget(this);
    m_itemList->setObjectName(QStringLiteral("marketItemList"));
    layout->addWidget(m_itemList, 3);

    // === 详情区 ===
    m_detailView = new QTextBrowser(this);
    m_detailView->setObjectName(QStringLiteral("marketDetail"));
    m_detailView->setOpenExternalLinks(true);
    layout->addWidget(m_detailView, 2);

    // === 操作行 ===
    auto* actionRow = new QHBoxLayout();
    m_btnInstall = new QPushButton(tr("安装"), this);
    m_btnUninstall = new QPushButton(tr("卸载"), this);
    m_btnOpenDir = new QPushButton(tr("打开插件目录"), this);
    actionRow->addWidget(m_btnInstall);
    actionRow->addWidget(m_btnUninstall);
    actionRow->addStretch();
    actionRow->addWidget(m_btnOpenDir);
    layout->addLayout(actionRow);

    // === 状态行 ===
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // === 信号 ===
    connect(m_btnRefresh, &QPushButton::clicked, this, &MarketplacePanel::onRefreshClicked);
    connect(m_itemList, &QListWidget::currentRowChanged, this, &MarketplacePanel::onSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MarketplacePanel::onSearchChanged);
    connect(m_btnInstall, &QPushButton::clicked, this, &MarketplacePanel::onInstallClicked);
    connect(m_btnUninstall, &QPushButton::clicked, this, &MarketplacePanel::onUninstallClicked);
    connect(m_btnOpenDir, &QPushButton::clicked, this, &MarketplacePanel::onOpenDirClicked);
    connect(&m_registry, &MarketplaceRegistry::fetched, this, [this](const QList<MarketItem>& items) {
        m_items = items;
        rebuildList(m_searchEdit->text());
        setStatus(tr("已加载 %1 个扩展").arg(items.size()));
    });
    connect(&m_registry, &MarketplaceRegistry::failed,
            this, &MarketplacePanel::onFetchFailed);
    updateButtons();
}

void MarketplacePanel::applyTheme()
{
    const auto& p = ThemeManager::instance().currentPalette();
    m_itemList->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; }")
        .arg(p.bgEditor.name(), p.fgPrimary.name(), p.borderDefault.name()));
    m_detailView->setStyleSheet(QStringLiteral(
        "QTextBrowser { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 6px; }")
        .arg(p.bgEditor.name(), p.fgPrimary.name(), p.borderDefault.name()));
    m_statusLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(p.fgSecondary.name()));
}

void MarketplacePanel::onRefreshClicked()
{
    m_btnRefresh->setEnabled(false);
    setStatus(tr("正在拉取注册表…"));
    m_registry.fetch();
}

void MarketplacePanel::onFetchFailed(const QString& error)
{
    m_btnRefresh->setEnabled(true);
    m_items.clear();
    rebuildList(m_searchEdit->text());
    setStatus(tr("✗ ") + error + tr("（检查网络或注册表源；插件也可手动放入 plugins/ 目录）"));
}

void MarketplacePanel::onSearchChanged(const QString& text)
{
    rebuildList(text);
}

void MarketplacePanel::rebuildList(const QString& filter)
{
    m_itemList->clear();
    const QString f = filter.trimmed().toLower();
    for (const MarketItem& it : m_items) {
        if (!f.isEmpty()
            && !it.name.toLower().contains(f)
            && !it.description.toLower().contains(f)
            && !it.author.toLower().contains(f)) {
            continue;
        }
        const QString typeTag = (it.type == QLatin1String("plugin")) ? tr("插件")
                                : (it.type == QLatin1String("theme")) ? tr("主题")
                                : (it.type == QLatin1String("snippet")) ? tr("片段")
                                : it.type;
        // M9 stage3: 已安装 / 可更新标记（按版本比较）
        QString stateTag;
        const QString instVer = installedVersion(it);
        if (!instVer.isEmpty()) {
            if (compareVersions(it.version, instVer) > 0)
                stateTag = QStringLiteral(" ⬆可更新 v%1→v%2").arg(instVer, it.version);
            else
                stateTag = QStringLiteral(" ✓已安装");
        }
        m_itemList->addItem(QStringLiteral("%1  v%2  [%3]%4  —  %5")
                                .arg(it.name, it.version, typeTag, stateTag, it.author));
    }
    if (m_itemList->count() > 0) m_itemList->setCurrentRow(0);
    else onSelectionChanged();

    // 更新检查汇总提示
    const int updatable = countUpdatable();
    if (updatable > 0) {
        setStatus(tr("已加载 %1 个扩展，其中 %2 个有可用更新（选中后点「安装」覆盖更新）")
                      .arg(m_items.size()).arg(updatable));
    }
}

// ============================================================
// M9 stage3: 版本比较与更新检查
// ============================================================

QString MarketplacePanel::installedVersion(const MarketItem& it) const
{
    if (it.type != QLatin1String("plugin")) return QString();
    for (const PluginRecord& rec : PluginManager::instance().plugins()) {
        if (QFileInfo(rec.filePath).fileName().compare(it.fileName, Qt::CaseInsensitive) == 0) {
            return rec.version;
        }
    }
    return QString();
}

int MarketplacePanel::compareVersions(const QString& a, const QString& b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int va = (i < pa.size()) ? pa[i].toInt() : 0;
        const int vb = (i < pb.size()) ? pb[i].toInt() : 0;
        if (va != vb) return va - vb;
    }
    return 0;
}

int MarketplacePanel::countUpdatable() const
{
    int n = 0;
    for (const MarketItem& it : m_items) {
        const QString instVer = installedVersion(it);
        if (!instVer.isEmpty() && compareVersions(it.version, instVer) > 0) ++n;
    }
    return n;
}

void MarketplacePanel::onSelectionChanged()
{
    const MarketItem* it = selectedItem();
    if (!it) {
        m_detailView->setHtml(tr("<i>未选择扩展</i>"));
    } else {
        m_detailView->setHtml(QStringLiteral(
            "<b>%1</b>  v%2<br>"
            "<span style='color:#888;'>%3 ｜ %4</span><br><br>%5")
            .arg(it->name.toHtmlEscaped(), it->version.toHtmlEscaped(),
                 it->author.toHtmlEscaped(), it->type.toHtmlEscaped(),
                 it->description.toHtmlEscaped()));
    }
    updateButtons();
}

const MarketItem* MarketplacePanel::selectedItem() const
{
    const int row = m_itemList->currentRow();
    if (row < 0) return nullptr;
    // 过滤后的列表与 m_items 顺序一致（rebuildList 按序添加）
    const QString f = m_searchEdit->text().trimmed().toLower();
    int idx = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        const MarketItem& it = m_items[i];
        if (!f.isEmpty()
            && !it.name.toLower().contains(f)
            && !it.description.toLower().contains(f)
            && !it.author.toLower().contains(f)) {
            continue;
        }
        ++idx;
        if (idx == row) return &m_items[i];
    }
    return nullptr;
}

QString MarketplacePanel::pluginsDir() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
}

void MarketplacePanel::updateButtons()
{
    const MarketItem* it = selectedItem();
    // M9 stage3: plugin 与 snippet 均可安装
    const bool installable = it && (it->type == QLatin1String("plugin")
                                    || it->type == QLatin1String("snippet"));
    m_btnInstall->setEnabled(installable);
    m_btnUninstall->setEnabled(it && it->type == QLatin1String("plugin"));
}

// ============================================================
// M9 stage1: 安装 / 卸载 / 打开目录
// ============================================================

void MarketplacePanel::onInstallClicked()
{
    const MarketItem* it = selectedItem();
    if (!it) return;
    if (it->type != QLatin1String("plugin") && it->type != QLatin1String("snippet")) {
        // M9 stage3: theme 需要宿主侧自定义主题文件加载器（ThemeManager 尚无该机制），维持占位
        setStatus(tr("「%1」类型的在线安装将在后续版本支持").arg(it->type));
        return;
    }

    m_btnInstall->setEnabled(false);
    setStatus(tr("正在下载 %1 …").arg(it->name));

    QUrl url(it->downloadUrl);
    if (!url.isValid()) {
        setStatus(tr("✗ 下载地址无效"));
        m_btnInstall->setEnabled(true);
        return;
    }

    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setTransferTimeout(60000);
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, it]() {
        reply->deleteLater();
        nam->deleteLater();
        m_btnInstall->setEnabled(true);

        if (reply->error() != QNetworkReply::NoError) {
            setStatus(tr("✗ 下载失败: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            setStatus(tr("✗ 下载内容为空"));
            return;
        }

        // M9 stage2: SHA-256 完整性校验（注册表条目可选提供 sha256 字段）
        if (!it->sha256.isEmpty()) {
            const QString actual = QString::fromLatin1(
                QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            if (actual != it->sha256) {
                setStatus(tr("✗ 校验和不匹配（预期 %1，实际 %2）——下载可能被篡改，已放弃安装")
                              .arg(it->sha256.left(16) + QStringLiteral("…"),
                                   actual.left(16) + QStringLiteral("…")));
                LOG_ERROR("[MarketplacePanel] SHA-256 mismatch:" << it->id.toStdString());
                return;
            }
            setStatus(tr("校验和验证通过，正在写入…"));
        }

        // ===== M9 stage3: 代码片段安装（下载 → VSCode JSON 导入 SnippetManager）=====
        if (it->type == QLatin1String("snippet")) {
            const QString tmpPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                    + QStringLiteral("/soulcove_market_snippet.json");
            QFile tmp(tmpPath);
            if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                setStatus(tr("✗ 临时文件写入失败"));
                return;
            }
            tmp.write(data);
            tmp.close();
            if (SnippetManager::instance().importFromVscodeJson(tmpPath)) {
                tmp.remove();
                setStatus(tr("✓ 片段包「%1」已导入（工具 → 代码片段管理 可查看/编辑）").arg(it->name));
            } else {
                setStatus(tr("✗ 片段导入失败（检查 JSON 是否为 VSCode snippet 格式）"));
            }
            rebuildList(m_searchEdit->text());
            return;
        }

        // ===== 插件安装 =====
        // 文件名安全：只取文件名部分，拒绝路径穿越
        const QString safeName = QFileInfo(it->fileName).fileName();
        if (safeName.isEmpty()) {
            setStatus(tr("✗ 注册表 fileName 无效"));
            return;
        }
        QDir().mkpath(pluginsDir());
        const QString target = pluginsDir() + QStringLiteral("/") + safeName;

        // 覆盖安装：先尝试删除旧文件（插件已加载时 Windows 会失败 → 提示重启后重试）
        if (QFileInfo::exists(target) && !QFile::remove(target)) {
            setStatus(tr("⚠ 旧版本文件被占用（插件已加载），请重启应用后重新安装"));
            return;
        }
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setStatus(tr("✗ 写入失败: %1").arg(target));
            return;
        }
        out.write(data);
        out.close();

        setStatus(tr("✓ 已安装 %1 → %2。重启应用后由插件管理器加载。")
                      .arg(it->name, QDir::toNativeSeparators(target)));
        LOG_INFO("[MarketplacePanel] 插件已安装:" << target.toStdString());
    });
}

void MarketplacePanel::onUninstallClicked()
{
    const MarketItem* it = selectedItem();
    if (!it) return;
    if (it->type != QLatin1String("plugin")) {
        setStatus(tr("stage1 暂只支持插件卸载"));
        return;
    }

    // 已加载插件（Windows 下 DLL 被占用，无法直接删除）
    for (const PluginRecord& rec : PluginManager::instance().plugins()) {
        if (QFileInfo(rec.filePath).fileName().compare(it->fileName, Qt::CaseInsensitive) == 0) {
            setStatus(tr("⚠ 插件「%1」当前已加载，无法立即删除；请重启应用后在此重试，"
                         "或直接关闭应用后手动删除文件。").arg(it->name));
            return;
        }
    }

    const QString target = pluginsDir() + QStringLiteral("/") + QFileInfo(it->fileName).fileName();
    if (!QFileInfo::exists(target)) {
        setStatus(tr("未找到已安装文件: %1").arg(target));
        return;
    }
    if (QFile::remove(target)) {
        setStatus(tr("✓ 已卸载 %1").arg(it->name));
    } else {
        setStatus(tr("✗ 删除失败（文件可能被占用）: %1").arg(target));
    }
}

void MarketplacePanel::onOpenDirClicked()
{
    QDir().mkpath(pluginsDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(pluginsDir()));
}

void MarketplacePanel::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
}
