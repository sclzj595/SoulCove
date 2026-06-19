#ifndef MADDYPARSER_H
#define MADDYPARSER_H

#include "interfaces/markdown/IMarkdownParser.h"

/// @brief 基于 maddy 三方库的 Markdown → HTML 解析器
/// maddy: C++ header-only, MIT license, GFM 支持
/// https://github.com/progsource/maddy
class MaddyParser : public IMarkdownParser
{
public:
    QString toHtml(const QString& markdown) override;
    QString name() const override { return QStringLiteral("maddy"); }

    /// 根据当前主题动态生成 CSS 样式（用于嵌入 HTML head）
    static QString defaultStyleSheet();
};

#endif // MADDYPARSER_H