#include "ui/editor/CompletionPreviewWidget.h"

#include <QTextEdit>
#include <QVBoxLayout>
#include <QTextDocument>
#include <QRegularExpression>

CompletionPreviewWidget::CompletionPreviewWidget(QWidget* parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setFixedWidth(300);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setFrameStyle(QFrame::NoFrame);
    m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    layout->addWidget(m_textEdit);

    applyTheme();
}

void CompletionPreviewWidget::setLspItem(const LspCompletionItem& item)
{
    QString html;

    // 签名（label + detail）
    if (!item.label.isEmpty()) {
        html += QStringLiteral("<b>%1</b>").arg(escapeHtml(item.label));
    }
    if (!item.detail.isEmpty()) {
        html += QStringLiteral("<br><span style='color:#586069;'>%1</span>")
                    .arg(escapeHtml(item.detail));
    }

    // 文档（markdown 渲染）
    if (!item.documentation.isEmpty()) {
        html += QStringLiteral("<hr>");
        html += markdownToHtml(item.documentation);
    }

    // insertText 预览
    if (!item.insertText.isEmpty() && item.insertText != item.label) {
        html += QStringLiteral("<hr><span style='color:#6f42c1;'>%1</span>")
                    .arg(escapeHtml(item.insertText));
    }

    if (html.isEmpty()) {
        m_textEdit->clear();
    } else {
        m_textEdit->setHtml(html);
    }
    adjustHeightToFit();
}

void CompletionPreviewWidget::setLocalWord(const QString& word)
{
    if (word.isEmpty()) {
        m_textEdit->clear();
    } else {
        m_textEdit->setHtml(
            QStringLiteral("<b>%1</b><br><span style='color:#586069;'>%2</span>")
                .arg(escapeHtml(word), tr("本地词典项")));
    }
    adjustHeightToFit();
}

void CompletionPreviewWidget::setSnippet(const QString& label, const QString& body)
{
    QString html;
    if (!label.isEmpty()) {
        html += QStringLiteral("<b>%1</b>").arg(escapeHtml(label));
    }
    if (!body.isEmpty()) {
        html += QStringLiteral("<hr><pre>%1</pre>")
                    .arg(highlightSnippetPlaceholders(body));
    }

    if (html.isEmpty()) {
        m_textEdit->clear();
    } else {
        m_textEdit->setHtml(html);
    }
    adjustHeightToFit();
}

void CompletionPreviewWidget::clearContent()
{
    m_textEdit->clear();
    adjustHeightToFit();
}

void CompletionPreviewWidget::applyTheme()
{
    // 浅色主题样式（与补全弹窗协调）
    setStyleSheet(QStringLiteral(
        "CompletionPreviewWidget {"
        "  background: #ffffff;"
        "  border: 1px solid #d0d7de;"
        "  border-radius: 4px;"
        "}"
        "QTextEdit {"
        "  background: transparent;"
        "  color: #1f2328;"
        "  padding: 6px;"
        "  font-size: 12px;"
        "}"
        "QScrollBar:vertical { width: 6px; }"
    ));
}

void CompletionPreviewWidget::adjustHeightToFit()
{
    if (!m_textEdit) return;
    // 等待文档布局完成后再获取高度
    m_textEdit->document()->setTextWidth(m_textEdit->viewport()->width());
    int docHeight = static_cast<int>(m_textEdit->document()->size().height());
    // 边距 + 边框
    int height = qBound(0, docHeight + 12, 200);
    setFixedHeight(height);
}

QString CompletionPreviewWidget::escapeHtml(const QString& text) const
{
    QString escaped = text;
    escaped.replace('&', QStringLiteral("&amp;"))
           .replace('<', QStringLiteral("&lt;"))
           .replace('>', QStringLiteral("&gt;"))
           .replace('"', QStringLiteral("&quot;"))
           .replace('\'', QStringLiteral("&#39;"));
    return escaped;
}

QString CompletionPreviewWidget::markdownToHtml(const QString& markdown) const
{
    if (markdown.isEmpty()) return QString();

    QString text = escapeHtml(markdown);

    // 代码块 ``` ... ```
    static const QRegularExpression codeBlockRe(
        QStringLiteral("```[\\s\\S]*?```"));
    // 先按代码块分割处理
    QStringList parts;
    int lastEnd = 0;
    auto it = codeBlockRe.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        if (match.capturedStart() > lastEnd) {
            parts << text.mid(lastEnd, match.capturedStart() - lastEnd);
        }
        QString code = match.captured();
        // 去掉 ```
        code = code.mid(3, code.length() - 6);
        // 去掉语言标识行（如 ```cpp）
        int nl = code.indexOf('\n');
        if (nl >= 0 && !code.left(nl).trimmed().isEmpty()) {
            code = code.mid(nl + 1);
        }
        parts << QStringLiteral("<pre>%1</pre>").arg(code.trimmed());
        lastEnd = match.capturedEnd();
    }
    if (lastEnd < text.length()) {
        parts << text.mid(lastEnd);
    }

    // 对非代码块部分应用行内格式
    for (int i = 0; i < parts.size(); ++i) {
        if (parts[i].startsWith(QStringLiteral("<pre>"))) continue;

        QString& s = parts[i];
        // 行内代码 `code`
        static const QRegularExpression inlineCodeRe(
            QStringLiteral("`([^`]+)`"));
        s.replace(inlineCodeRe, QStringLiteral("<code>\\1</code>"));

        // 标题 # / ## / ###
        s.replace(QRegularExpression(QStringLiteral("^###\\s+(.+)$"), QRegularExpression::MultilineOption),
                  QStringLiteral("<b>\\1</b>"));
        s.replace(QRegularExpression(QStringLiteral("^##\\s+(.+)$"), QRegularExpression::MultilineOption),
                  QStringLiteral("<b>\\1</b>"));
        s.replace(QRegularExpression(QStringLiteral("^#\\s+(.+)$"), QRegularExpression::MultilineOption),
                  QStringLiteral("<b>\\1</b>"));

        // 段落：空行 → <br><br>
        s.replace(QRegularExpression(QStringLiteral("\n\n+")),
                  QStringLiteral("<br><br>"));
        s.replace('\n', QStringLiteral("<br>"));
    }

    return parts.join(QString());
}

QString CompletionPreviewWidget::highlightSnippetPlaceholders(const QString& body) const
{
    QString text = escapeHtml(body);
    // ${n:default} → 高亮
    static const QRegularExpression re1(
        QStringLiteral("\\$\\{(\\d+):([^}]*)\\}"));
    text.replace(re1,
        QStringLiteral("<span style='color:#0969da;background:#ddf4ff;'>"
                       "${\\1:\\2}</span>"));
    // $n → 高亮
    static const QRegularExpression re2(QStringLiteral("\\$(\\d+)"));
    text.replace(re2,
        QStringLiteral("<span style='color:#0969da;background:#ddf4ff;'>"
                       "$\\1</span>"));
    return text;
}
