#ifndef CODEHIGHLIGHTER_H
#define CODEHIGHLIGHTER_H

#include <QString>
#include <QHash>
#include <QRegularExpression>

/// @brief 代码块语法高亮后处理器
/// 对 HTML 中的 <code class="language-xxx"> 块进行关键词着色
/// 支持 C/C++/Python/JavaScript/Java 等常见语言
class CodeHighlighter
{
public:
    /// 对整个 HTML 字符串中的代码块进行语法高亮
    static QString highlightHtml(const QString& html);

private:
    struct LanguageRules {
        QVector<QPair<QRegularExpression, QString>> patterns; // regex → css-color
    };

    /// 对单个代码块进行高亮
    static QString highlightBlock(const QString& code, const QString& lang);

    /// 获取指定语言的着色规则
    static const LanguageRules& rulesForLanguage(const QString& lang);

    /// 转义 HTML 实体
    static QString escapeHtml(const QString& text);

    static QHash<QString, LanguageRules> s_rules;
    static bool s_initialized;
    static void initRules();
};

#endif // CODEHIGHLIGHTER_H