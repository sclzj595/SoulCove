// M9/M3: Markdown 渲染管线边界用例测试（控制台，ctest 可挂）
// 覆盖用户必测清单：转义/嵌套/空行/表格行内标记/代码块原样/标题空格敏感
#include "core/markdown/MaddyParser.h"
#include "core/markdown/MarkdownParser.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include <functional>
#include <vector>

struct Case {
    const char* name;
    std::function<bool(const QString& html)> check;   // 返回 true = PASS
};

static int g_pass = 0, g_fail = 0;

static void run(const char* name, const QString& md,
                std::function<bool(const QString&)> check)
{
    MaddyParser parser;
    const QString html = parser.toHtml(md);
    const bool ok = check(html);
    if (ok) { ++g_pass; qInfo() << "[PASS]" << name; }
    else {
        ++g_fail;
        qWarning() << "[FAIL]" << name;
        qWarning() << "  input :" << md.left(120);
        qWarning() << "  output:" << html.left(400);
    }
}

static bool contains(const QString& html, const QString& needle) { return html.contains(needle, Qt::CaseInsensitive); }
static bool notContains(const QString& html, const QString& needle) { return !html.contains(needle, Qt::CaseInsensitive); }

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // 1. 转义：\* 不识别为粗体标记（CommonMark backslash escape）
    //    HTML 层验证：出现数字实体 &#42;（渲染层显示为 *），且无 <strong>
    run("escape/ascii-star", QStringLiteral("这是 \\* 不是粗体 \\* 结束"),
        [](const QString& h) { return notContains(h, "<strong>") && contains(h, "&#42;"); });

    // 2. 转义：中文标点（用户文档实际用法 0\、）→ 实体 &#12289;
    run("escape/cjk-punct", QStringLiteral("## 0\\、更新日志"),
        [](const QString& h) { return notContains(h, "\\、") && contains(h, "&#12289;"); });

    // 3. 转义还原不得污染代码块
    run("escape/code-block-preserved",
        QStringLiteral("```cpp\nint a = b \\* c; // \\*\n```\n"),
        [](const QString& h) { return contains(h, "\\* c;") && contains(h, "\\*"); });

    // 4. 代码块内 markdown 符号原样保留（高亮器会拆 span，验证关键内容完整且不解析）
    run("code/preserves-markdown",
        QStringLiteral("```\n# not heading **not bold** [x](y)\n```\n"),
        [](const QString& h) {
            return contains(h, "**not bold** [x](y)") && notContains(h, "<strong>not bold")
                && notContains(h, "<h1>");
        });

    // 5. 标题空格敏感：# 后无空格不是标题
    run("heading/requires-space", QStringLiteral("#no space heading\n"),
        [](const QString& h) { return notContains(h, "<h1>"); });

    // 6. 空行分段：两段文字应产生两个 <p>
    run("paragraph/empty-line-splits",
        QStringLiteral("第一段\n\n第二段\n"),
        [](const QString& h) { return int(h.count("<p>", Qt::CaseInsensitive) + h.count("<p ", Qt::CaseInsensitive)) >= 2; });

    // 7. 表格：单元格内行内标记（粗体/链接）
    run("table/inline-marks-in-cells",
        QStringLiteral("| A | B |\n| --- | --- |\n| **粗** | [链](https://x) |\n"),
        [](const QString& h) { return contains(h, "<strong>粗</strong>") && contains(h, "<a"); });

    // 8. 引用内列表（maddy 不支持 → 降级为引用内文字圆点，内容必须保留）
    run("quote/with-list",
        QStringLiteral("> 引导语\n> - 项目一\n> - 项目二\n"),
        [](const QString& h) {
            return contains(h, "blockquote") && contains(h, "项目一") && contains(h, "•");
        });

    // 8b. 探针：单行引用 + 带尾空行的引用（定位 maddy 空输出的边界）
    run("quote/probe-single", QStringLiteral("> 引导语\n"),
        [](const QString& h) { return contains(h, "blockquote") && contains(h, "引导语"); });
    run("quote/probe-tail-blank", QStringLiteral("> 引导语\n\n"),
        [](const QString& h) { return contains(h, "blockquote"); });

    // 9. 列表内代码块（嵌套，maddy 弱项探针）
    run("list/with-code-block",
        QStringLiteral("- 项 A\n\n  ```\n  code in list\n  ```\n"),
        [](const QString& h) { return contains(h, "code in list"); });

    // 10. 转义不影响行内代码内容
    run("escape/inline-code-preserved",
        QStringLiteral("用法：`a \\* b` 保持原样\n"),
        [](const QString& h) { return contains(h, "\\* b"); });

    qInfo() << "================================";
    qInfo() << "PASS" << g_pass << " FAIL" << g_fail;
    return g_fail == 0 ? 0 : 1;
}
