#include "core/markdown/MaddyParser.h"
#include "core/editor/CodeHighlighter.h"
#include "core/config/ThemeManager.h"

#include <maddy/parser.h>
#include <sstream>

QString MaddyParser::toHtml(const QString& markdown)
{
    // maddy 使用 std::stringstream 接口
    std::string input = markdown.toStdString();
    std::stringstream inStream(input);

    // maddy Parse: takes istream, returns string
    maddy::Parser parser;
    std::string rawHtml = parser.Parse(inStream);
    QString html = QString::fromStdString(rawHtml);

    // 代码块语法高亮 (对 <code class="language-xxx"> 着色为 <span class="hl-*">)
    html = CodeHighlighter::highlightHtml(html);

    // 注入主题感知 CSS (QTextBrowser 兼容的朴素 CSS2.1，颜色随当前主题)
    QString styled = QStringLiteral(
        "<html><head><meta charset=\"utf-8\"><style>%1</style></head>"
        "<body>%2</body></html>")
        .arg(defaultStyleSheet(), html);

    return styled;
}

QString MaddyParser::defaultStyleSheet()
{
    const auto& p = ThemeManager::instance().currentPalette();
    // 亮/暗主题判定 (与 ThemeManager/CommandPalette 一致: bgEditor.lightness() > 128)
    bool isLight = p.bgEditor.lightness() > 128;

    auto c = [](const QColor& color) -> QString {
        if (color.alpha() == 255) return color.name(QColor::HexRgb);
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };

    const QString accent      = c(p.accentPrimary);
    const QString fg          = c(p.fgPrimary);
    const QString fgSec       = c(p.fgSecondary);
    const QString border      = c(p.borderDefault);

    // 代码块/引用/表格配色 (随主题)
    const QString codeBg   = isLight ? QStringLiteral("#f5f5f7") : QStringLiteral("#282c34");
    const QString codeFg   = isLight ? QStringLiteral("#c64848") : QStringLiteral("#e06c75");
    const QString quoteBg  = isLight ? QStringLiteral("#fafafa") : QStringLiteral("rgba(255,255,255,0.04)");
    const QString thBg     = isLight ? QStringLiteral("#f0f0f2") : QStringLiteral("rgba(255,255,255,0.06)");

    // 代码高亮 token 配色 (暗色=VSCode Dark+, 亮色=VSCode Light+, 与 CodeSyntaxHighlighter 一致)
    QString hlKw, hlStr, hlNum, hlCmt, hlPp, hlType, hlFn;
    if (isLight) {
        hlKw  = QStringLiteral("#0000FF");
        hlStr = QStringLiteral("#A31515");
        hlNum = QStringLiteral("#098658");
        hlCmt = QStringLiteral("#008000");
        hlPp  = QStringLiteral("#AF00DB");
        hlType= QStringLiteral("#267F99");
        hlFn  = QStringLiteral("#795E26");
    } else {
        hlKw  = QStringLiteral("#569CD6");
        hlStr = QStringLiteral("#CE9178");
        hlNum = QStringLiteral("#B5CEA8");
        hlCmt = QStringLiteral("#6A9955");
        hlPp  = QStringLiteral("#C586C0");
        hlType= QStringLiteral("#4EC9B0");
        hlFn  = QStringLiteral("#DCDCAA");
    }

    // QTextBrowser 兼容的朴素 CSS2.1 (不用 var()/gradient/box-shadow/:hover/transition)
    return QStringLiteral(
        "body { font-family: 'Microsoft YaHei','Segoe UI',sans-serif; font-size: 14px; "
        "line-height: 1.7; color: %1; background-color: transparent; margin: 0; }"
        "h1,h2,h3,h4,h5,h6 { color: %2; font-weight: 600; line-height: 1.3; "
        "margin-top: 20px; margin-bottom: 8px; }"
        "h1 { font-size: 24px; border-bottom: 2px solid %3; padding-bottom: 6px; }"
        "h2 { font-size: 20px; border-bottom: 1px solid %3; padding-bottom: 4px; }"
        "h3 { font-size: 17px; }"
        "h4 { font-size: 15px; }"
        "h5,h6 { font-size: 14px; color: %4; }"
        "p { margin: 8px 0; }"
        "a { color: %2; text-decoration: none; }"
        "strong { font-weight: 700; }"
        "em { font-style: italic; }"
        "code { background-color: %5; color: %6; padding: 2px 5px; border-radius: 3px; "
        "font-family: 'Consolas','Courier New',monospace; font-size: 13px; }"
        "pre { background-color: %7; padding: 12px; border: 1px solid %3; border-radius: 6px; margin: 10px 0; }"
        "pre code { background-color: transparent; color: %1; padding: 0; border: none; "
        "display: block; white-space: pre; font-size: 13px; line-height: 1.5; }"
        "blockquote { border-left: 4px solid %2; padding: 6px 14px; margin: 10px 0; "
        "color: %4; background-color: %8; }"
        "table { border-collapse: collapse; margin: 10px 0; }"
        "th,td { border: 1px solid %3; padding: 6px 12px; }"
        "th { background-color: %9; color: %2; font-weight: 600; }"
        "hr { border: none; border-top: 1px solid %3; margin: 18px 0; }"
        "ul,ol { padding-left: 24px; margin: 6px 0; }"
        "li { margin: 3px 0; }"
        "img { max-width: 100%; }"
        ".hl-kw { color: %10; font-weight: 600; }"
        ".hl-str { color: %11; }"
        ".hl-num { color: %12; }"
        ".hl-cmt { color: %13; font-style: italic; }"
        ".hl-pp { color: %14; }"
        ".hl-type { color: %15; }"
        ".hl-fn { color: %16; }"
    ).arg(fg, accent, border, fgSec, codeBg, codeFg, codeBg, quoteBg, thBg,
          hlKw, hlStr, hlNum, hlCmt, hlPp, hlType, hlFn);
}
