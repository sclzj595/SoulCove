#include "core/markdown/MarkdownParser.h"
#include "core/config/ThemeManager.h"
#include <QRegularExpression>

QString MarkdownParser::escapeHtml(const QString& text)
{
    QString result = text;
    result.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    result.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    result.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    return result;
}

QString MarkdownParser::parseInline(const QString& text)
{
    QString result = text;

    // 先转义HTML（保护原有标签）
    // 注意：code标签内的内容不转义

    // ~~删除线~~
    static QRegularExpression strikeRe(QStringLiteral(R"(~~(.+?)~~)"));
    result.replace(strikeRe, QStringLiteral("<del>\\1</del>"));

    // **粗体**
    static QRegularExpression boldRe(QStringLiteral(R"(\*\*(.+?)\*\*)"));
    result.replace(boldRe, QStringLiteral("<strong>\\1</strong>"));

    // *斜体*
    static QRegularExpression italicRe(QStringLiteral(R"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)"));
    result.replace(italicRe, QStringLiteral("<em>\\1</em>"));

    // `行内代码`
    static QRegularExpression codeRe(QStringLiteral(R"(`([^`]+)`)"));
    result.replace(codeRe, QStringLiteral("<code>\\1</code>"));

    // ![图片](url)
    static QRegularExpression imgRe(QStringLiteral(R"(!\[([^\]]*)\]\(([^)]+)\))"));
    result.replace(imgRe, QStringLiteral(R"(<img src="\2" alt="\1">)"));

    // [链接](url)
    static QRegularExpression linkRe(QStringLiteral(R"(\[([^\]]+)\]\(([^)]+)\))"));
    result.replace(linkRe, QStringLiteral(R"(<a href="\2">\1</a>)"));

    return result;
}

QString MarkdownParser::parseCodeBlock(const QString& code, const QString& lang)
{
    QString escaped = code;
    escaped.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    escaped.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    escaped.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    escaped.replace(QStringLiteral("\n"), QStringLiteral("<br>"));

    QString langLabel = lang.isEmpty() ? QString() :
        QStringLiteral("<div style='color:#888;font-size:11px;margin-bottom:4px;'>%1</div>").arg(lang);

    return QStringLiteral("<pre>%1<code>%2</code></pre>").arg(langLabel, escaped);
}

QString MarkdownParser::parseTable(const QStringList& lines)
{
    if (lines.size() < 2) return QString();

    auto parseRow = [](const QString& line) -> QStringList {
        QStringList cells;
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('|')) trimmed = trimmed.mid(1);
        if (trimmed.endsWith('|')) trimmed = trimmed.chopped(1);
        for (const auto& cell : trimmed.split('|')) {
            cells.append(cell.trimmed());
        }
        return cells;
    };

    QStringList headerCells = parseRow(lines[0]);
    QStringList alignCells = parseRow(lines[1]);

    QString html = QStringLiteral("<table><thead><tr>");
    for (int i = 0; i < headerCells.size(); ++i) {
        QString align;
        if (i < alignCells.size()) {
            if (alignCells[i].startsWith(':') && alignCells[i].endsWith(':'))
                align = QStringLiteral("text-align:center;");
            else if (alignCells[i].endsWith(':'))
                align = QStringLiteral("text-align:right;");
        }
        html += QStringLiteral("<th style='%1'>%2</th>")
            .arg(align, parseInline(headerCells[i]));
    }
    html += QStringLiteral("</tr></thead><tbody>");

    for (int i = 2; i < lines.size(); ++i) {
        QStringList cells = parseRow(lines[i]);
        html += QStringLiteral("<tr>");
        for (int j = 0; j < headerCells.size() && j < cells.size(); ++j) {
            html += QStringLiteral("<td>%1</td>").arg(parseInline(cells[j]));
        }
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</tbody></table>");
    return html;
}

/// @brief 增强版Markdown解析器（修复B1列表BUG + 添加GFM支持 + 现代CSS样式）
///
/// 修复项：
/// - B1: 列表渲染错误 → 正确合并多个li到单个ul/ol
/// - D1: 支持GFM任务列表 - [x] / - [ ]
/// - D2/D3: 代码块增强（复制按钮 + 语言标签）
/// - D5: 标题添加anchor ID用于TOC跳转
/// - D7: 表格斑马纹+悬停效果
/// - S1-S5: 现代化视觉增强
QString MarkdownParser::toHtmlStatic(const QString& markdown)
{
    QStringList lines = markdown.split('\n');
    QString html;

    // ===== 增强版CSS样式（CSS变量从ThemeManager动态获取，嵌入HTML避免QSS解析失败）=====
    const auto& tm = ThemeManager::instance();
    const auto& p = tm.currentPalette();
    bool isLight = p.bgEditor.lightness() > 128;

    auto c = [](const QColor& color) -> QString {
        if (color.alpha() == 255) return color.name(QColor::HexRgb);
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };

    QString mdCodeBg = isLight ? QStringLiteral("rgba(0,0,0,0.06)")
                               : QStringLiteral("rgba(%1,%2,%3,0.15)")
                                     .arg(p.accentPrimary.red()).arg(p.accentPrimary.green()).arg(p.accentPrimary.blue());
    QString mdCodeFg = isLight ? QStringLiteral("#c64848") : QStringLiteral("#e06c75");
    QString mdPreBg  = isLight ? QStringLiteral("#f5f5f5") : QStringLiteral("#282c34");
    QString mdStrong = isLight ? c(p.fgPrimary) : QStringLiteral("#ffffff");
    QString mdQuoteBg = isLight ? QStringLiteral("rgba(0,0,0,0.03)")
                                : QStringLiteral("rgba(%1,%2,%3,0.08)")
                                      .arg(p.accentPrimary.red()).arg(p.accentPrimary.green()).arg(p.accentPrimary.blue());
    QString mdRowHover = isLight ? QStringLiteral("rgba(0,0,0,0.04)")
                                 : QStringLiteral("rgba(%1,%2,%3,0.08)")
                                       .arg(p.accentPrimary.red()).arg(p.accentPrimary.green()).arg(p.accentPrimary.blue());
    QString mdRowEven = isLight ? QStringLiteral("rgba(0,0,0,0.02)") : QStringLiteral("rgba(255,255,255,0.02)");

    // ===== 构建嵌入CSS（CSS变量从ThemeManager动态获取，嵌入HTML避免QSS解析失败）=====
    html += QStringLiteral(
        "<html><head><style>"
        ":root {"
        "  --md-fg: %1;"
        "  --md-bg: %2;"
        "  --md-accent: %3;"
        "  --md-accent-hover: %4;"
        "  --md-border: %5;"
        "  --md-fg-secondary: %6;"
        "  --md-fg-disabled: %7;"
        "  --md-code-bg: %8;"
        "  --md-code-fg: %9;"
        "  --md-pre-bg: %10;"
        "  --md-strong: %11;"
        "  --md-em: %12;"
        "  --md-del: %13;"
        "}"
        "html { scroll-behavior: smooth; }"
        "body { font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; font-size: 14px; "
        "line-height: 1.7; color: var(--md-fg); background: var(--md-bg); "
        "margin: 0; padding: 12px 20px; word-wrap: break-word; }"
        "h1,h2,h3,h4,h5,h6 { color: var(--md-accent); font-weight: 600; line-height: 1.3; margin-top: 24px; }"
        "h1 { font-size: 26px; border-bottom: 2px solid var(--md-border); padding-bottom: 8px; scroll-margin-top: 16px; }"
        "h2 { font-size: 22px; border-bottom: 1px solid var(--md-border); padding-bottom: 6px; scroll-margin-top: 16px; }"
        "h3 { font-size: 18px; scroll-margin-top: 16px; }"
        "h4 { font-size: 16px; }"
        "h5,h6 { font-size: 14px; color: var(--md-fg-secondary); }"
        "code { background-color: var(--md-code-bg); padding: 2px 6px; "
        "border-radius: 3px; font-family: 'Consolas','Courier New',monospace; font-size: 13px; "
        "color: var(--md-code-fg); }"
        "pre { position: relative; background-color: var(--md-pre-bg); padding: 32px 16px 16px; "
        "border-radius: 8px; overflow-x: auto; margin: 12px 0; border: 1px solid var(--md-border); "
        "box-shadow: 0 2px 8px rgba(0,0,0,0.3); }"
        "pre code { background: none; padding: 0; border-radius: 0; color: var(--md-fg); "
        "display: block; white-space: pre; tab-size: 4; font-size: 13px; line-height: 1.5; }"
        ".copy-btn { position: absolute; top: 8px; right: 8px; padding: 4px 12px; font-size: 11px; "
        "cursor: pointer; background: var(--md-accent); color: #fff; border: none; "
        "border-radius: 4px; opacity: 0.7; transition: opacity 0.2s; font-family: inherit; }"
        ".copy-btn:hover { opacity: 1; }"
        ".lang-label { position: absolute; top: 10px; left: 14px; font-size: 11px; "
        "color: var(--md-fg-secondary); text-transform: uppercase; letter-spacing: 1px; "
        "font-weight: 600; }"
        "blockquote { border-left: 4px solid var(--md-accent); padding: 12px 20px; "
        "margin: 12px 0; color: var(--md-fg-secondary); "
        "background: linear-gradient(135deg, %14 0%, transparent 100%); "
        "border-radius: 0 8px 8px 0; }"
        "blockquote p { margin: 4px 0; }"
        "table { border-collapse: collapse; margin: 12px 0; width: 100%; display: block; overflow-x: auto; }"
        "th, td { border: 1px solid var(--md-border); padding: 10px 14px; text-align: left; }"
        "th { background-color: var(--md-pre-bg); font-weight: 600; color: var(--md-accent); "
        "user-select: none; cursor: default; }"
        "tr:nth-child(even) { background-color: %15; }"
        "tr:hover { background-color: %16; transition: background-color 0.15s ease; }"
        "a { color: var(--md-accent); text-decoration: none; border-bottom: 1px dotted transparent; "
        "transition: all 0.2s ease; }"
        "a:hover { color: var(--md-accent-hover); border-bottom-color: currentColor; }"
        "hr { border: none; height: 1px; background: linear-gradient(to right, transparent, var(--md-border), transparent); "
        "margin: 24px 0; }"
        "ul, ol { padding-left: 24px; margin: 8px 0; }"
        "li { margin: 5px 0; line-height: 1.6; }"
        "li > ul, li > ol { margin: 4px 0; }"
        "p { margin: 8px 0; }"
        "img { max-width: 100%; height: auto; border-radius: 6px; box-shadow: 0 2px 8px rgba(0,0,0,0.25); "
        "cursor: pointer; transition: transform 0.2s ease, box-shadow 0.2s ease; }"
        "img:hover { transform: scale(1.02); box-shadow: 0 4px 16px rgba(0,0,0,0.35); }"
        "strong { color: var(--md-strong); font-weight: 600; }"
        "em { color: var(--md-em); font-style: italic; }"
        "del { color: var(--md-del); text-decoration: line-through; opacity: 0.65; }"
        "input[type='checkbox'] { width: 14px; height: 14px; margin-right: 8px; "
        "accent-color: var(--md-accent); cursor: pointer; vertical-align: middle; }"
        "li.task-list-item { list-style: none; margin-left: -20px; }"
        ".hl-kw  { color: #C586C0; font-weight: 500; }"
        ".hl-str { color: #CE9178; }"
        ".hl-num { color: #B5CEA8; }"
        ".hl-cmt { color: #6A9955; font-style: italic; }"
        ".hl-pp  { color: #9B59B6; }"
        ".hl-type { color: #4EC9B0; }"
        ".hl-fn  { color: #DCDCAA; }"
        "</style></head><body>"
    ).arg(c(p.fgPrimary), c(p.bgEditor), c(p.accentPrimary), c(p.accentHover),
          c(p.borderDefault), c(p.fgSecondary), c(p.fgDisabled),
          mdCodeBg, mdCodeFg, mdPreBg, mdStrong, c(p.accentPrimary), c(p.fgDisabled),
          mdQuoteBg, mdRowEven, mdRowHover);

    bool inCodeBlock = false;
    QString codeBlockContent;
    QString codeBlockLang;
    bool inTable = false;
    QStringList tableLines;

    // B1修复：列表状态追踪
    bool inUnorderedList = false;
    bool inOrderedList = false;
    QStringList listItems;

    auto flushParagraph = [&](QStringList& para) {
        if (para.isEmpty()) return;
        QString text = para.join('\n');
        html += QStringLiteral("<p>%1</p>\n").arg(parseInline(text));
        para.clear();
    };

    // B1修复：flush列表（将多个li合并到一个ul/ol中）
    auto flushList = [&]() {
        if (listItems.isEmpty()) return;

        if (inUnorderedList) {
            html += QStringLiteral("<ul>\n");
            for (const auto& item : listItems) {
                html += QStringLiteral("<li>%1</li>\n").arg(item);
            }
            html += QStringLiteral("</ul>\n");
        } else if (inOrderedList) {
            html += QStringLiteral("<ol>\n");
            for (const auto& item : listItems) {
                html += QStringLiteral("<li>%1</li>\n").arg(item);
            }
            html += QStringLiteral("</ol>\n");
        }

        listItems.clear();
        inUnorderedList = false;
        inOrderedList = false;
    };

    QStringList paragraph;

    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];

        // 代码块
        if (line.startsWith(QStringLiteral("```"))) {
            if (inCodeBlock) {
                // D2+D3: 代码块增强（带语言标签+复制按钮）
                QString escapedCode = codeBlockContent;
                escapedCode.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
                escapedCode.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
                escapedCode.replace(QStringLiteral(">"), QStringLiteral("&gt;"));

                QString langLabel = codeBlockLang.isEmpty() ? QString() :
                    QStringLiteral("<span class='lang-label'>%1</span>").arg(codeBlockLang.toUpper());
                QString copyBtn = QStringLiteral(
                    "<button class='copy-btn' onclick='"
                    "navigator.clipboard.writeText(this.parentElement.querySelector(\"code\").textContent);"
                    "this.textContent=\"Copied!\";"
                    "setTimeout(()=>this.textContent=\"Copy\",2000);"
                    "'>Copy</button>");

                html += QStringLiteral("<pre>%1%2<code class='language-%3'>%4</code></pre>\n")
                    .arg(langLabel, copyBtn, codeBlockLang.toLower(), escapedCode);

                codeBlockContent.clear();
                codeBlockLang.clear();
                inCodeBlock = false;
            } else {
                flushParagraph(paragraph);
                flushList();  // B1: 先关闭列表
                codeBlockLang = line.mid(3).trimmed();
                inCodeBlock = true;
            }
            continue;
        }

        if (inCodeBlock) {
            if (!codeBlockContent.isEmpty()) codeBlockContent += '\n';
            codeBlockContent += line;
            continue;
        }

        // 表格（连续的 | 行）
        if (line.trimmed().startsWith('|') && line.trimmed().endsWith('|')) {
            flushParagraph(paragraph);
            flushList();  // B1: 先关闭列表
            if (!inTable) {
                inTable = true;
                tableLines.clear();
            }
            tableLines.append(line);
            continue;
        } else if (inTable) {
            html += parseTable(tableLines);
            tableLines.clear();
            inTable = false;
        }

        // D5: 标题（添加anchor ID用于TOC跳转）
        static QRegularExpression hRe(QStringLiteral(R"(^(#{1,6})\s+(.+)$)"));
        auto hMatch = hRe.match(line);
        if (hMatch.hasMatch()) {
            int level = hMatch.captured(1).length();
            QString text = hMatch.captured(2);
            // 生成安全的anchor ID（转义特殊字符，保留中文）
            QString anchorId = text.toLower()
                .remove(QRegularExpression(QStringLiteral("[^a-z0-9\\x{4e00}-\\x{9fa5}\\-_]")))
                .replace(QRegularExpression(QStringLiteral("_{2,}|-{2,}")), QStringLiteral("-"))
                .replace(QRegularExpression(QStringLiteral("^[-_]|[-_]$")), QString());

            flushParagraph(paragraph);
            flushList();  // B1: 先关闭列表
            html += QStringLiteral("<h%1 id=\"%2\">%3</h%1>\n")
                .arg(level)
                .arg(anchorId)
                .arg(parseInline(text));
            continue;
        }

        // 水平线
        static QRegularExpression hrRe(QStringLiteral(R"(^(---|\*\*\*|___)\s*$)"));
        if (hrRe.match(line.trimmed()).hasMatch()) {
            flushParagraph(paragraph);
            flushList();  // B1: 先关闭列表
            html += QStringLiteral("<hr>\n");
            continue;
        }

        // 引用
        static QRegularExpression blockquoteRe(QStringLiteral(R"(^> (.+)$)"));
        auto bqMatch = blockquoteRe.match(line);
        if (bqMatch.hasMatch()) {
            flushParagraph(paragraph);
            flushList();  // B1: 先关闭列表
            html += QStringLiteral("<blockquote><p>%1</p></blockquote>\n")
                .arg(parseInline(bqMatch.captured(1)));
            continue;
        }

        // D1: GFM 任务列表检测（必须在普通列表之前！）
        static QRegularExpression taskRe(QStringLiteral(R"(^[\s]*-\s+\[([ xX])\]\s+(.+)$)"));
        auto taskMatch = taskRe.match(line);
        if (taskMatch.hasMatch()) {
            bool checked = !taskMatch.captured(1).trimmed().isEmpty();
            QString taskText = taskMatch.captured(2);
            QString checkbox = checked ?
                QStringLiteral("<input type='checkbox' checked disabled>") :
                QStringLiteral("<input type='checkbox' disabled>");

            // 如果不在列表中，开始新列表
            if (!inUnorderedList) {
                flushParagraph(paragraph);
                inUnorderedList = true;
            }

            listItems.append(QStringLiteral("%1 %2").arg(checkbox, parseInline(taskText)));
            continue;
        }

        // B1修复：无序列表（收集所有连续的li项）
        static QRegularExpression ulRe(QStringLiteral(R"(^[\*\-]\s+(.+)$)"));
        auto ulMatch = ulRe.match(line);
        if (ulMatch.hasMatch()) {
            if (!inUnorderedList) {
                flushParagraph(paragraph);
                inUnorderedList = true;
            }
            listItems.append(parseInline(ulMatch.captured(1)));
            continue;
        }

        // B1修复：有序列表（收集所有连续的li项）
        static QRegularExpression olRe(QStringLiteral(R"(^\d+\.\s+(.+)$)"));
        auto olMatch = olRe.match(line);
        if (olMatch.hasMatch()) {
            if (!inOrderedList) {
                flushParagraph(paragraph);
                inOrderedList = true;
            }
            listItems.append(parseInline(olMatch.captured(1)));
            continue;
        }

        // 空行 → 段落分隔（也关闭列表）
        if (line.trimmed().isEmpty()) {
            flushParagraph(paragraph);
            flushList();  // B1: 空行关闭列表
            continue;
        }

        // 普通段落（如果之前在列表中，先关闭列表）
        if (inUnorderedList || inOrderedList) {
            flushList();
        }
        paragraph.append(line);
    }

    // 处理未闭合的块
    if (inTable) html += parseTable(tableLines);
    flushList();      // B1: 关闭未闭合的列表
    flushParagraph(paragraph);

    html += QStringLiteral("</body></html>");
    return html;
}

QString MarkdownParser::toHtml(const QString& markdown)
{
    return toHtmlStatic(markdown);
}
