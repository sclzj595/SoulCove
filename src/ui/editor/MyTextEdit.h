#ifndef MYTEXTEDIT_H
#define MYTEXTEDIT_H

#include "interfaces/editor/IEditorEdit.h"
#include "interfaces/editor/ICompleter.h"

#include <QTextEdit>
#include <QWheelEvent>
#include <QDebug>
#include <QInputMethodEvent>
#include <QTimer>
#include <QPaintEvent>
#include <QTextBlock>
#include <QScrollBar>
#include <QImage>
#include <QTimer>
#include <QList>
#include <QVariantMap>
#include <QPair>

// 行号区域显示类（前向声明，避免头文件耦合）
class LineNumberArea;
class TextCompleter;
class ILineNumber;
class CodeSyntaxHighlighter;
class CodeFoldingManager;

/// @brief LSP 诊断信息（轻量结构，用于编辑器内联显示）
struct LspDiagnosticOverlay {
    int startLine;
    int startCol;
    int endLine;
    int endCol;
    enum Severity { Error = 1, Warning = 2, Info = 3, Hint = 4 } severity = Error;
    QString message;
};

/// @brief 自定义文本编辑器
/// 实现IEditorEdit接口，提供代码/文本编辑核心能力
/// 包含行号显示、当前行高亮、智能补全集成、字体缩放等功能
/// 补全器通过ICompleter接口访问，不依赖具体实现类
class MyTextEdit : public QTextEdit, public IEditorEdit
{
    Q_OBJECT

private:
    void showCompleter();
    void cursorPositionChangedInternal();
    void updateCompletion();

private slots:
    void handleTextChanged();

public:
    explicit MyTextEdit(QWidget* parent = nullptr);

    // ========== IEditorEdit 接口实现 ==========
    QString toPlainText() const override { return QTextEdit::toPlainText(); }
    void setPlainText(const QString& text) override { QTextEdit::setPlainText(text); }
    void append(const QString& text) override { QTextEdit::append(text); }
    void clear() override { QTextEdit::clear(); }
    QTextCursor textCursor() const override { return QTextEdit::textCursor(); }
    void setTextCursor(const QTextCursor& cursor) override { QTextEdit::setTextCursor(cursor); }

    int cursorPosition() const override { return textCursor().position(); }
    int currentLine() const override { return textCursor().blockNumber() + 1; }
    int currentColumn() const override { return textCursor().columnNumber() + 1; }

    void setFontSize(int size) override;
    int fontSize() const override;
    void fontZoomIn() override;
    void fontZoomOut() override;

    void setLineNumberVisible(bool visible) override;
    bool isLineNumberVisible() const override;
    void updateLineNumberArea() override;

    bool isModified() const override { return document()->isModified(); }
    void setModified(bool modified) override { document()->setModified(modified); }

    QWidget* asWidget() override { return this; }

    // ========== 行号相关 ==========
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

    // ========== 代码折叠（委托给 CodeFoldingManager） ==========
    /// 切换指定行的折叠状态（由行号区点击触发）
    /// @param blockNumber 要折叠/展开的块号（0-based）
    void toggleFold(int blockNumber);

    /// 判断指定块是否可折叠（该块包含 { 且有匹配的 }）
    bool isFoldable(int blockNumber) const;

    /// 判断指定块当前是否处于折叠状态
    bool isFolded(int blockNumber) const;

    /// 行号区鼠标点击处理（判断是否点击在折叠图标上）
    void lineNumberAreaClicked(const QPoint& pos, int areaWidth);

    /// 获取代码折叠管理器（供行号区绘制调用 paintFoldIcon）
    CodeFoldingManager* foldingManager() const { return m_foldingManager; }

    // ========== 补全相关（通过ICompleter接口）==========
    void setCompleter(ICompleter* completer);
    void updateWordList();
    QStringList getWordList() const { return m_wordList; }
    ICompleter* completer() const { return m_completer; }  // 返回接口指针

    // ========== 语法高亮 ==========
    /// 根据文件后缀启用对应语言的语法高亮
    void enableSyntaxHighlighting(const QString& fileSuffix);

    /// 禁用语法高亮
    void disableSyntaxHighlighting();

    /// 主题变更时更新语法高亮配色
    void updateSyntaxHighlightColors();

    // ========== L12-L14: LSP 语义高亮 ==========
    /// 设置 LSP 语义符号（转发给 CodeSyntaxHighlighter，触发语义重高亮）
    /// @param symbols LSP documentSymbol 响应的原始 QVariantMap 列表
    void setSemanticSymbols(const QList<QVariantMap>& symbols);

    /// 清除语义符号（文件关闭/切换时调用）
    void clearSemanticSymbols();

    /// 设置外部符号（来自 #include/import 的本地头文件符号，转发给高亮器）
    /// @param symbols (符号名, 语义角色) 列表，角色与 colorForRole 一致
    void setExternalSymbols(const QList<QPair<QString, QString>>& symbols);

    // ========== 括号匹配 ==========
    /// 高亮当前光标所在位置的配对括号
    void highlightMatchingBracket();
    /// 查找指定位置的括号的配对位置（返回-1表示未找到）
    int findMatchingBracket(int position) const;
    /// 判断字符是否为括号字符
    static bool isBracketChar(const QChar& ch);
    /// 返回括号的配对字符
    static QChar matchingBracket(const QChar& ch);

    // ========== 迷你地图 (M7) ==========
    /// 更新缩略图
    void updateMinimap();
    /// 绘制视口指示器（由updateMinimap和滚动时调用）
    void drawMinimapViewport(QPainter& painter, int w, int h, double scaleY, qreal visibleHeight);
    /// 切换迷你地图显隐
    void toggleMinimap(bool visible);
    /// 迷你地图是否可见
    bool isMinimapVisible() const { return m_minimapVisible; }
    /// 迷你地图绘制事件处理
    void paintMinimapEvent(QPaintEvent* event);
    /// 迷你地图点击事件处理
    void onMinimapClicked(QMouseEvent* event);

    // ========== M8: LSP 诊断覆盖层 ==========
    /// 设置 LSP 诊断信息列表（由外部 LspClient 驱动更新）
    void setDiagnostics(const QList<LspDiagnosticOverlay>& diagnostics);
    /// 清除所有诊断标记
    void clearDiagnostics();
    /// 获取当前行号对应的诊断信息（用于 tooltip 显示）
    QList<LspDiagnosticOverlay> diagnosticsForLine(int line) const;
    /// 请求 LSP 补全（Ctrl+Space 触发）
    void requestLspCompletion();

    /// 生成 Doxygen 注释并插入到光标上方（委托给 DoxygenGenerator）
    void insertDoxygenComment();

signals:
    void cursorPositionChangedSignal();
    void textChangedForCompletion();
    /// M8: 请求 LSP 补全信号（Ctrl+Space 触发，由 Widget 层连接到 LspClient）
    void lspCompletionRequested(int line, int column);
    /// M8: 文档已打开信号（通知 LspClient 发送 didOpen）
    void lspDocumentOpened(const QString& filePath, const QString& content, const QString& langId);
    /// M8: 文档内容变更信号（通知 LspClient 发送 didChange）
    void lspDocumentChanged(const QString& content);
    /// L16: 鼠标悬停请求 LSP hover（500ms 防抖，由 Widget 连接到 LspClient）
    void lspHoverRequested(int line, int column);
    /// Ctrl+左键单击请求 LSP 跳转定义（与 F12 等效，由 Widget 连接到 LspManager）
    void lspGotoDefinitionRequested();
    /// 请求保存信号（Ctrl+S 触发，由 Widget 层连接到保存逻辑）
    void requestSave();

    /// @brief 请求格式化文档 (右键菜单/快捷键) — Widget 层处理 (需要文件路径)
    void formatDocumentRequested();

    /// @brief 请求在编辑器中查找 (Ctrl+F)
    void findRequested();

    /// @brief 请求在编辑器中替换 (Ctrl+H)
    void replaceRequested();

    /// @brief 字体大小变化信号（Ctrl+滚轮/设置页/快捷键触发时发射）
    /// 外部（Widget）监听此信号同步到 ConfigManager 和其他编辑器
    void fontSizeChanged(int size);

    /// @brief 请求复制当前文件路径到剪贴板（右键菜单）
    void copyFilePathRequested();

    /// @brief 请求在系统文件管理器中打开当前文件所在目录（右键菜单）
    void openInFolderRequested();

    /// @brief 请求切换行注释（右键菜单 Ctrl+/）
    void toggleLineCommentRequested();

    /// @brief 请求转换选中文本为大写（右键菜单）
    void toUpperCaseRequested();

    /// @brief 请求转换选中文本为小写（右键菜单）
    void toLowerCaseRequested();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    ICompleter* m_completer = nullptr;   // 接口指针，解耦具体实现
    CodeSyntaxHighlighter* m_syntaxHighlighter = nullptr;  // 语法高亮器
    QStringList m_wordList;

    QTimer m_completionTimer;
    bool m_ignoreNextUpdate = false;

    // 行号
    ILineNumber* lineNumberArea;
    bool lineNumersVisible = true;       // 成员声明处初始化（WARN-5修复）

    // 括号匹配高亮
    int m_matchStartPos = -1;           // 匹配的起始括号位置
    int m_matchEndPos = -1;             // 匹配的结束括号位置
    QList<QTextEdit::ExtraSelection> m_bracketSelections;  // 括号高亮选择集

    // ========== T17: 多光标编辑 ==========
    QList<QTextCursor> m_secondaryCursors;  // 次级光标列表
    QList<QTextEdit::ExtraSelection> m_secondarySelections;  // 次级光标视觉选择集
    bool m_multiCursorMode = false;          // 是否处于多光标模式

    void updateSecondaryCursorDisplay();     // 更新次级光标视觉显示
    void applyToAllCursors(QChar ch);        // 向所有光标插入字符
    void backspaceAllCursors();              // 所有光标执行 backspace
    void deleteAllCursors();                 // 所有光标执行 delete
    void moveAllCursors(QTextCursor::MoveOperation op, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);  // 移动所有光标
    void clearSecondaryCursors();            // 清除所有次级光标

    // ========== 迷你地图 (M7) ==========
    QWidget*  m_minimapWidget = nullptr;     // 缩略图画布
    QImage*   m_minimapImage = nullptr;      // 缓存的渲染图像
    bool      m_minimapVisible = true;       // 是否显示迷你地图
    QTimer    m_minimapUpdateTimer;           // 延迟更新定时器（避免频繁重绘）

    // ========== M8: LSP 诊断覆盖层 ==========
    QList<LspDiagnosticOverlay> m_diagnostics;  // 当前文件的 LSP 诊断列表

    // ========== L16: 鼠标悬停 LSP hover ==========
    QTimer m_hoverTimer;              // 悬停防抖定时器（500ms）
    QPoint m_lastHoverPos;            // 上次鼠标位置（用于判断是否移动）

    // ========== 自动缩进配置 ==========
    int     m_tabSize = 4;              // 缩进大小（空格数）
    bool    m_useSpaces = true;         // true=空格缩进, false=Tab缩进
    void    loadIndentConfig();          // 从 ConfigManager 加载缩进配置
    QString currentLineIndent() const;   // 获取当前行的前导空白
    void    insertIndent();              // 在光标处插入缩进（空格或Tab）

    // ========== 右键菜单 / Doxygen ==========
    // 注：detectFunctionSignature 已迁入 DoxygenGenerator，insertDoxygenComment 委托调用

    // ========== 代码折叠 ==========
    // 注：FoldRegion/m_foldRegions/m_foldableBlocks/m_foldIconSize/scanFoldRegions/
    //     findFoldRegion/applyFoldState 已迁入 CodeFoldingManager
    CodeFoldingManager* m_foldingManager = nullptr;  // 代码折叠管理器（拥有折叠状态）
};

#endif // MYTEXTEDIT_H
