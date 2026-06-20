#ifndef OUTLINEPANEL_H
#define OUTLINEPANEL_H

#include <QWidget>
#include <QVariantMap>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

/// @brief 大纲面板（V1.9: 符号导航）
///
/// 职责：显示当前文件的符号大纲（LSP 精确符号 / 离线正则扫描 fallback），
///       点击符号项发射跳转信号。
///
/// 设计说明：
/// - 从 SideBar 抽取，零外部依赖（不依赖工作区/文件树状态）
/// - 面板模式：自含 UI（QTreeWidget + 提示 QLabel）与逻辑（LSP 解析 + 正则扫描）
/// - SideBar 持有并转发调用，Widget 层 API 不变
class OutlinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget* parent = nullptr);

    /// @brief 更新大纲（LSP documentSymbol 响应）
    /// @param filePath 当前文件路径
    /// @param symbols LSP documentSymbol 响应（QVariantMap 列表，含 name/kind/range/children）
    void updateOutline(const QString& filePath, const QList<QVariantMap>& symbols);

    /// @brief 清空大纲（文件关闭时调用）
    void clearOutline();

    /// @brief 离线正则扫描符号并更新大纲（无 LSP 时的 fallback）
    /// @param filePath 当前文件路径
    /// @param content 文件内容
    void updateOutlineFromText(const QString& filePath, const QString& content);

signals:
    /// @brief 符号被点击 — 参数：文件路径、行号(0-based)、列号(0-based)
    void symbolClicked(const QString& filePath, int line, int col);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);

private:
    /// 递归填充大纲树（解析 LSP documentSymbol QVariantMap 列表）
    void populateOutlineTreeFromList(QTreeWidgetItem* parent, const QList<QVariantMap>& symbols);

    /// 根据 LSP SymbolKind 返回图标字符
    QString symbolIcon(int kind) const;

    /// 从 LSP symbol QVariantMap 中提取起始行/列
    void extractSymbolPosition(const QVariantMap& sym, int& line, int& col) const;

    QTreeWidget* m_tree = nullptr;
    QLabel*      m_hint = nullptr;       // 提示标签（无符号时显示）
    QString      m_filePath;             // 当前大纲对应的文件路径
};

#endif // OUTLINEPANEL_H
