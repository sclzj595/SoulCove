#ifndef ISYNTAXHIGHLIGHTER_H
#define ISYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QString>

/// @brief 语法高亮器抽象接口
/// 定义代码语法高亮的核心能力
/// 上层代码只依赖此接口，不依赖具体实现
class ISyntaxHighlighter
{
public:
    virtual ~ISyntaxHighlighter() = default;

    /// 根据文件后缀名创建对应的高亮规则
    virtual void setupRules(const QString& fileSuffix) = 0;

    /// 获取当前支持的语言列表
    virtual QStringList supportedLanguages() const = 0;
};

#endif // ISYNTAXHIGHLIGHTER_H
