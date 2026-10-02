#ifndef OUTLINEPANEL_H
#define OUTLINEPANEL_H

#include <QWidget>
#include <QVariantMap>
#include <QHash>
#include <QSet>
#include <QPair>
#include <QList>
#include <QColor>
#include <QFutureWatcher>
#include <QTimer>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QLineEdit;

/// @brief 大纲面板（V1.9: 符号导航，V2.1: 嵌入 ExplorerPanel 内部）
///
/// 职责：显示当前文件的符号大纲（LSP 精确符号 / 离线正则扫描 fallback），
///       点击符号项发射跳转信号。
///
/// 设计说明：
/// - V2.1: 嵌入 ExplorerPanel 内部文件树下方（VSCode 风格），由 ExplorerPanel 控制显隐
/// - 支持符号筛选、右键菜单、折叠状态持久化、线程化扫描
/// - 公共 API 保持向后兼容（updateOutline/clearOutline/updateOutlineFromText/symbolClicked）
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

    /// V2.1 C3: 立即同步文件路径（不触发符号刷新）
    /// 用于 LSP 异步请求期间防止 m_filePath 不同步导致错误跳转
    void resetFilePath(const QString& filePath);

    /// V2.1 M3 修复：持久化折叠状态到磁盘（关机后恢复）
    void saveExpansionStatesToDisk();
    void loadExpansionStatesFromDisk();

    /// V2.1 M6 修复：返回离线大纲支持的文件后缀集合（单一数据源）
    /// 消除 isOutlineSupported / scanSymbolsOffline / onAsyncScanFinished 三处重复
    static QSet<QString> supportedSuffixes();

signals:
    /// @brief 符号被点击 — 参数：文件路径、起始行/列、结束行/列(0-based)
    /// V2.1: 增加 endLine/endCol 支持结构体/类完整作用域选中
    void symbolClicked(const QString& filePath, int line, int col, int endLine, int endCol);
    /// @brief V2.1: 离线扫描完成 — 参数：文件路径、符号数量
    /// ExplorerPanel 监听此信号，扫描完成后自动展开大纲区域
    void scanFinished(const QString& filePath, int symbolCount);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);
    void onFilterChanged(const QString& text);
    void onCustomContextMenuRequested(const QPoint& pos);
    void onAsyncScanFinished();

private:
    /// 递归填充大纲树（解析 LSP documentSymbol QVariantMap 列表）
    void populateOutlineTreeFromList(QTreeWidgetItem* parent, const QList<QVariantMap>& symbols);

    /// 根据 LSP SymbolKind 返回图标字符
    QString symbolIcon(int kind) const;

    /// 根据 LSP SymbolKind 返回着色（V2.0: 符号分类视觉区分）
    QColor symbolColor(int kind) const;

    /// V2.1: 判断是否为 Qt MOC 自动生成的元符号（需过滤）
    /// 过滤列表：staticMetaObject, metaObject, qt_metacast, qt_metacall,
    ///          qt_static_metacall, tr, trUtf8, QPrivateSignal
    static bool isMocGeneratedSymbol(const QString& name);

    /// 从 LSP symbol QVariantMap 中提取起始行/列
    void extractSymbolPosition(const QVariantMap& sym, int& line, int& col) const;

    /// V2.1: 从 LSP symbol QVariantMap 中提取结束行/列（range.end）
    /// 用于结构体/类完整作用域选中
    void extractSymbolEndPosition(const QVariantMap& sym, int& endLine, int& endCol) const;

    /// V2.1: 检测 LSP 返回的是否为扁平结构（SymbolInformation 格式，无 children 字段）
    bool isFlatSymbolList(const QList<QVariantMap>& symbols) const;

    /// V2.1: 将扁平结构（SymbolInformation）转换为嵌套结构（DocumentSymbol）
    /// 根据 containerName 字段构建父子关系
    QList<QVariantMap> buildNestedFromFlat(const QList<QVariantMap>& flatSymbols) const;

    /// 应用筛选文本（模糊匹配过滤符号名）
    void applyFilter(const QString& text);

    /// 递归过滤树节点（隐藏不匹配项，保留匹配项及其父节点）
    bool filterItem(QTreeWidgetItem* item, const QString& text);

    /// 保存当前文件的折叠状态
    void saveExpansionState();

    /// 恢复当前文件的折叠状态
    void restoreExpansionState();

    /// 折叠全部节点
    void collapseAll();

    /// 展开全部节点
    void expandAll();

    /// 启动异步正则扫描（避免大文件阻塞 UI）
    void startAsyncScan(const QString& filePath, const QString& content);

    QTreeWidget* m_tree = nullptr;
    QLabel*      m_hint = nullptr;       // 提示标签（无符号时显示）
    QLineEdit*   m_filter = nullptr;     // 筛选输入框（模糊匹配过滤符号名）
    QString      m_filePath;             // 当前大纲对应的文件路径

    /// 折叠状态持久化：filePath → 展开的符号名集合
    QHash<QString, QSet<QString>> m_expandedStates;

    /// 异步扫描结果监听器
    QFutureWatcher<QList<QPair<QString, int>>>* m_scanWatcher = nullptr;

    /// 当前异步扫描对应的文件路径（防止 stale 结果覆盖新文件）
    QString m_pendingScanFilePath;

    /// V2.1 M5 修复：筛选框防抖定时器（避免大符号树每次按键全量遍历）
    QTimer* m_filterDebounceTimer = nullptr;
    QString m_pendingFilterText;
};

#endif // OUTLINEPANEL_H
