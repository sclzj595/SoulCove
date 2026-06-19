#ifndef MARKDOWNPARSER_H
#define MARKDOWNPARSER_H

#include "interfaces/markdown/IMarkdownParser.h"
#include <QStringList>

/// @brief 轻量级 Markdown → HTML 解析器 (fallback)
/// 无三方依赖，纯Qt实现，支持基础MD语法
/// 实现 IMarkdownParser 接口，与 maddy 等三方解析器可互换
class MarkdownParser : public IMarkdownParser
{
public:
    QString toHtml(const QString& markdown) override;
    QString name() const override { return QStringLiteral("SimpleMarkdownParser"); }

    /// 静态便捷方法（向后兼容旧代码）
    static QString toHtmlStatic(const QString& markdown);

private:
    static QString parseInline(const QString& text);
    static QString parseCodeBlock(const QString& code, const QString& lang);
    static QString parseTable(const QStringList& lines);
    static QString escapeHtml(const QString& text);
};

#endif // MARKDOWNPARSER_H