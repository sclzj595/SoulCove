#ifndef MYTEXTEDIT_H
#define MYTEXTEDIT_H

#include "interfaces/editor/IEditorEdit.h"
#include "interfaces/editor/ICompleter.h"
#include "core/vcs/GitBlameReader.h"  // P2-H03 子项3: GitBlameLine
#include "core/editor/SpellChecker.h"  // P3-M03 子项5: 拼写检查

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
#include <QHash>
#include <QSet>
#include <QVariantMap>
#include <QPair>

// 行号区域显示类（前向声明，避免头文件耦合）
class LineNumberArea;
class TextCompleter;
class ILineNumber;
class CodeSyntaxHighlighter;
class CodeFoldingManager;
class MinimapRenderer;

// R1: LspHighlightState 从公共头文件引入（不再前向声明，保证类型安全）
#include "core/lsp/LspTypes.h"

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

    /// J1: 静默设置文本 — 抑制 textChanged 引发的补全弹窗触发
    /// 用于打开文件、跳转定义等程序化文本加载场景，避免编辑器卡顿
    void setPlainTextSilently(const QString& text);

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

    /// P1-3: 设置 LSP 高亮状态（双轨高亮降级）
    /// LSP Ready 时禁用启发式兜底，Disconnected/Initializing 时启用
    /// R3: 实现 IEditorEdit 接口方法
    void setLspHighlightState(LspHighlightState state) override;

    // ========== 括号匹配 ==========
    /// 高亮当前光标所在位置的配对括号
    void highlightMatchingBracket();
    /// 查找指定位置的括号的配对位置（返回-1表示未找到）
    int findMatchingBracket(int position) const;
    /// 判断字符是否为括号字符
    static bool isBracketChar(const QChar& ch);
    /// 返回括号的配对字符
    static QChar matchingBracket(const QChar& ch);

    // ========== 迷你地图（委托给 MinimapRenderer） ==========
    /// 切换迷你地图显隐
    void toggleMinimap(bool visible);
    /// 迷你地图是否可见
    bool isMinimapVisible() const;
    /// 获取迷你地图渲染器（供 eventFilter/resizeEvent 委托调用）
    MinimapRenderer* minimapRenderer() const { return m_minimapRenderer; }

    // ========== M8: LSP 诊断覆盖层 ==========
    /// 设置 LSP 诊断信息列表（由外部 LspClient 驱动更新）
    void setDiagnostics(const QList<LspDiagnosticOverlay>& diagnostics);
    /// 清除所有诊断标记
    void clearDiagnostics();
    /// 获取当前行号对应的诊断信息（用于 tooltip 显示）
    QList<LspDiagnosticOverlay> diagnosticsForLine(int line) const;
    /// 请求 LSP 补全（Ctrl+Space 触发）
    void requestLspCompletion();

    // ========== P2-H03 子项3: Git blame 行级标注 ==========
    /// 设置 Git blame 信息（由 Widget 在文件打开/保存后异步注入）
    /// @param info GitBlameLine 列表（按行号）
    void setGitBlameInfo(const QList<GitBlameLine>& info);
    /// 清除 Git blame 标注
    void clearGitBlameInfo();
    /// 设置 Git 标注可见性开关（视图菜单切换）
    void setGitBlameVisible(bool visible);
    /// Git 标注是否可见
    bool isGitBlameVisible() const { return m_gitBlameVisible; }

    /// 生成 Doxygen 注释并插入到光标上方（委托给 DoxygenGenerator）
    void insertDoxygenComment();

    // ========== P3-M04 子项3: 断点（行号栏切换 + 红圆点绘制）==========
    /// @brief 切换指定行的断点状态（存在则移除，不存在则添加）
    /// @param line 行号（1-based）
    void toggleBreakpoint(int line);

    /// @brief 直接设置断点行集合（批量覆盖，由 Widget 在文件加载/调试启动时注入）
    /// @param lines 行号集合（1-based）
    void setBreakpoints(const QSet<int>& lines);

    /// @brief 获取当前编辑器的断点行集合（1-based）
    const QSet<int>& breakpointLines() const { return m_breakpointLines; }

    /// @brief 清除所有断点（不发射 breakpointToggled 信号）
    void clearBreakpoints();

    // ========== P3-M03 子项1: EOL（行尾）配置 ==========
    /// 设置当前文档的行尾类型（"LF" / "CRLF" / "CR"）
    /// 切换时会转换文档中所有行尾到目标类型
    void setEolMode(const QString& eol);
    /// 获取当前文档的行尾类型
    QString eolMode() const { return m_eolMode; }

    // ========== P3-M03 子项3: 列选择模式 ==========
    /// 开启/关闭列选择模式
    void setColumnSelectionMode(bool enabled);
    /// 当前是否处于列选择模式
    bool columnSelectionMode() const { return m_columnSelectionMode; }

    // ========== P3-M03 子项4: .editorconfig 缩进设置（按文件覆盖）==========
    /// 直接设置缩进配置（不通过 ConfigManager，用于 .editorconfig 按文件应用）
    /// @param tabSize 缩进字符数
    /// @param useSpaces true=空格缩进, false=Tab缩进
    void setIndentConfig(int tabSize, bool useSpaces);

signals:
    void cursorPositionChangedSignal();
    void textChangedForCompletion();
    /// M8: 请求 LSP 补全信号（Ctrl+Space 触发，由 Widget 层连接到 LspClient）
    void lspCompletionRequested(int line, int column);
    /// M8: 文档已打开信号（通知 LspClient 发送 didOpen）
    void lspDocumentOpened(const QString& filePath, const QString& content, const QString& langId);
    /// M8: 文档内容变更信号（通知 LspClient 发送 didChange）
    void lspDocumentChanged(const QString& content);
    /// L16: 鼠标悬停请求 LSP hover（300ms 防抖，由 Widget 连接到 LspClient）
    void lspHoverRequested(int line, int column);
    /// 悬停中止信号（鼠标离开编辑器 / 按键 / 失焦时发射，Widget 用于隐藏弹窗）
    void hoverAborted();
    /// Ctrl+左键单击请求 LSP 跳转定义（与 F12 等效，由 Widget 连接到 LspManager）
    void lspGotoDefinitionRequested();
    /// C03-6: Ctrl+Alt+左键单击请求定义预览（不跳转，悬浮显示定义代码片段）
    /// @param line 光标行（1-based）
    /// @param col  光标列（1-based）
    void definitionPreviewRequested(int line, int col);
    /// Bug1: Ctrl+左键单击 #include 头文件路径 → 请求打开该头文件
    /// @param includeText 原始 include 文本（含尖括号/引号，如 "<QPushButton>" 或 "\"myheader.h\""）
    /// @param isSystem 是否为系统头文件 <...>（true）或本地头文件 "..."（false）
    void includeOpenRequested(const QString& includeText, bool isSystem);
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

    /// @brief P2-H01: 请求在终端中运行选中的代码（右键菜单「在终端运行」）
    /// @param code 选中的代码文本
    void runInTerminalRequested(const QString& code);

    /// P3-M03 子项1: 文档行尾类型变化信号（切换 EOL 时发射，供状态栏更新显示）
    void eolModeChanged(const QString& eol);

    /// @brief P3-M04 子项3: 断点切换信号
    /// 用户在行号栏空白区域点击时发射；Widget 接收并同步到 DebugManager
    /// @param line 行号（1-based）
    /// @param enabled 切换后该行断点是否启用（true=已添加, false=已移除）
    void breakpointToggled(int line, bool enabled);

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
    void mouseReleaseEvent(QMouseEvent* event) override;  // P3-M03 子项3: 列选拖拽结束
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    ICompleter* m_completer = nullptr;   // 接口指针，解耦具体实现
    CodeSyntaxHighlighter* m_syntaxHighlighter = nullptr;  // 语法高亮器
    QStringList m_wordList;

    QTimer m_completionTimer;
    bool m_ignoreNextUpdate = false;
    bool m_suppressCompletion = false;  // J1: 程序化文本加载时抑制补全弹窗

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

    /// Bug1: 检测光标所在行是否为 #include 指令，并提取头文件路径
    /// @param cursor 点击位置的光标
    /// @param includeText 输出：原始 include 文本（含 <>/""）
    /// @param isSystem 输出：true=系统头文件 <...>, false=本地头文件 "..."
    /// @return 若当前行是 #include 且光标位于路径上返回 true
    bool extractIncludeAtCursor(const QTextCursor& cursor, QString& includeText, bool& isSystem);

    // ========== 迷你地图 ==========
    // 状态与子控件已迁入 MinimapRenderer（widget/image/timer/visible）
    MinimapRenderer* m_minimapRenderer = nullptr;  // 迷你地图渲染器（拥有 widget/image/timer）

    // ========== M8: LSP 诊断覆盖层 ==========
    QList<LspDiagnosticOverlay> m_diagnostics;  // 当前文件的 LSP 诊断列表

    // ========== P2-H03 子项3: Git blame 行级标注 ==========
    QHash<int, GitBlameLine> m_gitBlameInfo;  // 行号(1-based) → blame 信息
    bool m_gitBlameVisible = false;            // 标注可见性开关（默认关闭）

    // ========== P3-M04 子项3: 断点行集合（1-based 行号）==========
    QSet<int> m_breakpointLines;

    // ========== L16: 鼠标悬停 LSP hover ==========
    QTimer m_hoverTimer;              // 悬停防抖定时器（300ms）
    QPoint m_lastHoverPos;            // 上次鼠标位置（用于判断是否移动）
    QPoint m_lastHoverGlobalPos;      // 上次悬停的全局坐标（用于弹窗定位）
    int m_hoverSeq = 0;               // 悬停请求序号（用于 stale 响应检测）

public:
    /// 获取上次悬停的全局坐标（Widget 用于弹窗定位）
    QPoint lastHoverGlobalPos() const { return m_lastHoverGlobalPos; }
    /// 获取当前悬停请求序号（Widget 用于 stale 响应检测）
    int hoverSeq() const { return m_hoverSeq; }

private:

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

    // ========== P0 C02-2: 行号栏渲染缓存 ==========
    // 缓存行号栏完整渲染结果（背景+行号+折叠图标），当滚动位置/宽度/文档版本未变时复用，
    // 避免水平滚动、光标移动（不引起垂直滚动）等场景下重复渲染
    QImage m_lineNumberCache;
    int m_lnCacheScroll = -1;       // 缓存对应的垂直滚动值
    int m_lnCacheWidth = -1;        // 缓存对应的行号栏宽度
    int m_lnCacheDocRev = -1;       // 缓存对应的文档 revision
    int m_lnCacheFoldSig = -1;      // 缓存对应的折叠状态签名（折叠变化时失效）
    int m_lnCacheBpSig = -1;        // P3-M04 子项3: 缓存对应的断点签名（断点变化时失效）

    // ========== C02-5: 编辑器增量渲染 ==========
    // 记录上次绘制的脏区域和滚动位置，用于诊断绘制循环按可视行范围裁剪
    QRect m_lastPaintRect;        // 上次绘制的脏区域
    int m_lastPaintScrollY = -1;  // 上次绘制时的垂直滚动位置

    // ========== P3-M03 子项1: EOL（行尾）配置 ==========
    // Qt 文档内部使用单个 '\n' 作为段落分隔符，m_eolMode 仅作为元信息记录
    // 保存时由 FileOperator::convertEol() 按此设置统一行尾
    QString m_eolMode = QStringLiteral("LF");

    // ========== P3-M03 子项3: 列选择模式 ==========
    bool m_columnSelectionMode = false;            // 列选模式开关
    QList<QTextCursor> m_columnCursors;            // 列选模式的多个光标（每行一个，相同列范围）
    QPoint m_columnSelectStart;                    // 列选拖拽起始位置
    int m_columnSelectStartCol = 0;                // 列选起始列
    bool m_columnDragging = false;                 // 是否正在列选拖拽

    /// P3-M03 子项3: 计算指定像素位置在文档中的列号（基于字符宽度）
    int columnAtPosition(const QPoint& pos) const;
    /// P3-M03 子项3: 重建列选多光标（按起止行列范围，每行一个光标）
    void rebuildColumnCursors(int startLine, int startCol, int endLine, int endCol);
    /// P3-M03 子项3: 清除列选光标（退出列选模式或取消选择时调用）
    void clearColumnCursors();

    // ========== P3-M03 子项5: 拼写检查 ==========
    QList<SpellMisspelledRange> m_spellErrors;     // 缓存的拼写错误区间
    QTimer m_spellCheckTimer;                       // 防抖定时器（500ms）
    /// 执行拼写检查并刷新视口（由防抖定时器触发）
    void performSpellCheck();
    /// 查找指定文档位置所在的拼写错误区间（无则返回 nullptr）
    const SpellMisspelledRange* spellErrorAt(int docPosition) const;
};

#endif // MYTEXTEDIT_H
