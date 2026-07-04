#ifndef CODEFOLDINGMANAGER_H
#define CODEFOLDINGMANAGER_H

#include <QObject>
#include <QList>
#include <QString>
#include <functional>

class QTextEdit;
class QTextCursor;
class QPainter;
class QPoint;

/// @brief 代码折叠管理器
///
/// 职责：扫描折叠区域、切换折叠状态、绘制折叠图标、处理行号区点击。
///
/// 设计说明：
/// - 管理器模式：从 MyTextEdit 抽取折叠相关状态与逻辑，MyTextEdit 委托调用
/// - 回调解耦：findMatchingBracket / updateLineNumberArea 通过 std::function 注入，
///   避免双向依赖（findMatchingBracket 也被括号匹配使用，保留在 MyTextEdit）
/// - 状态封装：FoldRegion/m_foldRegions/m_foldableBlocks/m_foldIconSize 全部内聚到管理器
class CodeFoldingManager : public QObject
{
    Q_OBJECT

public:
    /// P3-M03 子项2: 折叠区域类型（用于行号栏图标颜色区分）
    enum FoldRegionType {
        Brace       = 0,  // {} 配对（默认，最常见）
        Indentation = 1,  // 缩进折叠（基于缩进级别）
        Region      = 2,  // #region / #endregion（C#/VS 风格）
        Comment     = 3   // // {{{ / // }}} 或 // region: / // endregion: 自定义折叠标记
    };

    /// 折叠区域描述
    struct FoldRegion {
        int startBlock;    // 折叠起始块号（包含 { 的行）
        int endBlock;      // 折叠结束块号（包含 } 的行）
        bool folded;       // 是否已折叠
        FoldRegionType type = Brace;  // P3-M03 子项2: 折叠区域类型
    };

    explicit CodeFoldingManager(QTextEdit* editor, QObject* parent = nullptr);

    // === 回调注入（解耦 MyTextEdit 具体方法） ===

    /// 设置括号匹配查询回调（scanFoldRegions 依赖）
    /// @param cb 接收 position，返回匹配括号位置（-1 表示未找到）
    void setFindMatchingBracketCallback(std::function<int(int)> cb) {
        m_findMatchingBracket = std::move(cb);
    }

    /// 设置行号区/视口刷新回调（applyFoldState 依赖）
    void setRequestUpdateCallback(std::function<void()> cb) {
        m_requestUpdate = std::move(cb);
    }

    // === 折叠操作 ===

    /// 扫描整个文档，重建折叠区域列表（基于 { } 配对）
    void scanFoldRegions();

    /// 切换指定块的折叠状态
    void toggleFold(int blockNumber);

    /// 查询某块是否为折叠区起点
    bool isFoldable(int blockNumber) const;

    /// 查询某块是否已折叠
    bool isFolded(int blockNumber) const;

    /// P3-M03 子项2: 查询指定块的折叠区域类型（用于行号栏图标颜色区分）
    /// 若不是折叠区起点，返回 Brace（默认）
    FoldRegionType foldRegionType(int blockNumber) const;

    // === 行号区交互 ===

    /// 处理行号区点击（判断是否点击折叠图标）
    /// @param pos 点击位置（相对于行号区）
    /// @param areaWidth 行号区宽度
    /// @param cursor 点击位置对应的文本光标
    void onLineNumberAreaClicked(const QPoint& pos, int areaWidth,
                                 const QTextCursor& cursor);

    /// 绘制折叠图标（由 lineNumberAreaPaintEvent 调用）
    /// @param painter 画家对象
    /// @param blockNumber 块号
    /// @param iconX 图标 X 坐标
    /// @param top 块顶部 Y 坐标
    /// @param fontHeight 字体高度
    /// @param editorSize 编辑器字号（用于图标字号派生）
    void paintFoldIcon(QPainter& painter, int blockNumber, int iconX,
                       int top, int fontHeight, int editorSize);

    /// 折叠图标尺寸
    int foldIconSize() const { return m_foldIconSize; }

    /// P0 C02-2: 已折叠区域数量（作为行号栏缓存失效签名）
    int foldedBlockCount() const;

signals:
    /// P3-M03 子项2: 折叠状态变化信号（折叠/展开时通知外部刷新视图）
    /// @param blockNumber 折叠区域起始块号
    /// @param folded 新的折叠状态（true=已折叠，false=已展开）
    /// @param type 折叠区域类型（Brace/Indentation/Region/Comment）
    void foldStateChanged(int blockNumber, bool folded, int type);

private:
    /// 按 startBlock 查找折叠区域
    FoldRegion* findFoldRegion(int blockNumber);
    const FoldRegion* findFoldRegionConst(int blockNumber) const;  // P3-M03 子项2: const 查询

    /// 应用折叠状态到文档（隐藏/显示块）
    void applyFoldState();

    /// P3-M03 子项2: 扫描 #region / #endregion 标记（C#/VS 风格）
    void scanRegionMarkers();

    /// P3-M03 子项2: 扫描自定义折叠标记 // {{{ / // }}} 和 // region: / // endregion:
    void scanCustomMarkers();

private:
    QTextEdit*                  m_editor;
    std::function<int(int)>     m_findMatchingBracket;
    std::function<void()>       m_requestUpdate;
    QList<FoldRegion>           m_foldRegions;
    QList<int>                  m_foldableBlocks;
    int                         m_foldIconSize = 14;
};

#endif // CODEFOLDINGMANAGER_H
