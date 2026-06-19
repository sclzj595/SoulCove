#ifndef CODESYNTAXHIGHLIGHTER_H
#define CODESYNTAXHIGHLIGHTER_H

#include "interfaces/ISyntaxHighlighter.h"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QStringList>
#include <QMap>
#include <QHash>
#include <QVariantMap>

/// @brief 语义符号信息（来自 LSP documentSymbol，用于语义高亮）
/// 仅包含高亮所需的最小信息：符号名位置 + LSP SymbolKind
struct SemanticSymbol {
    QString name;           // 符号名称
    int kind = 0;           // LSP SymbolKind (5=Class, 6=Method, 12=Function, 13=Variable, ...)
    int nameLine = 0;       // 符号名所在行（0-based，与 QTextBlock::blockNumber() 一致）
    int nameStartCol = 0;   // 符号名起始列（0-based）
    int nameEndCol = 0;     // 符号名结束列（0-based，不含）
};

/// @brief 代码语法高亮器
/// 继承 QSyntaxHighlighter + ISyntaxHighlighter 接口
/// 支持 Python / C++ / C / JavaScript / JSON / Go / HTML/CSS 等语言
/// 颜色跟随 ThemeManager 主题变化
class CodeSyntaxHighlighter : public QSyntaxHighlighter, public ISyntaxHighlighter
{
    Q_OBJECT

public:
    explicit CodeSyntaxHighlighter(QTextDocument* parent = nullptr);

    /// 根据文件后缀名创建对应的高亮规则
    void setupRules(const QString& fileSuffix) override;

    /// 获取当前支持的语言列表
    QStringList supportedLanguages() const override;

    /// 获取支持的语言列表（静态方法，无需实例化）
    static QStringList getSupportedLanguages();

    /// 主题变更时更新高亮配色并重新高亮
    void updateThemeColors();

    // ========== L12-L14: LSP 语义高亮 ==========
    /// @brief 设置 LSP 语义符号（来自 documentSymbol 响应）
    /// 解析 QVariantMap 列表，提取符号名位置和类型，按行索引后触发重高亮
    /// @param symbols LSP documentSymbol 响应的原始 QVariantMap 列表
    void setSemanticSymbols(const QList<QVariantMap>& symbols);

    /// @brief 清除所有语义符号（文件关闭时调用）
    void clearSemanticSymbols();

    /// @brief 设置外部符号（来自 #include/import 的本地头文件）
    /// 将符号名添加为关键字规则，在源文件中任何位置出现都高亮
    /// @param symbols (符号名, 语义角色) 列表，角色与 colorForRole 一致
    void setExternalSymbols(const QList<QPair<QString, QString>>& symbols);

protected:
    void highlightBlock(const QString& text) override;

private:
    /// 高亮规则结构体
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
        QString formatRole;  // 角色标识，用于主题切换时更新颜色
    };

    /// 初始化各语言规则
    void setupPythonRules();
    void setupCppRules();
    void setupJsonRules();
    void setupYamlRules();
    void setupTomlRules();
    void setupJsRules();
    void setupGoRules();
    void setupHtmlCssRules();
    void setupGenericRules();

    /// 添加关键字规则
    void addKeywordRules(const QStringList& keywords, const QTextCharFormat& format, const QString& role);
    /// 添加单行注释规则
    void addSingleLineCommentRule(const QString& pattern);
    /// 添加字符串规则
    void addStringRules();

    /// 根据角色名获取当前主题对应的颜色
    QColor colorForRole(const QString& role) const;

    /// L12: 解析单个 LSP 符号 QVariantMap → SemanticSymbol（递归处理 children）
    void parseSymbolRecursive(const QVariantMap& sym, QList<SemanticSymbol>& out);

    /// L12: 根据 LSP SymbolKind 选择对应的语义高亮格式
    QTextCharFormat formatForSymbolKind(int kind) const;

    QList<HighlightRule> m_rules;
    QList<HighlightRule> m_externalRules;  // 外部符号规则（来自 #include/import 的本地头文件符号）
    QString m_currentSuffix;  // 当前语言后缀
    bool m_supportsBlockComment = false;  // 是否支持 /* */ 块注释（C/C++/JS/Go/CSS）

    // L12: 语义符号按行索引（行号 → 该行上的符号列表），highlightBlock 快速查找
    QHash<int, QList<SemanticSymbol>> m_symbolsByLine;

    // 高亮格式（颜色跟随主题）
    QTextCharFormat m_keywordFormat;      // 关键字
    QTextCharFormat m_controlFormat;      // 控制流
    QTextCharFormat m_stringFormat;       // 字符串
    QTextCharFormat m_numberFormat;       // 数字
    QTextCharFormat m_commentFormat;      // 注释
    QTextCharFormat m_functionFormat;     // 函数
    QTextCharFormat m_typeFormat;         // 类型
    QTextCharFormat m_preprocessorFormat; // 预处理
    QTextCharFormat m_builtinFormat;      // 内置
    QTextCharFormat m_decoratorFormat;    // 装饰器
    QTextCharFormat m_constantFormat;     // 常量
    QTextCharFormat m_tagFormat;          // HTML标签
    QTextCharFormat m_yamlKeyFormat;      // YAML键名 (M10)
    QTextCharFormat m_tomlKeyFormat;      // TOML键名 (M10)
    QTextCharFormat m_tomlSectionFormat;  // TOML段落头 (M10)

    // L13: 语义高亮格式（来自 LSP documentSymbol，区别于正则关键字高亮）
    QTextCharFormat m_functionDeclFormat;  // 函数声明（Function/Method/Constructor）
    QTextCharFormat m_typeDefFormat;       // 类型定义（Class/Struct/Enum/Interface/TypeParameter）
    QTextCharFormat m_memberVarFormat;     // 成员变量（Field/Property）
    QTextCharFormat m_localVarFormat;      // 局部变量（Variable）
};

#endif // CODESYNTAXHIGHLIGHTER_H
