#ifndef IMARKDOWNPARSER_H
#define IMARKDOWNPARSER_H

#include <QString>

/// @brief Markdown 解析器抽象接口
/// 所有 MD 解析器（自研/maddy/cmark 等）均实现此接口
/// 工厂模式创建，支持运行时切换实现
class IMarkdownParser
{
public:
    virtual ~IMarkdownParser() = default;

    /// 将 Markdown 文本转换为 HTML
    virtual QString toHtml(const QString& markdown) = 0;

    /// 返回解析器名称（用于调试/日志）
    virtual QString name() const = 0;
};

#endif // IMARKDOWNPARSER_H