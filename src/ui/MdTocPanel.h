#ifndef MDTOCPANEL_H
#define MDTOCPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

/// @brief Markdown 目录（Table of Contents）面板
///
/// 功能：
/// - 自动提取Markdown文档中的标题结构
/// - 以树形视图展示层级关系（H1-H6）
/// - 点击标题可跳转到编辑器/预览区对应位置
/// - 高亮当前浏览位置对应的标题
/// - 支持折叠/展开子目录
///
/// 使用场景：作为MarkdownMode的左侧或右侧辅助面板
class MdTocPanel : public QTreeWidget
{
    Q_OBJECT

public:
    explicit MdTocPanel(QWidget* parent = nullptr);

    /// @brief 标题项数据结构
    struct TocEntry {
        int level = 0;          ///< 标题级别 (1-6)
        QString title;           ///< 标题文本
        QString anchorId;        ///< HTML anchor ID
        int lineNumber = 0;      ///< 在编辑器中的行号
    };

    /// @brief 更新目录内容
    void updateToc(const QList<TocEntry>& entries);

    /// @brief 清空目录
    void clearToc();

    /// @brief 高亮当前活动项（跟随滚动位置）
    void highlightActiveItem(int lineNumber);

signals:
    /// @brief 用户点击了某个标题项，请求跳转
    void tocItemClicked(const QString& anchorId, int lineNumber);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);

    /// @brief 主题切换时刷新 QSS 样式
    void updateTheme();

private:
    /// @brief 创建树节点（带图标和样式）
    QTreeWidgetItem* createTocItem(const TocEntry& entry, QTreeWidgetItem* parent = nullptr);

    /// @brief 根据标题级别设置缩进图标
    QIcon iconForLevel(int level) const;

    /// @brief 应用当前主题的 QSS 样式
    void applyStyleSheet();

    QMap<QTreeWidgetItem*, QString> m_itemAnchorMap;  ///< 节点 → anchorId映射
    QMap<QTreeWidgetItem*, int> m_itemLineMap;       ///< 节点 → 行号映射
};

#endif // MDTOCPANEL_H
