#include "core/markdown/MdExporter.h"
#include "Logger.hpp"
#include "interfaces/markdown/IMarkdownParser.h"

#include <QFile>
#include <QTextDocument>
#include <QPrinter>
#include <QPageLayout>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QRegularExpression>

// ============================================================
// 构造函数
// ============================================================

MdExporter::MdExporter(QObject* parent)
    : QObject(parent)
{
}

void MdExporter::setParser(IMarkdownParser* parser)
{
    m_parser = parser;
}

// ============================================================
// 公共接口
// ============================================================

bool MdExporter::exportToFile(const QString& markdown,
                              const QString& filePath,
                              const ExportOptions& options)
{
    if (markdown.isEmpty()) {
        LOG_WARN_S("MdExporter", "exportToFile", "Markdown内容为空");
        emit exportFinished(false, tr("Markdown内容为空"));
        return false;
    }

    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();

    emit exportProgress(10);

    // 根据扩展名确定导出格式
    if (suffix == QStringLiteral("html") || suffix == QStringLiteral("htm")) {
        return exportHtml(markdown, filePath, options);
    } else if (suffix == QStringLiteral("pdf")) {
        return exportPdf(markdown, filePath, options);
    } else {
        LOG_WARN_S("MdExporter", "exportToFile", "不支持的文件格式:" << suffix);
        emit exportFinished(false, tr("不支持的文件格式: %1").arg(suffix));
        return false;
    }
}

QString MdExporter::toHtmlString(const QString& markdown,
                                  const ExportOptions& options)
{
    // 使用内置MarkdownParser转换（如果未设置外部解析器）
    QString htmlBody;

    if (m_parser) {
        htmlBody = m_parser->toHtml(markdown);
    } else {
        // Fallback: 简单包装
        htmlBody = QStringLiteral("<p>%1</p>").arg(markdown.toHtmlEscaped());
    }

    return generateHtmlDocument(htmlBody, options);
}

// ============================================================
// P3-M02 子项4: 复制为富文本
// 与 toHtmlString 的差异：
// - 关闭 TOC 导航（剪贴板富文本不需要目录）
// - 不带页脚（粘贴到 Word/邮件时无干扰）
// - 使用与导出一致的 inline-style HTML，保证 QTextDocument/QClipboard 渲染保真
// ============================================================

QString MdExporter::copyAsRichText(const QString& markdown)
{
    QString htmlBody;

    if (m_parser) {
        htmlBody = m_parser->toHtml(markdown);
    } else {
        // Fallback: 简单包装（行内 code/标题等格式丢失但内容保留）
        htmlBody = QStringLiteral("<pre>%1</pre>").arg(markdown.toHtmlEscaped());
    }

    // 关闭 TOC，避免在富文本中插入导航块（Word/邮件不友好）
    ExportOptions opts;
    opts.includeToc = false;

    // 复用 generateHtmlDocument 的内联样式生成逻辑（含 h1~h6/code/pre/table 等完整样式）
    return generateHtmlDocument(htmlBody, opts);
}

QString MdExporter::fileFilter()
{
    return QStringLiteral(
        "HTML 文件 (*.html *.htm);;"
        "PDF 文档 (*.pdf);;"
        "所有支持格式 (*.html *.htm *.pdf)"
    );
}

// ============================================================
// 私有方法：HTML导出
// ============================================================

bool MdExporter::exportHtml(const QString& markdown,
                            const QString& filePath,
                            const ExportOptions& options)
{
    emit exportProgress(30);

    // 转换Markdown → HTML
    QString htmlBody;
    if (m_parser) {
        htmlBody = m_parser->toHtml(markdown);
    } else {
        htmlBody = QStringLiteral("<pre>%1</pre>").arg(markdown.toHtmlEscaped());
    }

    emit exportProgress(50);

    // 生成完整HTML文档（带独立CSS）
    QString fullHtml = generateHtmlDocument(htmlBody, options);

    emit exportProgress(70);

    // 写入文件
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN_S("MdExporter", "exportHtml", "无法创建文件:" << filePath << file.errorString());
        emit exportFinished(false, tr("无法创建文件: %1").arg(file.errorString()));
        return false;
    }

    QByteArray data = fullHtml.toUtf8();
    qint64 written = file.write(data);
    file.close();

    if (written != data.size()) {
        LOG_WARN_S("MdExporter", "exportHtml", "写入不完整:" << written << "/" << data.size());
        emit exportFinished(false, tr("文件写入不完整"));
        return false;
    }

    emit exportProgress(100);
    LOG_DEBUG_S("MdExporter", "exportHtml", "HTML导出成功:" << filePath << "(" << data.size() / 1024 << "KB)");
    emit exportFinished(true, tr("HTML导出成功"));

    return true;
}

// ============================================================
// 私有方法：PDF导出
// ============================================================

bool MdExporter::exportPdf(const QString& markdown,
                           const QString& filePath,
                           const ExportOptions& options)
{
    emit exportProgress(30);

    // 1. Markdown → HTML
    QString htmlBody;
    if (m_parser) {
        htmlBody = m_parser->toHtml(markdown);
    } else {
        htmlBody = QStringLiteral("<pre>%1</pre>").arg(markdown.toHtmlEscaped());
    }

    emit exportProgress(50);

    // 2. 生成QTextDocument友好的HTML（pt单位，无浏览器专用CSS）
    QString fullHtml = generatePdfHtmlDocument(htmlBody, options);

    emit exportProgress(70);

    // 3. 用QTextDocument渲染HTML → QPrinter输出PDF
    QTextDocument doc;
    // 设置默认字体（14pt，作为未带内联样式的文本的后备字号）
    QFont defaultFont(QStringLiteral("Microsoft YaHei"), 14);
    defaultFont.setStyleHint(QFont::SansSerif);
    doc.setDefaultFont(defaultFont);
    doc.setHtml(fullHtml);
    // 注意：setHtml 后不再重设 defaultFont，因为已解析的内容不会受影响

    // 设置纸张为A4，合理的页边距
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);
    printer.setColorMode(QPrinter::Color);

    // 渲染到PDF文件
    doc.print(&printer);

    emit exportProgress(100);
    LOG_DEBUG_S("MdExporter", "exportPdf", "PDF导出成功:" << filePath);
    emit exportFinished(true, tr("PDF导出成功"));

    return true;
}

// ============================================================
// 私有方法：HTML文档生成
// ============================================================

QString MdExporter::generateHtmlDocument(const QString& htmlBody,
                                         const ExportOptions& options) const
{
    QString title = options.title.isEmpty() ? tr("Markdown Document") : options.title;
    QDateTime now = QDateTime::currentDateTime();

    // 完整的独立HTML文档（内嵌所有CSS，无需外部依赖）
    return QStringLiteral(
        "<!DOCTYPE html>\n"
        "<html lang=\"zh-CN\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>%1</title>\n"
        "  <meta name=\"author\" content=\"%2\">\n"
        "  <meta name=\"generator\" content=\"SoulCove Markdown Exporter\">\n"
        "  <meta name=\"date\" content=\"%3\">\n"
        "\n"
        "  <style>\n"
        "    /* ===== Reset & Base ===== */\n"
        "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
        "\n"
        "    body {\n"
        "      font-family: 'Microsoft YaHei', 'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif;\n"
        "      font-size: 14px;\n"
        "      line-height: 1.7;\n"
        "      color: #2c3e50;\n"
        "      background-color: #ffffff;\n"
        "      max-width: 900px;\n"
        "      margin: 0 auto;\n"
        "      padding: 40px 30px;\n"
        "    }\n"
        "\n"
        "    /* ===== Typography ===== */\n"
        "    h1, h2, h3, h4, h5, h6 {\n"
        "      color: #2c3e50;\n"
        "      font-weight: 600;\n"
        "      line-height: 1.3;\n"
        "      margin-top: 1.8em;\n"
        "      margin-bottom: 0.6em;\n"
        "      page-break-after: avoid;\n"  // PDF分页控制
        "    }\n"
        "    h1 { font-size: 22px; border-bottom: 2px solid #9B59B6; padding-bottom: 8px; color: #8e44ad; }\n"
    "    h2 { font-size: 19px; border-bottom: 1px solid #e0d0e8; padding-bottom: 6px; color: #9B59B6; margin-top: 1.5em; }\n"
    "    h3 { font-size: 17px; color: #7d3c98; margin-top: 1.3em; }\n"
    "    h4 { font-size: 15px; color: #666; margin-top: 1.2em; }\n"
    "    h5, h6 { font-size: 14px; color: #888; font-weight: 600; }\n"
        "\n"
        "    p { margin: 0.8em 0; text-align: justify; }\n"
        "\n"
        "    /* ===== Links ===== */\n"
        "    a { color: #9B59B6; text-decoration: none; border-bottom: 1px dotted #9B59B6; }\n"
        "    a:hover { color: #8e44ad; border-bottom-style: solid; }\n"
        "\n"
        "    /* ===== Code ===== */\n"
    "    code {\n"
    "      background-color: #f0eef0;\n"
    "      padding: 2px 6px;\n"
    "      border-radius: 3px;\n"
    "      font-family: 'Consolas', 'Courier New', monospace;\n"
    "      font-size: 13px;\n"
    "      color: #c7254e;\n"
    "    }\n"
    "\n"
    "    pre {\n"
    "      background-color: #2d2d2d;\n"
    "      color: #cccccc;\n"
    "      padding: 16px 18px;\n"
    "      border-radius: 6px;\n"
    "      overflow-x: auto;\n"
    "      margin: 1em 0;\n"
    "      border: 1px solid #444;\n"
    "      page-break-inside: avoid;\n"  // PDF:代码块不分页
    "    }\n"
    "    pre code {\n"
    "      background: none;\n"
    "      padding: 0;\n"
    "      border-radius: 0;\n"
    "      color: #cccccc;\n"
    "      font-size: 12.5px;\n"
    "      line-height: 1.6;\n"
    "    }\n"
        "\n"
        "    /* ===== Blockquote ===== */\n"
        "    blockquote {\n"
        "      border-left: 4px solid #9B59B6;\n"
        "      padding: 12px 20px;\n"
        "      margin: 1.2em 0;\n"
        "      color: #666;\n"
        "      background: linear-gradient(135deg, rgba(155,89,182,0.06) 0%, transparent 100%);\n"
        "      border-radius: 0 8px 8px 0;\n"
        "    }\n"
        "\n"
        "    /* ===== Tables ===== */\n"
        "    table {\n"
        "      border-collapse: collapse;\n"
        "      width: 100%;\n"
        "      margin: 1.2em 0;\n"
        "      page-break-inside: avoid;\n"
        "    }\n"
        "    th, td {\n"
        "      border: 1px solid #ddd;\n"
        "      padding: 10px 14px;\n"
        "      text-align: left;\n"
        "    }\n"
        "    th {\n"
        "      background-color: #f8f9fa;\n"
        "      font-weight: 600;\n"
        "      color: #9B59B6;\n"
        "    }\n"
        "    tr:nth-child(even) { background-color: #fafafa; }\n"
        "\n"
        "    /* ===== Lists ===== */\n"
        "    ul, ol { padding-left: 28px; margin: 0.8em 0; }\n"
        "    li { margin: 5px 0; line-height: 1.6; }\n"
        "\n"
        "    /* ===== Horizontal Rule ===== */\n"
        "    hr {\n"
        "      border: none;\n"
        "      height: 1px;\n"
        "      background: linear-gradient(to right, transparent, #ccc, transparent);\n"
        "      margin: 2em 0;\n"
        "    }\n"
        "\n"
        "    /* ===== Images ===== */\n"
        "    img {\n"
        "      max-width: 100%;\n"
        "      height: auto;\n"
        "      border-radius: 6px;\n"
        "      box-shadow: 0 2px 8px rgba(0,0,0,0.1);\n"
        "      margin: 1em 0;\n"
        "    }\n"
        "\n"
        "    /* ===== Strong & Emphasis ===== */\n"
        "    strong { color: #2c3e50; font-weight: 600; }\n"
        "    em { color: #9B59B6; font-style: italic; }\n"
        "    del { color: #999; text-decoration: line-through; opacity: 0.65; }\n"
        "\n"
        "    /* ===== Task List (GFM) ===== */\n"
        "    input[type='checkbox'] {\n"
        "      width: 14px; height: 14px;\n"
        "      margin-right: 8px;\n"
        "      accent-color: #9B59B6;\n"
        "    }\n"
        "\n"
        "    /* ===== Print Styles (PDF优化）===== */\n"
        "    @media print {\n"
        "      body { max-width: 100%; padding: 20px; }\n"
        "      pre, blockquote, table, img { page-break-inside: avoid; }\n"
        "      h1, h2, h3 { page-break-after: avoid; }\n"
        "      a { color: #000; text-decoration: underline; }\n"
        "      a[href]::after { content: \" (\" attr(href) \")\"; font-size: 80%; color: #666; }\n"
        "    }\n"
        "\n"
        "    /* ===== Footer ===== */\n"
        "    .footer {\n"
        "      margin-top: 4em;\n"
        "      padding-top: 1.5em;\n"
        "      border-top: 1px solid #eee;\n"
        "      text-align: center;\n"
        "      font-size: 12px;\n"
        "      color: #999;\n"
        "    }\n"
        "  </style>\n"
        "</head>\n"
        "\n"
        "<body>\n"
        "%4\n"  // 可选：目录导航
        "%5\n"  // 正文内容
        "  <div class='footer'>\n"
        "    <p>Generated by SoulCove | %3</p>\n"
        "  </div>\n"
        "</body>\n"
        "</html>"
    )
    .arg(title.toHtmlEscaped())
    .arg(options.author.toHtmlEscaped())
    .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
    .arg(options.includeToc ? generateTocFromHtml(htmlBody) : QString())
    .arg(htmlBody);
}

// ============================================================
// 私有方法：PDF专用HTML生成（QTextDocument友好）
// 关键：全部使用 pt 单位（分辨率无关），去掉QTextDocument不支持的CSS
// ============================================================

QString MdExporter::generatePdfHtmlDocument(const QString& htmlBody,
                                            const ExportOptions& options) const
{
    QString title = options.title.isEmpty() ? tr("Markdown Document") : options.title;
    QDateTime now = QDateTime::currentDateTime();

    // ============================================================
    // 预处理HTML：给所有关键标签加内联样式
    // 原因：QTextDocument 对 CSS 选择器支持不可靠，只有 inline style 才能保证生效
    // 风格：参考 GitHub Markdown 渲染风格，与 HTML 导出保持一致
    // 关键：所有元素强制 text-align:left，防止 QTextDocument 默认居中
    // ============================================================
    QString processedBody = htmlBody;

    // ---- h1~h6：GitHub风格标题层级 ----
    // h1=18pt 紫色粗体+下划线 / h2=15pt 紫色+下划线 / h3=13pt 深紫 / h4=12pt 灰 / h5=h6=11pt 浅灰
    static const QRegularExpression h1Re(QStringLiteral("<h1(?![^>]*style)([^>]*)>"));
    processedBody.replace(h1Re, QStringLiteral("<h1\\1 style=\"font-size:18pt;color:#8e44ad;"
        "font-weight:bold;border-bottom:2px solid #9B59B6;padding-bottom:4pt;"
        "margin-top:18pt;margin-bottom:8pt;text-align:left;\">"));
    static const QRegularExpression h2Re(QStringLiteral("<h2(?![^>]*style)([^>]*)>"));
    processedBody.replace(h2Re, QStringLiteral("<h2\\1 style=\"font-size:15pt;color:#9B59B6;"
        "font-weight:bold;border-bottom:1px solid #e0d0e8;padding-bottom:3pt;"
        "margin-top:16pt;margin-bottom:6pt;text-align:left;\">"));
    static const QRegularExpression h3Re(QStringLiteral("<h3(?![^>]*style)([^>]*)>"));
    processedBody.replace(h3Re, QStringLiteral("<h3\\1 style=\"font-size:13pt;color:#7d3c98;"
        "font-weight:bold;margin-top:14pt;margin-bottom:5pt;text-align:left;\">"));
    static const QRegularExpression h4Re(QStringLiteral("<h4(?![^>]*style)([^>]*)>"));
    processedBody.replace(h4Re, QStringLiteral("<h4\\1 style=\"font-size:12pt;color:#666666;"
        "font-weight:bold;margin-top:12pt;margin-bottom:4pt;text-align:left;\">"));
    static const QRegularExpression h56Re(QStringLiteral("<h([56])(?![^>]*style)([^>]*)>"));
    processedBody.replace(h56Re, QStringLiteral("<h\\1\\2 style=\"font-size:11pt;color:#888888;"
        "font-weight:bold;margin-top:10pt;margin-bottom:3pt;text-align:left;\">"));

    // ---- p 段落：左对齐（不设 font-size，让 defaultFont 14pt 兜底）----
    // 原因：QTextDocument 对 <p> 和 <body> 的 CSS font-size 可能误解析为 px，
    // 导致打印时极小。QFont 的 pointSize 是正确的物理字号，作为兜底最可靠。
    processedBody.replace(QStringLiteral("<p>"),
        QStringLiteral("<p style=\"margin:6pt 0;text-align:left;color:#2c3e50;\">"));

    // ---- 行内 code：浅紫底+红字（GitHub风格）----
    // 匹配 <code> 和 <code class='...'>，保留 class 属性
    static const QRegularExpression codeRe(QStringLiteral("<code(\\s+class=['\"][^'\"]*['\"])?(?![^>]*style)>"));
    processedBody.replace(codeRe, QStringLiteral("<code\\1 style=\"font-family:'Consolas','Courier New',monospace;"
                       "font-size:11pt;color:#c7254e;background-color:#f0eef0;"
                       "padding:1px 4px;text-align:left;\">"));

    // ---- 代码块 pre：浅灰底+深色字（打印友好，对比度高）----
    processedBody.replace(QStringLiteral("<pre>"),
        QStringLiteral("<pre style=\"font-family:'Consolas','Courier New',monospace;"
                       "font-size:11pt;color:#1a1a1a;background-color:#f6f8fa;"
                       "border:1px solid #d0d7de;margin:10pt 0;padding:12pt;"
                       "text-align:left;white-space:pre-wrap;\">"));
    processedBody.replace(QStringLiteral("<pre "),
        QStringLiteral("<pre style=\"font-family:'Consolas','Courier New',monospace;"
                       "font-size:11pt;color:#1a1a1a;background-color:#f6f8fa;"
                       "border:1px solid #d0d7de;margin:10pt 0;padding:12pt;"
                       "text-align:left;white-space:pre-wrap;\" "));
    // pre 内的 code → 覆盖行内code样式，继承 pre 的深色字
    static const QRegularExpression preCodeRe(QStringLiteral(
        "(<pre[^>]*>)<code(\\s+class=['\"][^'\"]*['\"])?(?![^>]*style)>"));
    processedBody.replace(preCodeRe, QStringLiteral(
        "\\1<code\\2 style=\"font-family:'Consolas','Courier New',monospace;"
        "font-size:11pt;color:#1a1a1a;background-color:transparent;"
        "padding:0;text-align:left;\">"));

    // ---- 表格：GitHub风格 ----
    // td → 12pt 左对齐 + 浅灰边框
    processedBody.replace(QStringLiteral("<td>"),
        QStringLiteral("<td style=\"font-size:12pt;padding:6pt 10pt;"
                       "border:1px solid #d0d7de;text-align:left;color:#2c3e50;\">"));
    // th → 12pt 左对齐 + 浅灰底 + 紫色字（合并已有 style 属性）
    {
        static const QRegularExpression thRe(QStringLiteral(
            "<th([^>]*?)(?:style=['\"]([^'\"]*)['\"])?([^>]*)>"));
        QRegularExpressionMatchIterator it = thRe.globalMatch(processedBody);
        QString result;
        int lastEnd = 0;
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            result += processedBody.mid(lastEnd, m.capturedStart() - lastEnd);
            QString before = m.captured(1);
            QString oldStyle = m.captured(2);
            QString after = m.captured(3);
            QString newStyle = QStringLiteral(
                "font-size:12pt;padding:6pt 10pt;border:1px solid #d0d7de;"
                "background-color:#f6f8fa;font-weight:bold;color:#9B59B6;text-align:left;");
            if (!oldStyle.isEmpty()) {
                newStyle = oldStyle + QStringLiteral(";") + newStyle;
            }
            result += QStringLiteral("<th%1 style=\"%2\"%3>").arg(before, newStyle, after);
            lastEnd = m.capturedEnd();
        }
        result += processedBody.mid(lastEnd);
        processedBody = result;
    }

    // ---- blockquote 内的 p → 覆盖为引用样式（不设 font-size，用 defaultFont）----
    processedBody.replace(QStringLiteral("<blockquote><p"),
        QStringLiteral("<blockquote><p style=\"margin:0;text-align:left;color:#666;\""));

    // ---- li 列表项 → 左对齐（不设 font-size，用 defaultFont）----
    processedBody.replace(QStringLiteral("<li>"),
        QStringLiteral("<li style=\"text-align:left;color:#2c3e50;margin:3pt 0;\">"));

    // ---- 关键修复：用 <span> 包裹 <p> 和 <li> 的文本内容，强制字号 ----
    // 原因：QTextDocument 对 <p> 标签的 inline font-size 不生效（block级元素），
    // 但对 <span> 的 inline font-size 生效（char级元素）。
    // 用正则匹配所有 <p style="..."> 开标签，在其后插入 <span style="font-size:14pt;">
    static const QRegularExpression pOpenRe(QStringLiteral("(<p style=\"[^\"]*\">)"));
    processedBody.replace(pOpenRe, QStringLiteral("\\1<span style=\"font-size:14pt;\">"));
    processedBody.replace(QStringLiteral("</p>"), QStringLiteral("</span></p>"));
    // 同理处理 <li>
    static const QRegularExpression liOpenRe(QStringLiteral("(<li style=\"[^\"]*\">)"));
    processedBody.replace(liOpenRe, QStringLiteral("\\1<span style=\"font-size:14pt;\">"));
    processedBody.replace(QStringLiteral("</li>"), QStringLiteral("</span></li>"));

    // ============================================================
    // 全局 CSS（作为后备，主要靠内联样式）
    // 参考 GitHub Markdown 配色
    // ============================================================
    return QStringLiteral(
        "<html><head><meta charset=\"UTF-8\"><style>\n"
        "body {\n"
        "  font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif;\n"
        "  color: #2c3e50;\n"
        "  text-align: left;\n"
        "  margin: 0;\n"
        "  padding: 0;\n"
        "}\n"
        "\n"
        "h1, h2, h3, h4, h5, h6 {\n"
        "  font-weight: bold;\n"
        "  text-align: left;\n"
        "  margin-top: 16pt;\n"
        "  margin-bottom: 6pt;\n"
        "}\n"
        "h1 { font-size: 18pt; color: #8e44ad; border-bottom: 2px solid #9B59B6; padding-bottom: 4pt; }\n"
        "h2 { font-size: 15pt; color: #9B59B6; border-bottom: 1px solid #e0d0e8; padding-bottom: 3pt; }\n"
        "h3 { font-size: 13pt; color: #7d3c98; }\n"
        "h4 { font-size: 12pt; color: #666; }\n"
        "h5, h6 { font-size: 11pt; color: #888; }\n"
        "\n"
        "p { margin: 6pt 0; text-align: left; }\n"
        "a { color: #9B59B6; text-decoration: underline; }\n"
        "\n"
        "blockquote {\n"
        "  border-left: 4px solid #9B59B6;\n"
        "  margin: 10pt 0;\n"
        "  padding: 8pt 14pt;\n"
        "  color: #666;\n"
        "  background-color: #faf8fa;\n"
        "  text-align: left;\n"
        "}\n"
        "\n"
        "table { border-collapse: collapse; width: 100%; margin: 10pt 0; text-align: left; }\n"
        "\n"
        "ul, ol { margin: 6pt 0; padding-left: 22pt; text-align: left; }\n"
        "li { margin: 3pt 0; text-align: left; }\n"
        "hr { border: none; border-top: 1px solid #d0d7de; margin: 14pt 0; }\n"
        "img { max-width: 100%; }\n"
        "strong { font-weight: bold; }\n"
        "em { font-style: italic; }\n"
        "del { text-decoration: line-through; color: #999; }\n"
        "\n"
        ".footer {\n"
        "  margin-top: 20pt;\n"
        "  border-top: 1px solid #eeeeee;\n"
        "  text-align: center;\n"
        "  font-size: 9pt;\n"
        "  color: #999;\n"
        "}\n"
        "</style></head>\n"
        "<body style=\"font-family:'Microsoft YaHei','Segoe UI',sans-serif;color:#2c3e50;text-align:left;\">\n"
        "%1\n"
        "%2\n"
        "  <div class='footer'><p style=\"font-size:9pt;text-align:center;color:#999;\">Generated by SoulCove | %3</p></div>\n"
        "</body></html>"
    )
    .arg(options.includeToc ? generateTocFromHtml(htmlBody) : QString())
    .arg(processedBody)
    .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

// ============================================================
// 辅助：从HTML中提取标题生成TOC
// ============================================================

QString MdExporter::generateTocFromHtml(const QString& htmlBody)
{
    // 使用正则提取所有<h1>-<h6>标签及其内容
    static QRegularExpression headingRe(
        QStringLiteral(R"(<h([1-6])[^>]*>([^<]+)</h[1-6]>)"),
        QRegularExpression::CaseInsensitiveOption
    );

    QStringList tocItems;
    QRegularExpressionMatchIterator it = headingRe.globalMatch(htmlBody);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int level = match.captured(1).toInt();
        QString title = match.captured(2);

        // 清理HTML实体
        title.replace(QStringLiteral("&amp;"), QStringLiteral("&"))
             .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
             .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
             .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
             .replace(QStringLiteral("&#39;"), QStringLiteral("'"));

        // 生成缩进
        QString indent(level * 2, QLatin1Char(' '));

        tocItems.append(QStringLiteral("%1• %2").arg(indent, title));
    }

    if (tocItems.isEmpty()) return QString();

    QString tocHtml = QStringLiteral(
        "  <nav id='toc' style='background:#f8f9fa;padding:16px;border-radius:8px;margin-bottom:2em;border-left:4px solid #9B59B6;'>\n"
        "    <h3 style='margin:0 0 10px 0;color:#9B59B6;font-size:16px;'>📑 目录</h3>\n"
        "    <ul style='list-style:none;padding-left:0;margin:0;'>\n"
    );

    for (const auto& item : tocItems) {
        tocHtml += QStringLiteral("      <li style='margin:4px 0;color:#555;'>%1</li>\n").arg(item);
    }

    tocHtml += QStringLiteral(
        "    </ul>\n"
        "  </nav>\n"
    );

    return tocHtml;
}
