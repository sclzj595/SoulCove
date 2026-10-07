#ifndef MARKETPLACEPANEL_H
#define MARKETPLACEPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QList>

#include "core/market/MarketplaceRegistry.h"

/// @brief 扩展市场面板（M9 stage1，标签页内嵌）
///
/// 布局对标 VSCode 扩展页：顶部工具行（刷新/搜索）→ 条目列表 → 详情区 → 操作行。
/// stage1 支持插件（plugin）在线安装/卸载；theme/snippet 预留（提示即将支持）。
class MarketplacePanel : public QWidget
{
    Q_OBJECT

public:
    explicit MarketplacePanel(QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();
    void onFetchFailed(const QString& error);
    void onSelectionChanged();
    void onSearchChanged(const QString& text);
    void onInstallClicked();
    void onUninstallClicked();
    void onOpenDirClicked();

private:
    void setupUi();
    void applyTheme();
    void rebuildList(const QString& filter = QString());
    void updateButtons();
    const MarketItem* selectedItem() const;
    QString pluginsDir() const;
    QString themesDir() const;                 ///< M9 stage4: 自定义主题目录
    static QString themeKeyForItem(const MarketItem& it);  ///< M9 stage4: 市场条目 → 主题 key
    void setStatus(const QString& text);
    // M9 stage3: 版本比较与更新检查
    QString installedVersion(const MarketItem& it) const;  ///< 已安装版本（未安装返回空）
    static int compareVersions(const QString& a, const QString& b);  ///< >0: a更新
    int countUpdatable() const;

    MarketplaceRegistry m_registry;
    QList<MarketItem> m_items;              ///< 全量条目（搜索过滤前）
    QListWidget* m_itemList = nullptr;
    QTextBrowser* m_detailView = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnRefresh = nullptr;
    QPushButton* m_btnInstall = nullptr;
    QPushButton* m_btnUninstall = nullptr;
    QPushButton* m_btnOpenDir = nullptr;
    QLabel* m_statusLabel = nullptr;
};

#endif // MARKETPLACEPANEL_H
