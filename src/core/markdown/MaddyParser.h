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

    /// 根据当前主题动态生成 CSS 样式（保留以兼容 MdExporter 等外部调用方）
    static QString defaultStyleSheet();

private:
    /// @brief P3-M02 子项5: 在 HTML 中查找 mermaid 代码块并渲染为 SVG
    /// @param html maddy 解析后的 HTML（含 <pre><code class="language-mermaid"> 块）
    /// @return 替换 mermaid 块为 SVG 后的 HTML
    static QString renderMermaidBlocks(const QString& html);
};

#endif // MADDYPARSER_H