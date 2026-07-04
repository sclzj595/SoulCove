#ifndef MDTOCPANEL_H
#define MDTOCPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

class QTreeWidget;
class QPushButton;

/// @brief Markdown 目录（Table of Contents）面板
///
/// 功能：
/// - 自动提取Markdown文档中的标题结构
/// - 以树形视图展示层级关系（H1-H6）
/// - 点击标题可跳转到编辑器/预览区对应位置
/// - 高亮当前浏览位置对应的标题
/// - 支持折叠/展开子目录（V1.7+ P3-M02 子项1）
/// - 「全部展开」「全部折叠」按钮（P3-M02 子项1）
/// - 按文件路径记忆折叠状态（P3-M02 子项1，最多 20 个文件）
///
/// 使用场景：作为MarkdownMode的左侧或右侧辅助面板
class MdTocPanel : public QWidget
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

    /// @brief 更新目录内容（自动恢复该文件的折叠状态）
    void updateToc(const QList<TocEntry>& entries);

    /// @brief 清空目录
    void clearToc();

    /// @brief 高亮当前活动项（跟随滚动位置）
    void highlightActiveItem(int lineNumber);

    /// @brief 设置当前文件路径（用于按文件记忆折叠状态）
    void setFilePath(const QString& filePath);

    /// @brief 全部展开
    void expandAll();

    /// @brief 全部折叠
    void collapseAll();

signals:
    /// @brief 用户点击了某个标题项，请求跳转
    void tocItemClicked(const QString& anchorId, int lineNumber);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);

    /// @brief 主题切换时刷新 QSS 样式
    void updateTheme();

    /// @brief 节点折叠/展开状态变化时持久化
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);

    /// @brief 「全部展开」按钮
    void onExpandAll();
    /// @brief 「全部折叠」按钮
    void onCollapseAll();

private:
    /// @brief 创建树节点（带图标和样式）
    QTreeWidgetItem* createTocItem(const TocEntry& entry, QTreeWidgetItem* parent = nullptr);

    /// @brief 根据标题级别设置缩进图标
    QIcon iconForLevel(int level) const;

    /// @brief 应用当前主题的 QSS 样式
    void applyStyleSheet();

    /// @brief 收集当前折叠的节点行号列表并持久化
    void saveCollapsedState() const;

    /// @brief 从 ConfigManager 恢复该文件的折叠状态
    void restoreCollapsedState();

    QTreeWidget* m_tree = nullptr;             ///< 内部树控件
    QPushButton* m_btnExpandAll = nullptr;     ///< 全部展开按钮
    QPushButton* m_btnCollapseAll = nullptr;   ///< 全部折叠按钮

    QMap<QTreeWidgetItem*, QString> m_itemAnchorMap;  ///< 节点 → anchorId映射
    QMap<QTreeWidgetItem*, int> m_itemLineMap;       ///< 节点 → 行号映射
    QString m_filePath;                              ///< 当前文件路径（用于按文件记忆状态）
};

#endif // MDTOCPANEL_H
