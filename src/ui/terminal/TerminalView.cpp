#include "ui/terminal/TerminalView.h"
#include "core/config/ThemeManager.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QRegularExpression>
#include <QPalette>
#include <QApplication>  // P1-2: 剪贴板操作
#include <QClipboard>    // P1-2: 剪贴板
#include <QDateTime>     // P1-2: 点击时间追踪
#include <QList>
#include <algorithm>     // P2-H01: std::sort for shell pattern matching

// 可执行文件扩展名列表（ls/dir 输出中高亮为绿色）
const QStringList TerminalView::s_executableExtensions = {
    QStringLiteral(".exe"), QStringLiteral(".dll"), QStringLiteral(".bat"),
    QStringLiteral(".cmd"),  QStringLiteral(".com"), QStringLiteral(".msi"),
    QStringLiteral(".ps1"),  QStringLiteral(".vbs"), QStringLiteral(".js"),
    QStringLiteral(".py"),   QStringLiteral(".sh")
};

// ============================================================
// 构造 / 析构
// ============================================================

TerminalView::TerminalView(QWidget* parent)
    : QTextEdit(parent)
    , m_inputMarker(0)     // 初始化为0，首次 appendOutput/showWelcome 时会更新
{
    setObjectName(QStringLiteral("terminalView"));

    // 关键：不设为只读！用户可直接在输出区域输入
    setAcceptRichText(true);
    setLineWrapMode(QTextEdit::NoWrap);

    // 安装等宽字体
    QFont font(QStringLiteral("Consolas"), 11);
    font.setStyleHint(QFont::Monospace);
    setFont(font);

    // 确保初始 marker 指向文档末尾（空文档时 position=0 是正确的）
    m_inputMarker = 0;
}

// ============================================================
// 公共方法 — 输出追加
// ============================================================

void TerminalView::appendOutput(const QByteArray& data)
{
    // ANSI 解析渲染（核心输出通道）
    parseAnsiData(data);

    // 每次收到输出后，更新输入标记到文档末尾之后
    // 确保用户新输入不会插入到已渲染的输出中间
    setInputMarker();

    // 自动滚动到底部
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalView::appendPlainText(const QString& text, const QColor& color)
{
    moveCursor(QTextCursor::End);

    if (color.isValid()) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        textCursor().setCharFormat(fmt);
    }

    insertPlainText(text);
    setInputMarker();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalView::showWelcome(const QString& text, const QColor& color)
{
    moveCursor(QTextCursor::End);

    if (color.isValid()) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        textCursor().setCharFormat(fmt);
    }

    insertPlainText(text);
    setInputMarker();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalView::setInputMarker()
{
    m_inputMarker = textCursor().position();
}

QString TerminalView::currentInputLine() const
{
    QTextCursor c(document());
    c.setPosition(m_inputMarker);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return c.selectedText().trimmed();
}

void TerminalView::clearCurrentInput()
{
    QTextCursor c(textCursor());
    c.setPosition(m_inputMarker);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
}

void TerminalView::clearScreen()
{
    // 保存当前 prompt（如果有），清屏后重新显示
    QString savedPrompt = m_lastPrompt.isEmpty()
        ? QStringLiteral("") : m_lastPrompt;

    clear();
    m_inputMarker = 0;

    // 恢复 prompt（让用户知道当前在哪个目录）
    if (!savedPrompt.isEmpty()) {
        appendPlainText(savedPrompt + QStringLiteral(" "), QColor(120, 220, 120));  // 绿色 prompt
        m_inputMarker = textCursor().position();
    } else {
        setInputMarker();
    }
}

void TerminalView::setTerminalFont(const QFont& font)
{
    QFont terminalFont = font;
    terminalFont.setStyleHint(QFont::Monospace);
    setFont(terminalFont);
}

void TerminalView::setForegroundColor(const QColor& color)
{
    QPalette pal = palette();
    pal.setColor(QPalette::Text, color);
    setPalette(pal);
}

void TerminalView::setBackgroundColor(const QColor& color)
{
    QPalette pal = palette();
    pal.setColor(QPalette::Base, color);
    setPalette(pal);
}

void TerminalView::setCursorColor(const QColor& color)
{
    // 通过样式表设置光标颜色
    QString styleSheet = QStringLiteral("QTextEdit { selection-background-color: %1; }").arg(color.name());
    setStyleSheet(styleSheet);
}

// ============================================================
// 公共槽 — 光标管理
// ============================================================

void TerminalView::focusInputEnd()
{
    moveCursor(QTextCursor::End);
}

// ============================================================
// 键盘事件处理 — 终端交互核心
// ============================================================

void TerminalView::keyPressEvent(QKeyEvent* event)
{
    // Ctrl+C → 发送中断信号（如果无选中文字则中断进程）
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_C) {
        if (!textCursor().hasSelection()) {
            emit interruptRequested();
            return;
        }
        // 有选中则走默认复制行为
    }

    // Ctrl+L → 清屏
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_L) {
        clearScreen();
        return;
    }

    // Tab → 发送 tab 字符（补全）
    if (event->key() == Qt::Key_Tab) {
        insertPlainText("    ");  // 4空格代替tab
        return;
    }

    // Enter → 提交命令
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        submitCurrentLine();
        return;
    }

    // Up/Down → 历史导航（非 Ctrl 组合）
    if (event->key() == Qt::Key_Up && !(event->modifiers() & Qt::ControlModifier)) {
        navigateHistory(-1);  // Up = 向上翻旧命令
        return;
    }
    if (event->key() == Qt::Key_Down && !(event->modifiers() & Qt::ControlModifier)) {
        navigateHistory(1);   // Down = 向下翻
        return;
    }

    // Backspace → 只能删除到 inputMarker 位置
    if (event->key() == Qt::Key_Backspace) {
        QTextCursor c = textCursor();
        if (c.position() > m_inputMarker) {
            c.deletePreviousChar();
            setTextCursor(c);
        }
        return;
    }

    // Delete → 同上限制
    if (event->key() == Qt::Key_Delete) {
        QTextCursor c = textCursor();
        if (c.position() >= m_inputMarker) {
            c.deleteChar();
            setTextCursor(c);
        }
        return;
    }

    // Home → 跳到输入行首（inputMarker位置）
    if (event->key() == Qt::Key_Home) {
        QTextCursor c = textCursor();
        c.setPosition(m_inputMarker);
        setTextCursor(c);
        return;
    }

    // End → 跳到文档末尾
    if (event->key() == Qt::Key_End) {
        moveCursor(QTextCursor::End);
        return;
    }

    // Left Arrow → 不能越过 inputMarker
    if (event->key() == Qt::Key_Left && !(event->modifiers() & Qt::ControlModifier)) {
        QTextCursor c = textCursor();
        if (c.position() > m_inputMarker) {
            c.movePosition(QTextCursor::Left);
            setTextCursor(c);
        }
        return;
    }

    // 其他可打印字符 → 正常插入（默认 QTextEdit 行为）
    // 但确保光标不在 inputMarker 之前
    ensureCursorInInputArea();
    QTextEdit::keyPressEvent(event);
}

// ============================================================
// 鼠标事件 — 点击时光标跳到输入区
// ============================================================

void TerminalView::mousePressEvent(QMouseEvent* event)
{
    QTextEdit::mousePressEvent(event);
    // 确保点击后光标不跑到输入区域之前
    if (textCursor().position() < m_inputMarker) {
        QTextCursor c = textCursor();
        c.setPosition(m_inputMarker);
        setTextCursor(c);
    }
}

// P1-2: 双击选中单词（用于复制）
void TerminalView::mouseDoubleClickEvent(QMouseEvent* event)
{
    // P1-2: 检测三击（在双击事件中处理）
    QDateTime now = QDateTime::currentDateTime();
    if (m_lastClickTime.isValid() &&
        m_lastClickTime.msecsTo(now) < QApplication::doubleClickInterval() &&
        m_clickCount >= 2) {
        // 三击：选中整行
        handleTripleClick(event);
        m_clickCount = 0;
        return;
    }

    ++m_clickCount;
    m_lastClickTime = now;

    // 使用默认的双击选中单词行为
    QTextEdit::mouseDoubleClickEvent(event);

    // 自动复制到剪贴板（VSCode风格）
    if (textCursor().hasSelection()) {
        QApplication::clipboard()->setText(textCursor().selectedText());
    }
}

// P1-2: 三击选中整行
void TerminalView::handleTripleClick(QMouseEvent* event)
{
    Q_UNUSED(event)
    // 选中当前整行
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::StartOfLine);
    cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    setTextCursor(cursor);

    // 自动复制到剪贴板
    if (cursor.hasSelection()) {
        QApplication::clipboard()->setText(cursor.selectedText());
    }
}

// ============================================================
// 内部方法 — 命令提交与历史导航
// ============================================================

void TerminalView::submitCurrentLine()
{
    QString cmd = currentInputLine().trimmed();

    // 插入换行（视觉上换到下一行）
    insertPlainText("\n");
    m_inputMarker = textCursor().position();  // 新输入行从这里开始

    if (!cmd.isEmpty()) {
        // 保存历史（去重：不与上一条重复）
        if (m_commandHistory.isEmpty() || m_commandHistory.last() != cmd) {
            m_commandHistory.append(cmd);
            // 限制历史条数（默认500）
            if (m_commandHistory.size() > 500) {
                m_commandHistory.removeFirst();
            }
        }
        m_historyIndex = -1;
        m_tempInput.clear();

        emit commandSubmitted(cmd);
    } else {
        // 空行也提交（shell 会显示新的 prompt）
        emit commandSubmitted(QString());
    }
}

void TerminalView::navigateHistory(int direction)  // -1=Up, 1=Down
{
    if (m_commandHistory.isEmpty()) return;

    if (direction == -1) {  // Up
        if (m_historyIndex < 0) {
            m_tempInput = currentInputLine();  // 首次按Up时保存当前输入
        }
        m_historyIndex++;
        if (m_historyIndex >= m_commandHistory.size()) {
            m_historyIndex = m_commandHistory.size() - 1;  // 不越界
        }
    } else {  // Down
        m_historyIndex--;
    }

    clearCurrentInput();
    if (m_historyIndex >= 0 && m_historyIndex < m_commandHistory.size()) {
        insertPlainText(m_commandHistory[m_historyIndex]);
    } else if (m_historyIndex < 0) {
        // 回到当前输入，恢复之前保存的内容
        insertPlainText(m_tempInput);
    }
}

void TerminalView::ensureCursorInInputArea()
{
    if (textCursor().position() < m_inputMarker) {
        moveCursor(QTextCursor::End);
    }
}

// ============================================================
// ANSI 转义序列解析与渲染
// （从 EmbeddedTerminal 移植，改为操作自身的 document 和 cursor）
// ============================================================

void TerminalView::parseAnsiData(const QByteArray& data)
{
    // 将新数据追加到缓冲区（处理跨包的不完整转义序列）
    m_ansiBuffer.append(data);

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);

    int pos = 0;
    const int len = m_ansiBuffer.size();

    while (pos < len) {
        char ch = m_ansiBuffer.at(pos);

        // 检测 ESC 字符，开始转义序列
        if (ch == '\x1b') {
            if (pos + 1 < len && m_ansiBuffer.at(pos + 1) == '[') {
                // CSI 序列: ESC [ ... (final byte)
                int seqEnd = pos + 2;
                while (seqEnd < len) {
                    char c = m_ansiBuffer.at(seqEnd);
                    // final byte 范围: 0x40-0x7E (@A-Z[\]^_`a-z{|}~)
                    if (c >= 0x40 && c <= 0x7e) {
                        break;
                    }
                    ++seqEnd;
                }

                if (seqEnd >= len) {
                    // 序列不完整，保留缓冲区剩余部分等待下次数据
                    m_ansiBuffer = m_ansiBuffer.mid(pos);
                    setTextCursor(cursor);
                    return;
                }

                // 提取完整的 CSI 序列参数部分
                char finalByte = m_ansiBuffer.at(seqEnd);
                QByteArray paramsRaw = m_ansiBuffer.mid(pos + 2, seqEnd - (pos + 2));

                if (finalByte == 'm') {
                    // SGR (Select Graphic Rendition) — 颜色/格式
                    QString paramsStr = QString::fromLatin1(paramsRaw);
                    QStringList paramList = paramsStr.split(QLatin1Char(';'), Qt::SkipEmptyParts);
                    applySgrFormat(paramList);
                }
                // 其他 CSI 序列可在此扩展（光标移动、清屏等）

                pos = seqEnd + 1;  // 跳过整个序列
                continue;
            } else {
                // 不完整 ESC 序列或非 CSI
                if (pos + 1 >= len) {
                    m_ansiBuffer = m_ansiBuffer.mid(pos);
                    setTextCursor(cursor);
                    return;
                }
                // 孤立 ESC 或其他转义，跳过
                ++pos;
                continue;
            }
        }

        // 控制字符处理
        if (ch == '\r') {
            ++pos;
            continue;   // \r 单独不换行（\r\n 才是 Windows 换行）
        }
        if (ch == '\n') {
            cursor.insertText(QStringLiteral("\n"));
            ++pos;
            continue;
        }
        if (ch == '\t') {
            cursor.insertText(QStringLiteral("    "));  // Tab → 4空格
            ++pos;
            continue;
        }
        if (ch == '\x07') {
            // BEL (响铃)，忽略
            ++pos;
            continue;
        }

        // 可打印字符：收集连续的普通文本批量插入
        int textStart = pos;
        while (pos < len) {
            char c = m_ansiBuffer.at(pos);
            if (c == '\x1b' || c == '\r' || c == '\n' || c == '\t' || c == '\x07')
                break;
            ++pos;
        }

        if (pos > textStart) {
            QString text = QString::fromLocal8Bit(
                m_ansiBuffer.mid(textStart, pos - textStart));

            // === Prompt 检测：识别 shell 提示符并缓存 ===
            // PowerShell: "PS F:\path> "  CMD: "F:\path> "  Bash: "$ "
            QRegularExpression promptRegex(
                QStringLiteral("^(PS\\s+)?[A-Za-z]:\\\\[^>]*>|^\\$\\s*$|^%.*%>|^#\\s+"),
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match = promptRegex.match(text.trimmed());
            if (match.hasMatch() && !text.contains(QLatin1Char('\n'))) {
                m_lastPrompt = text.trimmed();
            }

            // P2-H01: 记录插入起始位置（供 shell 模式高亮定位文档范围）
            int shellHighlightStart = cursor.position();

            // === 可执行文件颜色高亮（ls/dir 输出中的 .exe/.dll 等）===
            cursor.setCharFormat(m_currentFormat);
            QColor fgColor = m_currentFormat.foreground().color();
            bool isDefaultColor = !fgColor.isValid()
                || fgColor == QColor(Qt::white)
                || fgColor == QColor(204, 204, 204);
            if (isDefaultColor) {
                // 仅在默认前景色时做可执行文件检测（避免覆盖 ANSI 已设颜色）
                QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                int insertPos = 0;
                for (const QString& word : words) {
                    // 插入分隔空格
                    if (insertPos > 0) {
                        cursor.insertText(QStringLiteral(" "));
                    }
                    // 检测是否为可执行文件
                    if (isExecutableFile(word)) {
                        QTextCharFormat exeFmt = m_currentFormat;
                        exeFmt.setForeground(QColor(120, 220, 120));  // 绿色
                        cursor.setCharFormat(exeFmt);
                    } else {
                        cursor.setCharFormat(m_currentFormat);
                    }
                    cursor.insertText(word);
                    insertPos++;
                }
                // 处理末尾可能的多余空格/换行
                if (text.endsWith(QLatin1Char(' '))) {
                    cursor.setCharFormat(m_currentFormat);
                    cursor.insertText(QStringLiteral(" "));
                }
            } else {
                // ANSI 已设置自定义颜色，直接插入不做额外处理
                cursor.insertText(text);
            }

            // === P2-H01: Shell 模式高亮（错误/警告/成功/信息/路径着色）===
            // 在文本插入后追加着色，不破坏 ANSI 已有着色与可执行文件高亮
            highlightShellPatterns(text, shellHighlightStart);
        }
    }

    // 全部解析完毕，清空缓冲区
    m_ansiBuffer.clear();

    setTextCursor(cursor);
}

void TerminalView::applySgrFormat(const QStringList& params)
{
    if (params.isEmpty()) {
        // ESC[m 或 ESC[0m → 重置所有格式
        m_currentFormat = QTextCharFormat();
        return;
    }

    for (const QString& p : params) {
        bool ok = false;
        int code = p.toInt(&ok);
        if (!ok) continue;

        switch (code) {
        case 0:   // 重置
            m_currentFormat = QTextCharFormat();
            break;
        case 1:   // 粗体/Bright
            m_currentFormat.setFontWeight(QFont::Bold);
            break;
        case 2:   // 淡色（细体）
            m_currentFormat.setFontWeight(QFont::Light);
            break;
        case 3:   // 斜体
            m_currentFormat.setFontItalic(true);
            break;
        case 4:   // 下划线
            m_currentFormat.setFontUnderline(true);
            break;
        case 7:   // 反色
            {
                QColor fg = m_currentFormat.foreground().color();
                QColor bg = m_currentFormat.background().color();
                if (fg.isValid()) m_currentFormat.setBackground(fg);
                if (bg.isValid()) m_currentFormat.setForeground(bg);
            }
            break;
        case 9:   // 删除线
            m_currentFormat.setFontStrikeOut(true);
            break;
        case 22:  // 正常粗细（取消粗体/淡色）
            m_currentFormat.setFontWeight(QFont::Normal);
            break;
        case 23:  // 取消斜体
            m_currentFormat.setFontItalic(false);
            break;
        case 24:  // 取消下划线
            m_currentFormat.setFontUnderline(false);
            break;
        case 27:  // 取消反色
            break;  // 简化：不做复杂状态保存
        case 29:  // 取消删除线
            m_currentFormat.setFontStrikeOut(false);
            break;

        // 前景色：标准色 30-37
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            m_currentFormat.setForeground(ansiColorToQColor(code, true));
            break;

        // 前景色：亮色 90-97
        case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97:
            m_currentFormat.setForeground(ansiColorToQColor(code, true));
            break;

        // 背景色：标准色 40-47
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            m_currentFormat.setBackground(ansiColorToQColor(code, false));
            break;

        // 背景色：亮色 100-107
        case 100: case 101: case 102: case 103:
        case 104: case 105: case 106: case 107:
            m_currentFormat.setBackground(ansiColorToQColor(code, false));
            break;

        default:
            break;
        }
    }
}

QColor TerminalView::ansiColorToQColor(int code, bool foreground)
{
    // 标准 ANSI 16 色
    static const QColor standardColors[16] = {
        QColor(0x00, 0x00, 0x00),   // 0 / 40: 黑
        QColor(0xcd, 0x31, 0x27),   // 1 / 41: 红
        QColor(0x0d, 0xc9, 0x6a),   // 2 / 42: 绿
        QColor(0xe5, 0xe5, 0x10),   // 3 / 43: 黄
        QColor(0x24, 0x72, 0xb8),   // 4 / 44: 蓝
        QColor(0xbc, 0x3f, 0xbc),   // 5 / 45: 品红
        QColor(0x11, 0xa8, 0xcd),   // 6 / 46: 青
        QColor(0xe5, 0xe5, 0xe5),   // 7 / 47: 白（亮）
        QColor(0x66, 0x66, 0x66),   // 8 / 100: 亮黑（灰）
        QColor(0xf1, 0x4c, 0x4c),   // 9 / 101: 亮红
        QColor(0x23, 0xeb, 0x0a),   // 10 / 102: 亮绿
        QColor(0xf5, 0xf5, 0x22),   // 11 / 103: 亮黄
        QColor(0x3b, 0x8e, 0xea),   // 12 / 104: 亮蓝
        QColor(0xd3, 0x6e, 0xd3),   // 13 / 105: 亮品红
        QColor(0x29, 0xcd, 0xff),   // 14 / 106: 亮青
        QColor(0xe5, 0xe5, 0xe5),   // 15 / 107: 亮白
    };

    int idx = -1;
    if (foreground) {
        if (code >= 30 && code <= 37) idx = code - 30;
        else if (code >= 90 && code <= 97) idx = code - 90 + 8;
    } else {
        if (code >= 40 && code <= 47) idx = code - 40;
        else if (code >= 100 && code <= 107) idx = code - 100 + 8;
    }

    if (idx >= 0 && idx < 16)
        return standardColors[idx];

    return QColor();  // 无效码返回无效颜色
}

bool TerminalView::isExecutableFile(const QString& text) const
{
    // 检查是否以已知可执行扩展名结尾
    for (const QString& ext : s_executableExtensions) {
        if (text.endsWith(ext, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

// ============================================================
// P2-H01: Shell 模式高亮（错误/警告/成功/信息/路径着色）
// 在 ANSI 解析插入文本后调用，对刚插入的文本范围应用模式匹配着色
// 设计要点：
//   - 不破坏 ANSI 已有着色（仅对默认前景色的文本应用高亮）
//   - 不破坏可执行文件高亮（检查文档中已有颜色，有则跳过）
//   - 颜色从 ThemeManager::currentPalette() 获取，跟随主题
// ============================================================

void TerminalView::highlightShellPatterns(const QString& text, int startPos)
{
    if (text.isEmpty()) return;

    // 检查基础 ANSI 格式是否有自定义前景色 — 若有则整段跳过（保留 ANSI 着色）
    QTextCursor checkCursor(document());
    if (startPos < document()->characterCount()) {
        checkCursor.setPosition(startPos + 1);
        QTextCharFormat baseFmt = checkCursor.charFormat();
        QColor baseFg = baseFmt.foreground().color();
        bool hasAnsiColor = baseFg.isValid()
            && baseFg != QColor(Qt::white)
            && baseFg != QColor(204, 204, 204);
        if (hasAnsiColor) return;
    }

    // 从主题获取配色（跟随主题热切换）
    const auto& palette = ThemeManager::instance().currentPalette();
    QColor errorColor   = palette.errorColor;
    QColor warningColor = palette.warningColor;
    QColor successColor = QColor(120, 220, 120);   // 绿色（成功）
    QColor infoColor    = QColor(120, 200, 220);   // 青色（信息）
    QColor promptColor  = palette.fgSecondary;      // 灰色（命令提示符）
    QColor pathColor    = palette.accentPrimary;    // 蓝色（文件路径）

    // 定义 Shell 模式列表（顺序决定优先级，靠前的模式先匹配）
    struct ShellPattern {
        QRegularExpression regex;
        QColor color;
        bool underline;
    };
    QList<ShellPattern> patterns = {
        { QRegularExpression(QStringLiteral("error:|Error:|ERROR:|fatal:|FAIL")),
          errorColor, false },
        { QRegularExpression(QStringLiteral("warning:|Warning:|WARN:|deprecated:")),
          warningColor, false },
        { QRegularExpression(QStringLiteral("success|succeeded|passed|\xe2\x9c\x93|done")),
          successColor, false },  // ✓ = U+2713 (UTF-8: E2 9C 93)
        { QRegularExpression(QStringLiteral("info:|Info:|INFO:|note:")),
          infoColor, false },
        { QRegularExpression(QStringLiteral("^\\s*[\\$#>]\\s"),
          QRegularExpression::MultilineOption),
          promptColor, false },
        { QRegularExpression(QStringLiteral("[\\w/\\\\]+\\.\\w+")),
          pathColor, true },
    };

    // 收集所有匹配项
    struct Match { int pos; int len; QColor color; bool underline; };
    QList<Match> matches;
    for (const auto& p : patterns) {
        auto it = p.regex.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            matches.append({ (int)m.capturedStart(), (int)m.capturedLength(),
                             p.color, p.underline });
        }
    }
    if (matches.isEmpty()) return;

    // 按位置排序，处理重叠（保留先出现的匹配，跳过被覆盖的）
    std::sort(matches.begin(), matches.end(),
              [](const Match& a, const Match& b) { return a.pos < b.pos; });

    // 应用高亮 — 使用 mergeCharFormat 附加到现有格式（不破坏可执行文件绿色等）
    QTextCursor cursor(document());
    int lastEnd = 0;
    for (const auto& m : matches) {
        if (m.pos < lastEnd) continue;  // 跳过重叠区域

        int absStart = startPos + m.pos;
        int absEnd   = startPos + m.pos + m.len;
        if (absEnd > document()->characterCount()) break;  // 越界保护

        cursor.setPosition(absStart);
        cursor.setPosition(absEnd, QTextCursor::KeepAnchor);

        // 检查该范围是否已有非默认前景色（如可执行文件绿色），有则跳过
        QTextCharFormat rangeFmt = cursor.charFormat();
        QColor rangeFg = rangeFmt.foreground().color();
        bool hasCustomColor = rangeFg.isValid()
            && rangeFg != QColor(Qt::white)
            && rangeFg != QColor(204, 204, 204);
        if (hasCustomColor) {
            lastEnd = m.pos + m.len;
            continue;
        }

        // 附加前景色和下划线（mergeCharFormat 不覆盖其他格式属性）
        QTextCharFormat fmt;
        fmt.setForeground(m.color);
        if (m.underline) fmt.setFontUnderline(true);
        cursor.mergeCharFormat(fmt);
        lastEnd = m.pos + m.len;
    }
}
