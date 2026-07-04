#include "HoverPopup.h"
#include "core/config/ThemeManager.h"

#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QScreen>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QTextDocument>
#include <QEnterEvent>

// ============================================================
// HoverPopup — Markdown 富文本悬停预览弹窗
// ============================================================

HoverPopup::HoverPopup(QWidget* parent)
    : QWidget(parent)
{
    // 弹窗属性：无边框 + 工具窗口 + 失焦自动隐藏
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);  // 启用 hover 事件追踪

    // 布局
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 文本浏览器（支持 HTML 渲染 + 鼠标交互）
    m_textBrowser = new QTextBrowser(this);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setFocusPolicy(Qt::NoFocus);
    m_textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textBrowser->setContextMenuPolicy(Qt::NoContextMenu);
    m_textBrowser->setMouseTracking(true);
    layout->addWidget(m_textBrowser);

    // 限制最大尺寸
    setMaximumWidth(600);
    setMaximumHeight(400);

    // 淡入淡出动画：透明度效果 + 属性动画
    // 注意：Qt 每个 widget 只能有一个 QGraphicsEffect，因此用 opacity effect
    // 替代 shadow effect（动画优先级 > 阴影装饰）
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);  // 初始透明
    setGraphicsEffect(m_opacityEffect);

    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setDuration(150);  // 150ms 淡入淡出
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_fadingOut) {
            QWidget::hide();  // 淡出完成后真正隐藏
            m_fadingOut = false;
        }
    });

    // H2: 防闪烁隐藏延时：鼠标离开后 50ms 才触发淡出（从 150ms 缩短，消除滞留感）
    // 仅保留极短防抖延时防误闪，鼠标离开标识符区域应立即关闭预览
    m_hideDelayTimer.setSingleShot(true);
    m_hideDelayTimer.setInterval(50);
    connect(&m_hideDelayTimer, &QTimer::timeout, this, [this]() {
        startFadeOut();
    });
}

void HoverPopup::showMarkdown(const QString& markdown, const QPoint& pos)
{
    if (markdown.isEmpty()) {
        hidePopup();
        return;
    }

    // 取消任何正在进行的隐藏
    m_hideDelayTimer.stop();
    m_fadingOut = false;

    // 转换 Markdown → HTML
    QString html = markdownToHtml(markdown);
    QString fullHtml = QStringLiteral("<html><head><style>%1</style></head><body>%2</body></html>")
                           .arg(generateStylesheet(), html);

    m_textBrowser->setHtml(fullHtml);
    m_textBrowser->document()->setDocumentMargin(0);

    // 自适应大小
    adjustSizeToFit();

    // 定位弹窗（确保不超出屏幕边界）
    move(pos);
    show();

    // 再次调整位置，确保不超出屏幕
    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    QRect popupRect = geometry();
    if (popupRect.right() > screenRect.right()) {
        move(screenRect.right() - popupRect.width() - 4, pos.y());
    }
    if (popupRect.bottom() > screenRect.bottom()) {
        move(pos.x(), pos.y() - popupRect.height() - 20);
    }

    // 启动淡入动画
    startFadeIn();
}

void HoverPopup::hidePopup()
{
    // 带淡出动画的隐藏
    if (!isVisible()) return;
    m_hideDelayTimer.stop();
    startFadeOut();
}

void HoverPopup::hideImmediately()
{
    // 立即隐藏（无动画），用于切页/按键等即时场景
    m_hideDelayTimer.stop();
    m_fadingOut = false;
    if (m_fadeAnimation) m_fadeAnimation->stop();
    if (m_opacityEffect) m_opacityEffect->setOpacity(0.0);
    QWidget::hide();
}

void HoverPopup::startFadeIn()
{
    if (!m_opacityEffect || !m_fadeAnimation) return;
    m_fadeAnimation->stop();
    m_fadingOut = false;
    m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
}

void HoverPopup::startFadeOut()
{
    if (!m_opacityEffect || !m_fadeAnimation) return;
    if (!isVisible()) return;
    m_fadeAnimation->stop();
    m_fadingOut = true;
    m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->start();
}

void HoverPopup::focusOutEvent(QFocusEvent* event)
{
    Q_UNUSED(event)
    hidePopup();
}

void HoverPopup::enterEvent(QEnterEvent* event)
{
    // 鼠标进入弹窗 → 取消防闪烁隐藏（允许用户阅读/选择文本）
    m_hideDelayTimer.stop();
    // 如果正在淡出，恢复显示
    if (m_fadingOut) {
        m_fadingOut = false;
        if (m_fadeAnimation) m_fadeAnimation->stop();
        startFadeIn();
    }
    QWidget::enterEvent(event);
}

void HoverPopup::leaveEvent(QEvent* event)
{
    // 鼠标离开弹窗 → 启动防闪烁隐藏延时
    m_hideDelayTimer.start();
    QWidget::leaveEvent(event);
}

// ============================================================
// Markdown → HTML 转换
// ============================================================

QString HoverPopup::markdownToHtml(const QString& markdown) const
{
    QStringList lines = markdown.split(QStringLiteral("\n"));
    QStringList htmlParts;
    bool inCodeBlock = false;
    QString codeBlockLang;
    QStringList codeBlockLines;

    // H3: 列表/引用块累积状态 — 连续同类型行合并为单个 <ul>/<ol>/<blockquote>
    enum ListType { None, Unordered, Ordered };
    ListType currentListType = None;
    QStringList listItems;          // 累积当前列表项
    QStringList quoteLines;         // 累积引用块行

    // H3: 辅助 lambda — flush 当前列表为 HTML
    auto flushList = [&]() {
        if (currentListType == None || listItems.isEmpty()) return;
        QString tag = (currentListType == Unordered) ? QStringLiteral("ul") : QStringLiteral("ol");
        htmlParts.append(QStringLiteral("<%1>").arg(tag));
        for (const QString& item : listItems) {
            htmlParts.append(QStringLiteral("<li>%1</li>").arg(item));
        }
        htmlParts.append(QStringLiteral("</%1>").arg(tag));
        listItems.clear();
        currentListType = None;
    };

    // H3: 辅助 lambda — flush 当前引用块为 HTML
    auto flushQuote = [&]() {
        if (quoteLines.isEmpty()) return;
        htmlParts.append(QStringLiteral("<blockquote>%1</blockquote>")
                             .arg(quoteLines.join(QStringLiteral("<br>"))));
        quoteLines.clear();
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];

        // 代码块开始/结束 ```lang
        if (line.trimmed().startsWith(QStringLiteral("```"))) {
            // H3: 进入/退出代码块前先 flush 列表/引用块
            flushList();
            flushQuote();
            if (!inCodeBlock) {
                // 代码块开始
                inCodeBlock = true;
                codeBlockLang = line.trimmed().mid(3).trimmed();  // 提取语言标识
                codeBlockLines.clear();
            } else {
                // 代码块结束 → 输出代码块 HTML（带 C++ 关键字高亮）
                inCodeBlock = false;
                QString codeContent = highlightCppCode(codeBlockLines.join(QStringLiteral("\n")));
                htmlParts.append(QStringLiteral("<div class=\"code-block\"><pre><code>%1</code></pre></div>")
                                     .arg(codeContent));
            }
            continue;
        }

        if (inCodeBlock) {
            codeBlockLines.append(line);
            continue;
        }

        QString trimmed = line.trimmed();

        // H3: 引用块 > quote — 累积连续引用行
        static const QRegularExpression quoteRegex(QStringLiteral("^>\\s?(.*)$"));
        auto quoteMatch = quoteRegex.match(trimmed);
        if (quoteMatch.hasMatch()) {
            // 引用块开始/继续：先 flush 列表（不同块类型切换）
            if (currentListType != None) flushList();
            quoteLines.append(processInlineMarkdown(quoteMatch.captured(1).trimmed()));
            continue;
        } else {
            // 非引用行 → flush 引用块
            flushQuote();
        }

        // H3: 无序列表 - item / * item / + item
        static const QRegularExpression ulRegex(QStringLiteral("^[-*+]\\s+(.+)$"));
        auto ulMatch = ulRegex.match(trimmed);
        if (ulMatch.hasMatch()) {
            if (currentListType != Unordered) { flushList(); currentListType = Unordered; }
            listItems.append(processInlineMarkdown(ulMatch.captured(1).trimmed()));
            continue;
        }

        // H3: 有序列表 1. item / 2. item
        static const QRegularExpression olRegex(QStringLiteral("^\\d+\\.\\s+(.+)$"));
        auto olMatch = olRegex.match(trimmed);
        if (olMatch.hasMatch()) {
            if (currentListType != Ordered) { flushList(); currentListType = Ordered; }
            listItems.append(processInlineMarkdown(olMatch.captured(1).trimmed()));
            continue;
        }

        // H3: 非列表行 → flush 列表
        flushList();

        // 空行
        if (trimmed.isEmpty()) {
            htmlParts.append(QStringLiteral("<br>"));
            continue;
        }

        // 分割线 ---
        if (trimmed == QStringLiteral("---") || trimmed == QStringLiteral("***")) {
            htmlParts.append(QStringLiteral("<hr>"));
            continue;
        }

        // 标题 # ## ### ####
        if (trimmed.startsWith(QStringLiteral("####"))) {
            htmlParts.append(QStringLiteral("<h4>%1</h4>").arg(processInlineMarkdown(trimmed.mid(4).trimmed())));
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("###"))) {
            htmlParts.append(QStringLiteral("<h3>%1</h3>").arg(processInlineMarkdown(trimmed.mid(3).trimmed())));
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("##"))) {
            htmlParts.append(QStringLiteral("<h2>%1</h2>").arg(processInlineMarkdown(trimmed.mid(2).trimmed())));
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("#"))) {
            htmlParts.append(QStringLiteral("<h1>%1</h1>").arg(processInlineMarkdown(trimmed.mid(1).trimmed())));
            continue;
        }

        // H4: Doxygen 标签行 — 扩充标签集，对齐 CodeSyntaxHighlighter 支持范围
        // 支持：@brief @data @author @param @tparam @return @returns @note @warning
        //       @see @since @version @date @throws @throw @exception @code @endcode
        //       @file @mainpage @section @subsection @bug @todo @deprecated
        //       @internal @endinternal @class @struct @enum @fn @var @def @typedef @namespace
        static const QRegularExpression doxyRegex(
            QStringLiteral("^\\s*@(brief|data|author|param|param\\[\\w+\\]|tparam|return|returns|see|note|warning|deprecated|throws|throw|exception|version|since|date|file|mainpage|section|subsection|bug|todo|code|endcode|internal|endinternal|class|struct|enum|fn|var|def|typedef|namespace)\\b"));
        auto doxyMatch = doxyRegex.match(trimmed);
        if (doxyMatch.hasMatch()) {
            QString tag = doxyMatch.captured(1);
            QString rest = trimmed.mid(doxyMatch.capturedEnd()).trimmed();
            htmlParts.append(QStringLiteral("<div class=\"doxy-line\"><span class=\"doxy-tag\">@%1</span> %2</div>")
                                 .arg(escapeHtml(tag), processInlineMarkdown(rest)));
            continue;
        }

        // 普通段落
        htmlParts.append(QStringLiteral("<p>%1</p>").arg(processInlineMarkdown(trimmed)));
    }

    // H3: 文件结束前 flush 残留的列表/引用块
    flushList();
    flushQuote();

    // 处理未闭合的代码块
    if (inCodeBlock && !codeBlockLines.isEmpty()) {
        QString codeContent = highlightCppCode(codeBlockLines.join(QStringLiteral("\n")));
        htmlParts.append(QStringLiteral("<div class=\"code-block\"><pre><code>%1</code></pre></div>")
                             .arg(codeContent));
    }

    return htmlParts.join(QStringLiteral("\n"));
}

QString HoverPopup::escapeHtml(const QString& text) const
{
    QString result = text;
    result.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    result.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    result.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    result.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
    return result;
}

QString HoverPopup::processInlineMarkdown(const QString& text) const
{
    QString result = escapeHtml(text);

    // 行内代码 `code`（先处理，避免被其他规则干扰）
    static const QRegularExpression inlineCodeRegex(QStringLiteral("`([^`]+)`"));
    result.replace(inlineCodeRegex, QStringLiteral("<code class=\"inline-code\">\\1</code>"));

    // 加粗 **text** 或 __text__
    static const QRegularExpression boldRegex(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    result.replace(boldRegex, QStringLiteral("<b>\\1</b>"));
    static const QRegularExpression boldRegex2(QStringLiteral("__(.+?)__"));
    result.replace(boldRegex2, QStringLiteral("<b>\\1</b>"));

    // 斜体 *text* 或 _text_（避免与加粗冲突）
    static const QRegularExpression italicRegex(QStringLiteral("(?<!\\*)\\*(?!\\*)([^*]+?)\\*(?!\\*)"));
    result.replace(italicRegex, QStringLiteral("<i>\\1</i>"));

    // H4: Doxygen 行内标签 — 扩充标签集，与块级正则保持一致
    // 在段落中出现的 @brief @param @tparam @return @see @note @warning @deprecated @throws
    // @author @date @version @since @todo @bug @code @endcode 等
    static const QRegularExpression doxyInlineRegex(
        QStringLiteral("@(brief|data|author|param|tparam|return|returns|see|note|warning|deprecated|throws|throw|exception|version|since|date|file|mainpage|section|subsection|bug|todo|code|endcode|internal|endinternal|class|struct|enum|fn|var|def|typedef|namespace)\\b"));
    result.replace(doxyInlineRegex, QStringLiteral("<span class=\"doxy-tag\">@\\1</span>"));

    return result;
}

QString HoverPopup::highlightCppCode(const QString& code) const
{
    // H1: 简单 C++ 语法高亮 — 先转义 HTML，再用 <span> 包裹关键字/类型/字符串/注释/预处理
    QString escaped = escapeHtml(code);

    // 顺序很重要：先处理注释和字符串（避免内部的关键字被高亮）
    // 行注释 //...
    static const QRegularExpression lineCommentRegex(QStringLiteral("(//[^\n]*)"));
    escaped.replace(lineCommentRegex, QStringLiteral("<span class=\"cpp-comment\">\\1</span>"));
    // 块注释 /* ... */（跨行）
    static const QRegularExpression blockCommentRegex(QStringLiteral("(/\\*[\\s\\S]*?\\*/)"));
    escaped.replace(blockCommentRegex, QStringLiteral("<span class=\"cpp-comment\">\\1</span>"));
    // 字符串 "..." 和 '...'
    static const QRegularExpression stringRegex(QStringLiteral("(\"[^\"]*\")"));
    escaped.replace(stringRegex, QStringLiteral("<span class=\"cpp-string\">\\1</span>"));
    static const QRegularExpression charRegex(QStringLiteral("('[^']*')"));
    escaped.replace(charRegex, QStringLiteral("<span class=\"cpp-string\">\\1</span>"));
    // 预处理指令 #include / #define / #ifdef ...
    static const QRegularExpression preprocRegex(QStringLiteral("(#[a-zA-Z]+)"));
    escaped.replace(preprocRegex, QStringLiteral("<span class=\"cpp-preproc\">\\1</span>"));

    // H5: TODO/FIXME/NOTE/XXX/HACK 待办标记高亮 — 暖红 + 斜体
    // 在注释/字符串 span 处理之后，关键字处理之前
    // 用单词边界匹配，避免误匹配 cpp-todo 等 class 名（连字符不构成单词边界）
    static const QRegularExpression todoRegex(
        QStringLiteral("\\b(TODO|FIXME|NOTE|XXX|HACK)\\b"));
    escaped.replace(todoRegex, QStringLiteral("<span class=\"cpp-todo\">\\1</span>"));

    // C++ 关键字
    static const QStringList keywords = {
        QStringLiteral("class"), QStringLiteral("struct"), QStringLiteral("public"),
        QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("virtual"),
        QStringLiteral("override"), QStringLiteral("static"), QStringLiteral("const"),
        QStringLiteral("constexpr"), QStringLiteral("inline"), QStringLiteral("explicit"),
        QStringLiteral("namespace"), QStringLiteral("using"), QStringLiteral("template"),
        QStringLiteral("typename"), QStringLiteral("return"), QStringLiteral("if"),
        QStringLiteral("else"), QStringLiteral("for"), QStringLiteral("while"),
        QStringLiteral("do"), QStringLiteral("switch"), QStringLiteral("case"),
        QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("default"),
        QStringLiteral("new"), QStringLiteral("delete"), QStringLiteral("this"),
        QStringLiteral("nullptr"), QStringLiteral("true"), QStringLiteral("false"),
        QStringLiteral("void"), QStringLiteral("auto"), QStringLiteral("enum"),
        QStringLiteral("union"), QStringLiteral("typedef"), QStringLiteral("friend"),
        QStringLiteral("operator"), QStringLiteral("mutable"), QStringLiteral("volatile"),
        QStringLiteral("throw"), QStringLiteral("try"), QStringLiteral("catch"),
        QStringLiteral("noexcept"), QStringLiteral("final"), QStringLiteral("sizeof"),
        QStringLiteral("alignof"), QStringLiteral("decltype")
    };
    // 用单词边界匹配每个关键字
    for (const QString& kw : keywords) {
        QRegularExpression re(QStringLiteral("\\b") + QRegularExpression::escape(kw) + QStringLiteral("\\b"));
        escaped.replace(re, QStringLiteral("<span class=\"cpp-keyword\">") + kw + QStringLiteral("</span>"));
    }

    // 常见类型（大写开头或常见类型名）
    static const QStringList types = {
        QStringLiteral("int"), QStringLiteral("char"), QStringLiteral("bool"),
        QStringLiteral("float"), QStringLiteral("double"), QStringLiteral("long"),
        QStringLiteral("short"), QStringLiteral("unsigned"), QStringLiteral("signed"),
        QStringLiteral("size_t"), QStringLiteral("uint32_t"), QStringLiteral("uint64_t"),
        QStringLiteral("int32_t"), QStringLiteral("int64_t"), QStringLiteral("QString"),
        QStringLiteral("QList"), QStringLiteral("QHash"), QStringLiteral("QMap"),
        QStringLiteral("QVector"), QStringLiteral("QWidget"), QStringLiteral("QObject"),
        QStringLiteral("QStringList"), QStringLiteral("QByteArray"), QStringLiteral("QVariant"),
        QStringLiteral("QPoint"), QStringLiteral("QRect"), QStringLiteral("QColor"),
        QStringLiteral("QTimer"), QStringLiteral("QAction"), QStringLiteral("QMenu"),
        QStringLiteral("QTextEdit"), QStringLiteral("QTextCursor"), QStringLiteral("QTextBlock"),
        QStringLiteral("QRegularExpression"), QStringLiteral("QEvent"), QStringLiteral("QKeyEvent"),
        QStringLiteral("QMouseEvent"), QStringLiteral("QFocusEvent"), QStringLiteral("QPaintEvent"),
        QStringLiteral("QWheelEvent"), QStringLiteral("QContextMenuEvent"), QStringLiteral("QDragEnterEvent"),
        QStringLiteral("QDropEvent"), QStringLiteral("QMimeData"), QStringLiteral("QUrl"),
        QStringLiteral("QFileInfo"), QStringLiteral("QFile"), QStringLiteral("QDir"),
        QStringLiteral("QFileSystemWatcher"), QStringLiteral("QSplitter"), QStringLiteral("QLabel"),
        QStringLiteral("QPushButton"), QStringLiteral("QComboBox"), QStringLiteral("QCheckBox"),
        QStringLiteral("QLineEdit"), QStringLiteral("QTextEdit"), QStringLiteral("QPlainTextEdit"),
        QStringLiteral("QDialog"), QStringLiteral("QMainWindow"), QStringLiteral("QApplication"),
        QStringLiteral("QScreen"), QStringLiteral("QGuiApplication"), QStringLiteral("QStyle"),
        QStringLiteral("QShortcut"), QStringLiteral("QKeySequence"), QStringLiteral("QClipboard"),
        QStringLiteral("QProcess"), QStringLiteral("QSettings"), QStringLiteral("QJsonDocument"),
        QStringLiteral("QJsonObject"), QStringLiteral("QJsonArray"), QStringLiteral("QJsonValue"),
        QStringLiteral("std::string"), QStringLiteral("std::vector"), QStringLiteral("std::map"),
        QStringLiteral("std::shared_ptr"), QStringLiteral("std::unique_ptr"), QStringLiteral("std::function")
    };
    for (const QString& ty : types) {
        // 跳过已经被 span 包裹的内容（简单处理：直接替换，已在 span 内的不会被二次匹配因为 &lt; 等）
        QRegularExpression re(QStringLiteral("\\b") + QRegularExpression::escape(ty) + QStringLiteral("\\b"));
        escaped.replace(re, QStringLiteral("<span class=\"cpp-type\">") + ty + QStringLiteral("</span>"));
    }

    return escaped;
}

QString HoverPopup::generateStylesheet() const
{
    // 根据当前主题选择配色（通过编辑器背景亮度判断暗色/亮色主题）
    const auto& palette = ThemeManager::instance().currentPalette();
    bool isDark = palette.bgEditor.lightness() <= 128;
    QString bgColor = isDark ? QStringLiteral("#1e1e1e") : QStringLiteral("#ffffff");
    QString fgColor = isDark ? QStringLiteral("#d4d4d4") : QStringLiteral("#333333");
    QString borderColor = isDark ? QStringLiteral("#3c3c3c") : QStringLiteral("#cccccc");
    QString codeBgColor = isDark ? QStringLiteral("#2d2d2d") : QStringLiteral("#f5f5f5");
    QString codeFgColor = isDark ? QStringLiteral("#ce9178") : QStringLiteral("#a31515");
    QString doxyTagColor = isDark ? QStringLiteral("#569CD6") : QStringLiteral("#0000FF");
    QString h1Color = isDark ? QStringLiteral("#4EC9B0") : QStringLiteral("#267F99");
    QString h2Color = isDark ? QStringLiteral("#569CD6") : QStringLiteral("#0000FF");
    QString h3Color = isDark ? QStringLiteral("#C586C0") : QStringLiteral("#AF00DB");
    QString linkColor = isDark ? QStringLiteral("#3794ff") : QStringLiteral("#0070C1");
    QString mutedColor = isDark ? QStringLiteral("#808080") : QStringLiteral("#888888");
    // C++ 语法高亮配色（VSCode Dark+ 风格）
    QString cppKwColor = isDark ? QStringLiteral("#569CD6") : QStringLiteral("#0000FF");      // 关键字：蓝
    QString cppTypeColor = isDark ? QStringLiteral("#4EC9B0") : QStringLiteral("#267F99");    // 类型：青绿
    QString cppStrColor = isDark ? QStringLiteral("#ce9178") : QStringLiteral("#a31515");     // 字符串：橙
    QString cppCommentColor = isDark ? QStringLiteral("#6A9955") : QStringLiteral("#008000"); // 注释：绿
    QString cppPreprocColor = isDark ? QStringLiteral("#C586C0") : QStringLiteral("#AF00DB"); // 预处理：紫
    // H5: TODO/FIXME/XXX/HACK 待办标记 — 暖红斜体（对齐编辑器 CodeSyntaxHighlighter 配色）
    QString cppTodoColor = isDark ? QStringLiteral("#FF6B6B") : QStringLiteral("#D1242F");
    // H3: 引用块文字颜色 — 弱化前景，与正文区分
    QString quoteColor = isDark ? QStringLiteral("#9aa0a6") : QStringLiteral("#666666");

    return QStringLiteral(
        "body { "
        "  background-color: %1; "
        "  color: %2; "
        "  font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif; "
        "  font-size: 13px; "
        "  line-height: 1.5; "
        "  padding: 10px 14px; "
        "  margin: 0; "
        "} "
        "h1 { "
        "  color: %3; "
        "  font-size: 16px; "
        "  font-weight: bold; "
        "  margin: 8px 0 4px 0; "
        "  padding-bottom: 2px; "
        "} "
        "h2 { "
        "  color: %4; "
        "  font-size: 15px; "
        "  font-weight: bold; "
        "  margin: 6px 0 3px 0; "
        "} "
        "h3 { "
        "  color: %5; "
        "  font-size: 14px; "
        "  font-weight: bold; "
        "  margin: 5px 0 2px 0; "
        "} "
        "h4 { "
        "  color: %5; "
        "  font-size: 13px; "
        "  font-weight: bold; "
        "  margin: 4px 0 2px 0; "
        "} "
        "p { "
        "  margin: 3px 0; "
        "} "
        "hr { "
        "  border: none; "
        "  border-top: 1px solid %6; "
        "  margin: 6px 0; "
        "} "
        // H3: 列表样式 — 缩进 + 圆点/数字标记
        "ul { "
        "  margin: 4px 0 4px 18px; "
        "  padding-left: 8px; "
        "} "
        "ol { "
        "  margin: 4px 0 4px 18px; "
        "  padding-left: 8px; "
        "} "
        "li { "
        "  margin: 2px 0; "
        "} "
        // H3: 引用块样式 — 左侧强调边 + 浅色背景
        "blockquote { "
        "  margin: 4px 0; "
        "  padding: 4px 10px; "
        "  border-left: 3px solid %10; "
        "  background-color: %7; "
        "  color: %16; "
        "  font-style: italic; "
        "} "
        ".code-block { "
        "  background-color: %7; "
        "  border: 1px solid %6; "
        "  border-radius: 4px; "
        "  padding: 8px 10px; "
        "  margin: 4px 0; "
        "  overflow-x: auto; "
        "} "
        ".code-block pre { "
        "  margin: 0; "
        "  white-space: pre-wrap; "
        "} "
        ".code-block code { "
        "  font-family: 'Consolas', 'Courier New', monospace; "
        "  font-size: 12px; "
        "  color: %2; "
        "} "
        ".inline-code { "
        "  background-color: %7; "
        "  color: %8; "
        "  font-family: 'Consolas', 'Courier New', monospace; "
        "  font-size: 12px; "
        "  padding: 1px 4px; "
        "  border-radius: 3px; "
        "} "
        ".doxy-line { "
        "  margin: 2px 0; "
        "} "
        ".doxy-tag { "
        "  color: %9; "
        "  font-weight: bold; "
        "  font-family: 'Consolas', monospace; "
        "} "
        "a { "
        "  color: %10; "
        "  text-decoration: none; "
        "} "
        "b { "
        "  font-weight: bold; "
        "} "
        ".cpp-keyword { color: %11; font-weight: bold; } "
        ".cpp-type { color: %12; } "
        ".cpp-string { color: %13; } "
        ".cpp-comment { color: %14; font-style: italic; } "
        ".cpp-preproc { color: %15; } "
        // H5: TODO/FIXME/XXX/HACK 待办标记 — 暖红 + 斜体，对齐主流 IDE 体验
        ".cpp-todo { color: %17; font-weight: bold; font-style: italic; } "
    ).arg(bgColor, fgColor, h1Color, h2Color, h3Color,
          borderColor, codeBgColor, codeFgColor, doxyTagColor, linkColor,
          cppKwColor, cppTypeColor, cppStrColor, cppCommentColor, cppPreprocColor,
          quoteColor, cppTodoColor);
}

void HoverPopup::adjustSizeToFit()
{
    // H1: 自适应弹窗大小 — 先给一个初始宽度让文档布局，再根据实际内容尺寸调整
    // 设置 textBrowser 的宽度约束，让 QTextDocument 在该宽度下排版
    const int minW = 300;
    const int maxW = 580;
    const int padding = 28;  // body padding 10+14 + 边距

    // 先用最大宽度让文档排版，计算理想宽度
    m_textBrowser->setFixedWidth(maxW - padding);
    // QTextDocument 在 setHtml 后同步布局，size() 可用
    int docWidth = m_textBrowser->document()->idealWidth();
    int docHeight = m_textBrowser->document()->size().height();

    // 目标宽度：取理想宽度与最小/最大宽度的钳制值
    int targetWidth = qMin(qMax(docWidth + padding, minW), maxW);
    // 如果理想宽度小于最大宽度，重新用目标宽度排版以获得准确高度
    if (targetWidth < maxW - padding) {
        m_textBrowser->setFixedWidth(targetWidth - padding);
        docHeight = m_textBrowser->document()->size().height();
    }

    // 目标高度：内容高度 + 上下 padding，钳制在 36~400
    int targetHeight = qMin(qMax(static_cast<int>(docHeight) + 20, 36), 400);

    // 恢复 textBrowser 的弹性宽度约束（撤销 setFixedWidth）
    m_textBrowser->setMinimumSize(0, 0);
    m_textBrowser->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    resize(targetWidth, targetHeight);
}
