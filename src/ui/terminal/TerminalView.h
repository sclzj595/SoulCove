#ifndef TERMINALVIEW_H
#define TERMINALVIEW_H

#include <QTextEdit>
#include <QStringList>
#include <QTextCharFormat>
#include <QByteArray>
#include <QColor>
#include <QDateTime>  // P1-2: 点击时间追踪

/// @brief 统一终端视图组件（输入输出合一）
///
/// 核心设计：
/// - 单个 QTextEdit 同时负责显示和输入（非只读）
/// - 通过 m_inputMarker 追踪"输入起始位置"，光标不能移到它之前
/// - 用户键入的字符直接插入到文档末尾（模拟真实终端回显）
/// - Enter 键时提取输入行发送给后端进程
/// - 支持 Up/Down 历史导航、Backspace 行内删除、Ctrl+C 中断等
class TerminalView : public QTextEdit
{
    Q_OBJECT

public:
    explicit TerminalView(QWidget* parent = nullptr);
    ~TerminalView() override = default;

    /// @brief 追加进程输出数据（ANSI 解析渲染）
    void appendOutput(const QByteArray& data);

    /// @brief 追加纯文本输出（带颜色）
    void appendPlainText(const QString& text, const QColor& color = QColor());

    /// @brief 显示欢迎/状态信息
    void showWelcome(const QString& text, const QColor& color);

    /// @brief 设置输入标记位置（新 prompt 开始处）
    void setInputMarker();

    /// @brief 获取当前输入行的内容（从 inputMarker 到文档末尾）
    QString currentInputLine() const;

    /// @brief 清除当前输入行内容（用于历史替换）
    void clearCurrentInput();

    /// @brief 清屏
    void clearScreen();

    /// @brief 设置字体
    void setTerminalFont(const QFont& font);

    /// @brief 设置前景色（文字颜色）
    void setForegroundColor(const QColor& color);

    /// @brief 设置背景色
    void setBackgroundColor(const QColor& color);

    /// @brief 设置光标颜色
    void setCursorColor(const QColor& color);

signals:
    /// @brief 用户按下 Enter 提交命令
    void commandSubmitted(const QString& command);

    /// @brief 用户按下 Ctrl+C（中断信号请求）
    void interruptRequested();

public slots:
    /// @brief 将光标移动到输入行末尾（获得焦点时调用）
    void focusInputEnd();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;  // P1-2: 双击选中单词

private:
    /// @brief 三击选中整行（P1-2：在双击事件中检测三击）
    void handleTripleClick(QMouseEvent* event);   // P1-2: 三击选中整行

private:
    // 输入区域管理
    int m_inputMarker = 0;           // 文档中"输入起始"的位置
    bool m_isUserTyping = false;     // 是否正在输入模式

    // P1-2: 鼠标点击追踪（用于三击检测）
    QDateTime m_lastClickTime;       // 上次点击时间
    int m_clickCount = 0;            // 点击计数

    // 命令历史
    QStringList m_commandHistory;
    int m_historyIndex = -1;         // -1=当前输入, >=0=浏览历史
    QString m_tempInput;             // 浏览历史前保存的当前输入

    // ANSI 解析状态
    QByteArray m_ansiBuffer;         // 不完整的ANSI序列缓冲区
    QTextCharFormat m_currentFormat;// 当前ANSI渲染格式

    // Prompt 追踪（用于 Ctrl+L 清屏后恢复）
    QString m_lastPrompt;            // 最后一次检测到的 shell prompt 文本

    // 内部方法
    void handleKeyPress(QKeyEvent* event);
    void navigateHistory(int direction);  // -1=Up, 1=Down
    void submitCurrentLine();
    void ensureCursorInInputArea();

    // ANSI 解析
    void parseAnsiData(const QByteArray& data);
    void applySgrFormat(const QStringList& params);
    static QColor ansiColorToQColor(int code, bool foreground = true);

    // P2-H01: 终端输出语法高亮（命令/错误/日志着色）
    // 在 parseAnsiData 插入文本后调用，对刚插入的文本范围应用 shell 模式匹配着色
    // 不破坏 ANSI 已有着色（仅对默认前景色的文本应用高亮）
    // @param text 刚插入的纯文本（用于模式匹配定位）
    // @param startPos 文本在文档中的起始位置
    void highlightShellPatterns(const QString& text, int startPos);

    // 可执行文件颜色高亮（ls/dir 输出中 .exe/.dll 等显示绿色）
    bool isExecutableFile(const QString& text) const;
    static const QStringList s_executableExtensions;
};

#endif // TERMINALVIEW_H
