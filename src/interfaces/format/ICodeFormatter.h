#ifndef ICODEFORMATTER_H
#define ICODEFORMATTER_H

#include <QString>

/// @brief 代码格式化器抽象接口
/// 上层代码只依赖此接口，不依赖 CodeFormatter 具体实现
/// 设计模式：Strategy（可替换为不同的格式化后端，如 clang-format / prettier / 内置）
class ICodeFormatter
{
public:
    /// 缩进风格
    enum class IndentStyle { Spaces, Tabs };

    virtual ~ICodeFormatter() = default;

    /// 格式化文本内容，返回格式化后的文本
    /// @param code      待格式化的代码文本
    /// @param filePath  文件路径（用于 clang-format --assume-filename）
    /// @return 格式化后的文本
    virtual QString format(const QString& code, const QString& filePath = QString()) = 0;

    /// 检查 clang-format 是否可用
    virtual bool isClangFormatAvailable() const = 0;

    /// 设置缩进风格（用于内置格式化）
    virtual void setIndentStyle(IndentStyle style) = 0;

    /// 设置缩进大小
    virtual void setIndentSize(int size) = 0;
};

#endif // ICODEFORMATTER_H
