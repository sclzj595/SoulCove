#include "core/editor/CodeSyntaxHighlighter.h"
#include "core/config/ThemeManager.h"

CodeSyntaxHighlighter::CodeSyntaxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    // 初始化格式（颜色由 updateThemeColors 设置）
    m_keywordFormat.setFontWeight(QFont::Bold);
    m_controlFormat.setFontWeight(QFont::Bold);
    m_commentFormat.setFontItalic(true);

    // M10: 初始化新增格式
    m_yamlKeyFormat.setFontWeight(QFont::Bold);
    m_tomlKeyFormat.setFontWeight(QFont::Bold);
    m_tomlSectionFormat.setFontWeight(QFont::Bold);

    // L13: 语义高亮格式 — 函数声明用斜体区分正则匹配的函数调用
    m_functionDeclFormat.setFontItalic(true);

    updateThemeColors();
    setupGenericRules();
}

QColor CodeSyntaxHighlighter::colorForRole(const QString& role) const
{
    const auto& palette = ThemeManager::instance().currentPalette();

    // 亮/暗主题判定 (与 ThemeManager/MaddyParser/CommandPalette 一致: bgEditor.lightness() > 128)
    // 覆盖 light / pink 及任何亮色主题，避免 pink 误用暗色配色导致浅色背景上看不清
    bool isLight = palette.bgEditor.lightness() > 128;

    if (isLight) {
        // GitHub Light 配色 — 清晰、现代、高对比度，浅色/白粉背景下完全可读
        if (role == QStringLiteral("keyword"))     return QColor("#cf222e"); // 红
        if (role == QStringLiteral("control"))     return QColor("#cf222e"); // 红(控制流)
        if (role == QStringLiteral("string"))      return QColor("#0a3069"); // 深蓝
        if (role == QStringLiteral("number"))      return QColor("#0550ae"); // 蓝
        if (role == QStringLiteral("comment"))     return QColor("#6e7781"); // 灰(柔和)
        if (role == QStringLiteral("function"))    return QColor("#8250df"); // 紫
        if (role == QStringLiteral("type"))        return QColor("#953800"); // 橙
        if (role == QStringLiteral("preprocessor"))return QColor("#cf222e"); // 红
        if (role == QStringLiteral("builtin"))     return QColor("#6639ba"); // 紫
        if (role == QStringLiteral("decorator"))   return QColor("#6639ba"); // 紫
        if (role == QStringLiteral("constant"))    return QColor("#0550ae"); // 蓝(替代刺眼浅蓝#4FC1FF)
        if (role == QStringLiteral("tag"))         return QColor("#116329"); // 绿
        if (role == QStringLiteral("yamlKey"))     return QColor("#0550ae"); // 蓝
        if (role == QStringLiteral("tomlKey"))     return QColor("#0550ae"); // 蓝
        if (role == QStringLiteral("tomlSection")) return QColor("#cf222e"); // 红
        // L13: 语义高亮配色（GitHub Light 风格，与正则高亮区分）
        if (role == QStringLiteral("funcDecl"))    return QColor("#8250df"); // 紫（函数声明，斜体）
        if (role == QStringLiteral("typeDef"))     return QColor("#953800"); // 橙（类型定义）
        if (role == QStringLiteral("memberVar"))   return QColor("#0550ae"); // 蓝（成员变量）
        if (role == QStringLiteral("localVar"))    return QColor("#24292f"); // 深灰（局部变量，低调）
    }

    // 暗色主题 - VSCode Dark+ 配色
    if (role == QStringLiteral("keyword"))    return QColor("#569CD6");
    if (role == QStringLiteral("control"))    return QColor("#C586C0");
    if (role == QStringLiteral("string"))     return QColor("#CE9178");
    if (role == QStringLiteral("number"))     return QColor("#B5CEA8");
    if (role == QStringLiteral("comment"))    return QColor("#6A9955");
    if (role == QStringLiteral("function"))   return QColor("#DCDCAA");
    if (role == QStringLiteral("type"))       return QColor("#4EC9B0");
    if (role == QStringLiteral("preprocessor"))return QColor("#C586C0");
    if (role == QStringLiteral("builtin"))    return QColor("#DCDCAA");
    if (role == QStringLiteral("decorator"))  return QColor("#DCDCAA");
    if (role == QStringLiteral("constant"))   return QColor("#4FC1FF");
    if (role == QStringLiteral("tag"))        return QColor("#569CD6");
    if (role == QStringLiteral("yamlKey"))    return QColor("#4EC9B0");     // M10: YAML键名（青色）
    if (role == QStringLiteral("tomlKey"))    return QColor("#9CDCFE");     // M10: TOML键名
    if (role == QStringLiteral("tomlSection"))return QColor("#C586C0");    // M10: TOML段落头
    // L13: 语义高亮配色（VSCode Dark+ 风格）
    if (role == QStringLiteral("funcDecl"))   return QColor("#DCDCAA"); // 黄（函数声明，斜体）
    if (role == QStringLiteral("typeDef"))    return QColor("#4EC9B0"); // 青（类型定义）
    if (role == QStringLiteral("memberVar"))  return QColor("#9CDCFE"); // 浅蓝（成员变量）
    if (role == QStringLiteral("localVar"))   return QColor("#9CDCFE"); // 浅蓝（局部变量）

    return palette.fgPrimary;
}

void CodeSyntaxHighlighter::updateThemeColors()
{
    m_keywordFormat.setForeground(colorForRole(QStringLiteral("keyword")));
    m_controlFormat.setForeground(colorForRole(QStringLiteral("control")));
    m_stringFormat.setForeground(colorForRole(QStringLiteral("string")));
    m_numberFormat.setForeground(colorForRole(QStringLiteral("number")));
    m_commentFormat.setForeground(colorForRole(QStringLiteral("comment")));
    m_functionFormat.setForeground(colorForRole(QStringLiteral("function")));
    m_typeFormat.setForeground(colorForRole(QStringLiteral("type")));
    m_preprocessorFormat.setForeground(colorForRole(QStringLiteral("preprocessor")));
    m_builtinFormat.setForeground(colorForRole(QStringLiteral("builtin")));
    m_decoratorFormat.setForeground(colorForRole(QStringLiteral("decorator")));
    m_constantFormat.setForeground(colorForRole(QStringLiteral("constant")));
    m_tagFormat.setForeground(colorForRole(QStringLiteral("tag")));
    // M10: 初始化新增格式颜色
    m_yamlKeyFormat.setForeground(colorForRole(QStringLiteral("yamlKey")));
    m_tomlKeyFormat.setForeground(colorForRole(QStringLiteral("tomlKey")));
    m_tomlSectionFormat.setForeground(colorForRole(QStringLiteral("tomlSection")));

    // L13: 语义高亮格式颜色
    m_functionDeclFormat.setForeground(colorForRole(QStringLiteral("funcDecl")));
    m_typeDefFormat.setForeground(colorForRole(QStringLiteral("typeDef")));
    m_memberVarFormat.setForeground(colorForRole(QStringLiteral("memberVar")));
    m_localVarFormat.setForeground(colorForRole(QStringLiteral("localVar")));
    // 保留斜体属性
    m_functionDeclFormat.setFontItalic(true);

    // 更新所有规则的颜色
    for (auto& rule : m_rules) {
        if (!rule.formatRole.isEmpty()) {
            rule.format.setForeground(colorForRole(rule.formatRole));
            // 保留原有格式属性（粗体/斜体）
            if (rule.formatRole == QStringLiteral("keyword") || rule.formatRole == QStringLiteral("control")) {
                rule.format.setFontWeight(QFont::Bold);
            }
            if (rule.formatRole == QStringLiteral("comment")) {
                rule.format.setFontItalic(true);
            }
        }
    }

    // 同步更新外部符号规则颜色（主题切换时跟随）
    for (auto& rule : m_externalRules) {
        if (!rule.formatRole.isEmpty()) {
            rule.format.setForeground(colorForRole(rule.formatRole));
            if (rule.formatRole == QStringLiteral("funcDecl")) {
                rule.format.setFontItalic(true);
            }
        }
    }

    rehighlight();
}

QStringList CodeSyntaxHighlighter::supportedLanguages() const
{
    return getSupportedLanguages();
}

QStringList CodeSyntaxHighlighter::getSupportedLanguages()
{
    return {QStringLiteral("py"), QStringLiteral("cpp"), QStringLiteral("c"),
            QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("js"),
            QStringLiteral("json"), QStringLiteral("ts"), QStringLiteral("go"),
            QStringLiteral("html"), QStringLiteral("css"), QStringLiteral("qss"),
            QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml")};
}

void CodeSyntaxHighlighter::setupRules(const QString& fileSuffix)
{
    m_rules.clear();
    m_externalRules.clear();  // 切换语言/文件时清空外部符号（由 Widget 重新扫描填充）
    m_currentSuffix = fileSuffix.toLower();
    m_supportsBlockComment = false;  // 重置，由各语言 setup 方法按需开启
    setupGenericRules();

    if (m_currentSuffix == QStringLiteral("py"))          setupPythonRules();
    else if (m_currentSuffix == QStringLiteral("cpp") ||
             m_currentSuffix == QStringLiteral("c") ||
             m_currentSuffix == QStringLiteral("h") ||
             m_currentSuffix == QStringLiteral("hpp") ||
             m_currentSuffix == QStringLiteral("cc") ||
             m_currentSuffix == QStringLiteral("cxx"))    setupCppRules();
    else if (m_currentSuffix == QStringLiteral("js") ||
             m_currentSuffix == QStringLiteral("ts"))     setupJsRules();
    else if (m_currentSuffix == QStringLiteral("json"))   setupJsonRules();
    else if (m_currentSuffix == QStringLiteral("yaml") ||
             m_currentSuffix == QStringLiteral("yml"))     setupYamlRules();      // M10
    else if (m_currentSuffix == QStringLiteral("toml"))   setupTomlRules();      // M10
    else if (m_currentSuffix == QStringLiteral("go"))     setupGoRules();
    else if (m_currentSuffix == QStringLiteral("html") ||
             m_currentSuffix == QStringLiteral("css") ||
             m_currentSuffix == QStringLiteral("qss"))    setupHtmlCssRules();

    rehighlight();
}

void CodeSyntaxHighlighter::highlightBlock(const QString& text)
{
    // 1. 应用单行规则（关键字/字符串/数字/单行注释等）
    for (const auto& rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // 1.5 应用外部符号规则（来自 #include/import 的本地头文件符号）
    //     在普通规则之后、块注释之前应用，可覆盖普通关键字配色
    //     例：源文件中引用的自定义类名 ILineNumber 会被高亮为类型色
    for (const auto& rule : m_externalRules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // 2. 处理多行块注释 /* ... */（含 Doxygen 风格 /*** */ 等）
    //    使用 block state 跨行跟踪：0=正常，1=块注释中
    //    setFormat 会覆盖步骤1的格式，确保注释内容统一为注释色
    if (m_supportsBlockComment) {
        int startIndex = 0;
        bool continuingComment = (previousBlockState() == 1);  // 是否从上一行延续块注释

        if (!continuingComment) {
            // 上一行不在块注释中，从行首查找 /*
            startIndex = text.indexOf(QStringLiteral("/*"));
        }
        // 延续块注释时 startIndex = 0，整行都可能是注释内容

        while (startIndex >= 0) {
            // 查找 */ 的结束位置：
            // - 延续块注释时从 startIndex（即 0）开始查找，避免跳过行首的 */
            // - 新块注释时从 /* 之后（startIndex + 2）开始查找
            int searchFrom = continuingComment ? startIndex : startIndex + 2;
            int endIndex = text.indexOf(QStringLiteral("*/"), searchFrom);
            continuingComment = false;  // 第一次查找后不再是延续状态

            int commentLength;
            if (endIndex >= 0) {
                // 当行找到 */，块注释结束
                commentLength = endIndex - startIndex + 2;
                setCurrentBlockState(0);
            } else {
                // 当行未找到 */，剩余部分均为注释，延续到下一行
                commentLength = text.length() - startIndex;
                setCurrentBlockState(1);
            }
            setFormat(startIndex, commentLength, m_commentFormat);
            // 继续查找本行后续的 /*（处理一行多个块注释的情况）
            startIndex = text.indexOf(QStringLiteral("/*"), startIndex + commentLength);
        }
    }

    // 3. L12: 应用 LSP 语义高亮（覆盖正则规则，提供基于符号类型的精确高亮）
    //    按当前行号查找语义符号，对符号名范围应用对应格式
    //    语义高亮优先级最高：函数声明 > 类型定义 > 成员变量 > 局部变量
    int blockNum = currentBlock().blockNumber();
    auto it = m_symbolsByLine.constFind(blockNum);
    if (it != m_symbolsByLine.constEnd()) {
        for (const SemanticSymbol& sym : it.value()) {
            // 列范围安全检查（防止 LSP 返回过期的位置）
            int start = sym.nameStartCol;
            int length = sym.nameEndCol - sym.nameStartCol;
            if (start < 0 || length <= 0 || start + length > text.length()) continue;
            setFormat(start, length, formatForSymbolKind(sym.kind));
        }
    }
}

void CodeSyntaxHighlighter::addKeywordRules(const QStringList& keywords, const QTextCharFormat& format, const QString& role)
{
    for (const QString& kw : keywords) {
        HighlightRule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(kw));
        rule.format = format;
        rule.formatRole = role;
        m_rules.append(rule);
    }
}

void CodeSyntaxHighlighter::addSingleLineCommentRule(const QString& pattern)
{
    HighlightRule rule;
    rule.pattern = QRegularExpression(pattern);
    rule.format = m_commentFormat;
    rule.formatRole = QStringLiteral("comment");
    m_rules.append(rule);
}

void CodeSyntaxHighlighter::addStringRules()
{
    // 双引号字符串
    HighlightRule doubleQuoteRule;
    doubleQuoteRule.pattern = QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\""));
    doubleQuoteRule.format = m_stringFormat;
    doubleQuoteRule.formatRole = QStringLiteral("string");
    m_rules.append(doubleQuoteRule);

    // 单引号字符串
    HighlightRule singleQuoteRule;
    singleQuoteRule.pattern = QRegularExpression(QStringLiteral("'(?:[^'\\\\]|\\\\.)*'"));
    singleQuoteRule.format = m_stringFormat;
    singleQuoteRule.formatRole = QStringLiteral("string");
    m_rules.append(singleQuoteRule);
}

void CodeSyntaxHighlighter::setupGenericRules()
{
    // 数字
    HighlightRule numberRule;
    numberRule.pattern = QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"));
    numberRule.format = m_numberFormat;
    numberRule.formatRole = QStringLiteral("number");
    m_rules.append(numberRule);

    addStringRules();
}

void CodeSyntaxHighlighter::setupPythonRules()
{
    // Python 控制流关键字
    addKeywordRules({
        QStringLiteral("if"), QStringLiteral("elif"), QStringLiteral("else"),
        QStringLiteral("for"), QStringLiteral("while"), QStringLiteral("break"),
        QStringLiteral("continue"), QStringLiteral("return"), QStringLiteral("yield"),
        QStringLiteral("try"), QStringLiteral("except"), QStringLiteral("finally"),
        QStringLiteral("raise"), QStringLiteral("with"), QStringLiteral("as"),
        QStringLiteral("import"), QStringLiteral("from"), QStringLiteral("pass"),
        QStringLiteral("assert"), QStringLiteral("del"), QStringLiteral("global"),
        QStringLiteral("nonlocal"), QStringLiteral("lambda")
    }, m_controlFormat, QStringLiteral("control"));

    // Python 语言关键字
    addKeywordRules({
        QStringLiteral("def"), QStringLiteral("class"), QStringLiteral("and"),
        QStringLiteral("or"), QStringLiteral("not"), QStringLiteral("in"),
        QStringLiteral("is"), QStringLiteral("True"), QStringLiteral("False"),
        QStringLiteral("None")
    }, m_keywordFormat, QStringLiteral("keyword"));

    // Python 内置函数
    addKeywordRules({
        QStringLiteral("print"), QStringLiteral("len"), QStringLiteral("range"),
        QStringLiteral("input"), QStringLiteral("int"), QStringLiteral("str"),
        QStringLiteral("float"), QStringLiteral("list"), QStringLiteral("dict"),
        QStringLiteral("set"), QStringLiteral("tuple"), QStringLiteral("type"),
        QStringLiteral("isinstance"), QStringLiteral("enumerate"), QStringLiteral("zip"),
        QStringLiteral("map"), QStringLiteral("filter"), QStringLiteral("sorted"),
        QStringLiteral("open"), QStringLiteral("super"), QStringLiteral("property"),
        QStringLiteral("staticmethod"), QStringLiteral("classmethod"), QStringLiteral("abs"),
        QStringLiteral("max"), QStringLiteral("min"), QStringLiteral("sum"),
        QStringLiteral("any"), QStringLiteral("all"), QStringLiteral("hasattr"),
        QStringLiteral("getattr"), QStringLiteral("setattr"), QStringLiteral("repr")
    }, m_builtinFormat, QStringLiteral("builtin"));

    // Python 装饰器
    HighlightRule decoratorRule;
    decoratorRule.pattern = QRegularExpression(QStringLiteral("@\\w+"));
    decoratorRule.format = m_decoratorFormat;
    decoratorRule.formatRole = QStringLiteral("decorator");
    m_rules.append(decoratorRule);

    // Python 单行注释
    addSingleLineCommentRule(QStringLiteral("#[^\n]*"));

    // Python 多行字符串/文档字符串
    HighlightRule tripleDoubleRule;
    tripleDoubleRule.pattern = QRegularExpression(QStringLiteral("\"\"\"(?:[^\"\\\\]|\\\\.)*\"\"\""));
    tripleDoubleRule.format = m_stringFormat;
    tripleDoubleRule.formatRole = QStringLiteral("string");
    m_rules.append(tripleDoubleRule);

    HighlightRule tripleSingleRule;
    tripleSingleRule.pattern = QRegularExpression(QStringLiteral("'''(?:[^'\\\\]|\\\\.)*'''"));
    tripleSingleRule.format = m_stringFormat;
    tripleSingleRule.formatRole = QStringLiteral("string");
    m_rules.append(tripleSingleRule);

    // self
    addKeywordRules({QStringLiteral("self")}, m_constantFormat, QStringLiteral("constant"));
}

void CodeSyntaxHighlighter::setupCppRules()
{
    m_supportsBlockComment = true;  // C/C++ 支持 /* */ 块注释
    // C++ 控制流关键字
    addKeywordRules({
        QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("break"),
        QStringLiteral("continue"), QStringLiteral("return"), QStringLiteral("switch"),
        QStringLiteral("case"), QStringLiteral("default"), QStringLiteral("goto"),
        QStringLiteral("try"), QStringLiteral("catch"), QStringLiteral("throw"),
        QStringLiteral("noexcept"), QStringLiteral("co_await"), QStringLiteral("co_yield"),
        QStringLiteral("co_return")
    }, m_controlFormat, QStringLiteral("control"));

    // C++ 类型/声明关键字
    addKeywordRules({
        QStringLiteral("void"), QStringLiteral("int"), QStringLiteral("char"),
        QStringLiteral("float"), QStringLiteral("double"), QStringLiteral("bool"),
        QStringLiteral("long"), QStringLiteral("short"), QStringLiteral("unsigned"),
        QStringLiteral("signed"), QStringLiteral("auto"), QStringLiteral("const"),
        QStringLiteral("static"), QStringLiteral("extern"), QStringLiteral("inline"),
        QStringLiteral("virtual"), QStringLiteral("explicit"), QStringLiteral("mutable"),
        QStringLiteral("constexpr"), QStringLiteral("decltype"), QStringLiteral("register"),
        QStringLiteral("volatile"), QStringLiteral("thread_local"), QStringLiteral("consteval"),
        QStringLiteral("constinit")
    }, m_keywordFormat, QStringLiteral("keyword"));

    // C++ 复合类型关键字
    addKeywordRules({
        QStringLiteral("class"), QStringLiteral("struct"), QStringLiteral("enum"),
        QStringLiteral("union"), QStringLiteral("namespace"), QStringLiteral("using"),
        QStringLiteral("template"), QStringLiteral("typename"), QStringLiteral("public"),
        QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("friend"),
        QStringLiteral("operator"), QStringLiteral("typedef"), QStringLiteral("concept"),
        QStringLiteral("requires"), QStringLiteral("static_assert")
    }, m_typeFormat, QStringLiteral("type"));

    // C++ 布尔/空值
    addKeywordRules({
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("nullptr"),
        QStringLiteral("NULL"), QStringLiteral("this"), QStringLiteral("override"),
        QStringLiteral("final"), QStringLiteral("delete"), QStringLiteral("new"),
        QStringLiteral("sizeof"), QStringLiteral("alignof"), QStringLiteral("typeid")
    }, m_constantFormat, QStringLiteral("constant"));

    // C++ 预处理指令（不含 include，include 由专门的规则用字符串颜色高亮）
    HighlightRule preprocessorRule;
    preprocessorRule.pattern = QRegularExpression(QStringLiteral("#\\s*(?:define|ifdef|ifndef|if|else|elif|endif|pragma|undef|error|warning|line)\\b[^\n]*"));
    preprocessorRule.format = m_preprocessorFormat;
    preprocessorRule.formatRole = QStringLiteral("preprocessor");
    m_rules.append(preprocessorRule);

    // C++ 单行注释
    addSingleLineCommentRule(QStringLiteral("//[^\n]*"));

    // C++ 包含头文件 <xxx>（简单匹配，保留兼容）
    HighlightRule includeRule;
    includeRule.pattern = QRegularExpression(QStringLiteral("<[\\w/.]+>"));
    includeRule.format = m_stringFormat;
    includeRule.formatRole = QStringLiteral("string");
    m_rules.append(includeRule);

    // ===== [VSCode风格增强] 新增规则 =====

    // 函数名高亮：匹配 identifier 后紧跟 (
    HighlightRule funcRule;
    funcRule.pattern = QRegularExpression(QStringLiteral("\\b[a-zA-Z_]\\w*\\s*(?=\\()"));
    funcRule.format = m_functionFormat;
    funcRule.formatRole = QStringLiteral("function");
    m_rules.append(funcRule);

    // C++ 标准库常用类型（VSCode风格）
    addKeywordRules({
        QStringLiteral("string"), QStringLiteral("vector"), QStringLiteral("map"),
        QStringLiteral("unordered_map"), QStringLiteral("set"), QStringLiteral("unordered_set"),
        QStringLiteral("array"), QStringLiteral("queue"), QStringLiteral("stack"),
        QStringLiteral("pair"), QStringLiteral("tuple"), QStringLiteral("optional"),
        QStringLiteral("variant"), QStringLiteral("any"), QStringLiteral("shared_ptr"),
        QStringLiteral("unique_ptr"), QStringLiteral("weak_ptr"), QStringLiteral("function"),
        QStringLiteral("istream"), QStringLiteral("ostream"), QStringLiteral("iostream"),
        QStringLiteral("fstream"), QStringLiteral("stringstream"), QStringLiteral("thread"),
        QStringLiteral("mutex"), QStringLiteral("atomic"), QStringLiteral("future"),
        QStringLiteral("promise"), QStringLiteral("condition_variable"),
        QStringLiteral("size_t"), QStringLiteral("uint32_t"), QStringLiteral("int32_t"),
        QStringLiteral("int64_t"), QStringLiteral("uint64_t"), QStringLiteral("wchar_t"),
        QStringLiteral("exception"), QStringLiteral("runtime_error"), QStringLiteral("logic_error")
    }, m_typeFormat, QStringLiteral("type"));

    // 增强 #include 头文件路径支持：<path/to/header.h> 和 "path/to/header.h"
    HighlightRule includePathRule;
    includePathRule.pattern = QRegularExpression(
        QStringLiteral("#\\s*include\\s+(?:<[^>]+>|\"[^\"]+\")"));
    includePathRule.format = m_stringFormat;
    includePathRule.formatRole = QStringLiteral("string");
    m_rules.append(includePathRule);

    // Raw 字符串字面量 R"delim(...)delim"
    // 注意：使用 delim 定界符避免与内部 R" 冲突
    HighlightRule rawStringRule;
    rawStringRule.pattern = QRegularExpression(
        QStringLiteral(R"delim(R"([^(\s]*)\((?:(?!\)\1").)*\)\1")delim"));
    rawStringRule.format = m_stringFormat;
    rawStringRule.formatRole = QStringLiteral("string");
    m_rules.append(rawStringRule);

    // C++11/14/17 属性 [[nodiscard]] 等
    HighlightRule attrRule;
    attrRule.pattern = QRegularExpression(QStringLiteral("\\[\\[[\\w,:\\s]+\\]\\]"));
    attrRule.format = m_preprocessorFormat;
    attrRule.formatRole = QStringLiteral("preprocessor");
    m_rules.append(attrRule);

    // 静态断言 / 类型 traits 常用关键字
    addKeywordRules({
        QStringLiteral("static_assert"), QStringLiteral("typeof"), QStringLiteral("alignas"),
        QStringLiteral("alignof"), QStringLiteral("typeid"), QStringLiteral("sizeof..."),
        QStringLiteral("decltype"), QStringLiteral("nullptr")
    }, m_constantFormat, QStringLiteral("constant"));
}

void CodeSyntaxHighlighter::setupJsRules()
{
    m_supportsBlockComment = true;  // JS/TS 支持 /* */ 块注释
    // JS 控制流
    addKeywordRules({
        QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("break"),
        QStringLiteral("continue"), QStringLiteral("return"), QStringLiteral("switch"),
        QStringLiteral("case"), QStringLiteral("default"), QStringLiteral("throw"),
        QStringLiteral("try"), QStringLiteral("catch"), QStringLiteral("finally"),
        QStringLiteral("yield"), QStringLiteral("await"), QStringLiteral("of"),
        QStringLiteral("in"), QStringLiteral("instanceof"), QStringLiteral("typeof"),
        QStringLiteral("delete"), QStringLiteral("void"), QStringLiteral("new")
    }, m_controlFormat, QStringLiteral("control"));

    // JS 声明关键字
    addKeywordRules({
        QStringLiteral("var"), QStringLiteral("let"), QStringLiteral("const"),
        QStringLiteral("function"), QStringLiteral("class"), QStringLiteral("extends"),
        QStringLiteral("import"), QStringLiteral("export"), QStringLiteral("from"),
        QStringLiteral("as"), QStringLiteral("default"), QStringLiteral("async"),
        QStringLiteral("static"), QStringLiteral("get"), QStringLiteral("set"),
        QStringLiteral("super"), QStringLiteral("constructor")
    }, m_keywordFormat, QStringLiteral("keyword"));

    // JS 值
    addKeywordRules({
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null"),
        QStringLiteral("undefined"), QStringLiteral("this"), QStringLiteral("NaN"),
        QStringLiteral("Infinity")
    }, m_constantFormat, QStringLiteral("constant"));

    // JS 内置
    addKeywordRules({
        QStringLiteral("console"), QStringLiteral("document"), QStringLiteral("window"),
        QStringLiteral("Array"), QStringLiteral("Object"), QStringLiteral("String"),
        QStringLiteral("Number"), QStringLiteral("Boolean"), QStringLiteral("Promise"),
        QStringLiteral("Map"), QStringLiteral("Set"), QStringLiteral("JSON"),
        QStringLiteral("Math"), QStringLiteral("Date"), QStringLiteral("Error"),
        QStringLiteral("require"), QStringLiteral("module")
    }, m_builtinFormat, QStringLiteral("builtin"));

    // JS 注释
    addSingleLineCommentRule(QStringLiteral("//[^\n]*"));

    // 模板字符串
    HighlightRule templateRule;
    templateRule.pattern = QRegularExpression(QStringLiteral("`(?:[^`\\\\]|\\\\.)*`"));
    templateRule.format = m_stringFormat;
    templateRule.formatRole = QStringLiteral("string");
    m_rules.append(templateRule);
}

void CodeSyntaxHighlighter::setupJsonRules()
{
    // JSON 键名（绿色/类型色）
    HighlightRule keyRule;
    keyRule.pattern = QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"\\s*:"));
    keyRule.format = m_typeFormat;
    keyRule.formatRole = QStringLiteral("type");
    m_rules.append(keyRule);

    // JSON 字符串值（橙色）
    HighlightRule strValueRule;
    strValueRule.pattern = QRegularExpression(QStringLiteral(":\\s*\"(?:[^\"\\\\]|\\\\.)*\""));
    strValueRule.format = m_stringFormat;
    strValueRule.formatRole = QStringLiteral("string");
    m_rules.append(strValueRule);

    // JSON 数字值（蓝色）
    HighlightRule numValueRule;
    numValueRule.pattern = QRegularExpression(QStringLiteral(":\\s*-?\\d+(?:\\.\\d+)?([eE][+-]?\\d+)?"));
    numValueRule.format = m_numberFormat;
    numValueRule.formatRole = QStringLiteral("number");
    m_rules.append(numValueRule);

    // JSON 布尔/null（紫色/常量色）
    addKeywordRules({
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null")
    }, m_constantFormat, QStringLiteral("constant"));

    // JSON 括号标记
    HighlightRule braceRule;
    braceRule.pattern = QRegularExpression(QStringLiteral("[\\[\\]\\{\\}]"));
    braceRule.format = m_keywordFormat;
    braceRule.formatRole = QStringLiteral("keyword");
    m_rules.append(braceRule);
}

// ========== M10: YAML 语法高亮规则 ==========

void CodeSyntaxHighlighter::setupYamlRules()
{
    // YAML 注释 (# 开头, 灰色)
    addSingleLineCommentRule(QStringLiteral("#[^\n]*"));

    // YAML 键名（青色，冒号结尾）
    HighlightRule yamlKeyRule;
    yamlKeyRule.pattern = QRegularExpression(QStringLiteral("^\\s*[a-zA-Z_][a-zA-Z0-9_-]*\\s*:"));
    yamlKeyRule.format = m_yamlKeyFormat;
    yamlKeyRule.formatRole = QStringLiteral("yamlKey");
    m_rules.append(yamlKeyRule);

    // YAML 锚点 (&) 和别名 (*)
    HighlightRule anchorRule;
    anchorRule.pattern = QRegularExpression(QStringLiteral("&[a-zA-Z_][a-zA-Z0-9_-]*|\\*[a-zA-Z_][a-zA-Z0-9_-]*"));
    anchorRule.format = m_typeFormat;
    anchorRule.formatRole = QStringLiteral("type");
    m_rules.append(anchorRule);

    // YAML 字符串值（双引号/单引号包裹）
    HighlightRule yamlDQString;
    yamlDQString.pattern = QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\""));
    yamlDQString.format = m_stringFormat;
    yamlDQString.formatRole = QStringLiteral("string");
    m_rules.append(yamlDQString);

    HighlightRule yamlSQString;
    yamlSQString.pattern = QRegularExpression(QStringLiteral("'[^']*'"));
    yamlSQString.format = m_stringFormat;
    yamlSQString.formatRole = QStringLiteral("string");
    m_rules.append(yamlSQString);

    // YAML 布尔值
    addKeywordRules({
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("yes"),
        QStringLiteral("no"), QStringLiteral("on"), QStringLiteral("off")
    }, m_constantFormat, QStringLiteral("constant"));

    // YAML 数字
    HighlightRule yamlNumRule;
    yamlNumRule.pattern = QRegularExpression(QStringLiteral("\\b-?\\d+(?:\\.\\d+)?([eE][+-]?\\d+)?\\b"));
    yamlNumRule.format = m_numberFormat;
    yamlNumRule.formatRole = QStringLiteral("number");
    m_rules.append(yamlNumRule);
}

// ========== M10: TOML 语法高亮规则 ==========

void CodeSyntaxHighlighter::setupTomlRules()
{
    // TOML 注释 (# 开头)
    addSingleLineCommentRule(QStringLiteral("#[^\n]*"));

    // TOML 段落头 [section] 或 [[array]]
    HighlightRule tomlSectionRule;
    tomlSectionRule.pattern = QRegularExpression(QStringLiteral("^\\s*\\[{1,2}[^\\]]+\\]{1,2}"));
    tomlSectionRule.format = m_tomlSectionFormat;
    tomlSectionRule.formatRole = QStringLiteral("tomlSection");
    m_rules.append(tomlSectionRule);

    // TOML 键名 (key = value)
    HighlightRule tomlKeyRule;
    tomlKeyRule.pattern = QRegularExpression(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_.-]*\\s*="));
    tomlKeyRule.format = m_tomlKeyFormat;
    tomlKeyRule.formatRole = QStringLiteral("tomlKey");
    m_rules.append(tomlKeyRule);

    // TOML 字符串
    HighlightRule tomlStrRule;
    tomlStrRule.pattern = QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'[^']*'|'''[\\s\\S]*?'''|\"\"\"[\\s\\S]*?\"\"\""));
    tomlStrRule.format = m_stringFormat;
    tomlStrRule.formatRole = QStringLiteral("string");
    m_rules.append(tomlStrRule);

    // TOML 布尔
    addKeywordRules({QStringLiteral("true"), QStringLiteral("false")},
                    m_constantFormat, QStringLiteral("constant"));

    // TOML 日期时间
    HighlightRule tomlDateRule;
    tomlDateRule.pattern = QRegularExpression(
        QStringLiteral("\\d{4}-\\d{2}-\\d{2}(?:T\\d{2}:\\d{2}:\\d{2})?(?:Z|[+-]\\d{2}:\\d{2})?"));
    tomlDateRule.format = m_constantFormat;
    tomlDateRule.formatRole = QStringLiteral("constant");
    m_rules.append(tomlDateRule);
}

void CodeSyntaxHighlighter::setupGoRules()
{
    m_supportsBlockComment = true;  // Go 支持 /* */ 块注释
    // Go 控制流
    addKeywordRules({
        QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("range"), QStringLiteral("switch"), QStringLiteral("case"),
        QStringLiteral("default"), QStringLiteral("break"), QStringLiteral("continue"),
        QStringLiteral("return"), QStringLiteral("goto"), QStringLiteral("fallthrough"),
        QStringLiteral("select"), QStringLiteral("defer"), QStringLiteral("go")
    }, m_controlFormat, QStringLiteral("control"));

    // Go 声明关键字
    addKeywordRules({
        QStringLiteral("func"), QStringLiteral("package"), QStringLiteral("import"),
        QStringLiteral("var"), QStringLiteral("const"), QStringLiteral("type"),
        QStringLiteral("struct"), QStringLiteral("interface"), QStringLiteral("map"),
        QStringLiteral("chan"), QStringLiteral("nil")
    }, m_keywordFormat, QStringLiteral("keyword"));

    // Go 类型
    addKeywordRules({
        QStringLiteral("bool"), QStringLiteral("int"), QStringLiteral("int8"),
        QStringLiteral("int16"), QStringLiteral("int32"), QStringLiteral("int64"),
        QStringLiteral("uint"), QStringLiteral("uint8"), QStringLiteral("uint16"),
        QStringLiteral("uint32"), QStringLiteral("uint64"), QStringLiteral("float32"),
        QStringLiteral("float64"), QStringLiteral("string"), QStringLiteral("byte"),
        QStringLiteral("rune"), QStringLiteral("error")
    }, m_typeFormat, QStringLiteral("type"));

    // Go 常量
    addKeywordRules({
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("iota")
    }, m_constantFormat, QStringLiteral("constant"));

    // Go 内置
    addKeywordRules({
        QStringLiteral("make"), QStringLiteral("new"), QStringLiteral("len"),
        QStringLiteral("cap"), QStringLiteral("append"), QStringLiteral("copy"),
        QStringLiteral("delete"), QStringLiteral("close"), QStringLiteral("panic"),
        QStringLiteral("recover"), QStringLiteral("println"), QStringLiteral("print")
    }, m_builtinFormat, QStringLiteral("builtin"));

    // Go 单行注释
    addSingleLineCommentRule(QStringLiteral("//[^\n]*"));
}

void CodeSyntaxHighlighter::setupHtmlCssRules()
{
    m_supportsBlockComment = true;  // CSS/QSS 支持 /* */ 块注释
    // HTML 标签
    HighlightRule tagRule;
    tagRule.pattern = QRegularExpression(QStringLiteral("</?\\w+[^>]*>"));
    tagRule.format = m_tagFormat;
    tagRule.formatRole = QStringLiteral("tag");
    m_rules.append(tagRule);

    // HTML 属性
    HighlightRule attrRule;
    attrRule.pattern = QRegularExpression(QStringLiteral("\\b\\w+\\s*="));
    attrRule.format = m_keywordFormat;
    attrRule.formatRole = QStringLiteral("keyword");
    m_rules.append(attrRule);

    // CSS 属性
    if (m_currentSuffix == QStringLiteral("css") || m_currentSuffix == QStringLiteral("qss")) {
        // CSS 选择器（简单匹配）
        HighlightRule selectorRule;
        selectorRule.pattern = QRegularExpression(QStringLiteral("[.#]\\w+"));
        selectorRule.format = m_tagFormat;
        selectorRule.formatRole = QStringLiteral("tag");
        m_rules.append(selectorRule);

        // CSS 属性名
        HighlightRule cssPropRule;
        cssPropRule.pattern = QRegularExpression(QStringLiteral("\\b[a-z-]+\\s*:"));
        cssPropRule.format = m_keywordFormat;
        cssPropRule.formatRole = QStringLiteral("keyword");
        m_rules.append(cssPropRule);
    }

    // 注释
    addSingleLineCommentRule(QStringLiteral("//[^\n]*"));
}

// ============================================================
// L12-L14: LSP 语义高亮
// ============================================================

void CodeSyntaxHighlighter::setSemanticSymbols(const QList<QVariantMap>& symbols)
{
    m_symbolsByLine.clear();

    // 解析 LSP documentSymbol 响应，提取符号名位置和类型
    QList<SemanticSymbol> parsed;
    parsed.reserve(symbols.size());
    for (const QVariantMap& sym : symbols) {
        parseSymbolRecursive(sym, parsed);
    }

    // 按行号索引，highlightBlock 用 O(1) 查找
    for (const SemanticSymbol& s : parsed) {
        m_symbolsByLine[s.nameLine].append(s);
    }

    // 触发重高亮（语义信息变化后需要刷新所有行）
    rehighlight();
}

void CodeSyntaxHighlighter::clearSemanticSymbols()
{
    if (m_symbolsByLine.isEmpty()) return;
    m_symbolsByLine.clear();
    rehighlight();
}

void CodeSyntaxHighlighter::setExternalSymbols(const QList<QPair<QString, QString>>& symbols)
{
    // 快速路径：空列表且当前也无外部规则 → 无需任何操作（避免无意义的 rehighlight）
    if (symbols.isEmpty() && m_externalRules.isEmpty()) return;

    m_externalRules.clear();

    // 将外部符号名转换为 HighlightRule：用 \b 边界匹配整个单词
    // 角色映射到对应的 QTextCharFormat（与 colorForRole 一致）
    for (const auto& sym : symbols) {
        const QString& name = sym.first;
        const QString& role = sym.second;
        if (name.isEmpty() || name.length() < 2) continue;  // 过滤过短符号（避免误高亮单字符）

        HighlightRule rule;
        // 转义正则特殊字符（符号名一般是 \w+，但保险起见）
        rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(name)));
        rule.formatRole = role;

        // 按角色选择格式
        if (role == QStringLiteral("funcDecl")) {
            rule.format = m_functionDeclFormat;
        } else if (role == QStringLiteral("typeDef")) {
            rule.format = m_typeDefFormat;
        } else if (role == QStringLiteral("memberVar")) {
            rule.format = m_memberVarFormat;
        } else if (role == QStringLiteral("localVar")) {
            rule.format = m_localVarFormat;
        } else if (role == QStringLiteral("constant")) {
            rule.format = m_constantFormat;
        } else {
            // 默认按类型定义处理
            rule.format = m_typeDefFormat;
            rule.formatRole = QStringLiteral("typeDef");
        }
        m_externalRules.append(rule);
    }

    // 过滤后仍为空 → 无需重高亮（避免对无本地依赖的文件触发多余刷新）
    if (m_externalRules.isEmpty()) return;

    rehighlight();
}

void CodeSyntaxHighlighter::parseSymbolRecursive(const QVariantMap& sym, QList<SemanticSymbol>& out)
{
    SemanticSymbol s;
    s.name = sym.value(QStringLiteral("name")).toString();
    s.kind = sym.value(QStringLiteral("kind")).toInt();

    // LSP documentSymbol 有两种响应格式：
    // 1. DocumentSymbol（层级）：有 selectionRange 字段，selectionRange 标记符号名精确范围
    // 2. SymbolInformation（扁平）：有 location.range 字段，range 通常覆盖符号名
    QVariantMap selRange;
    if (sym.contains(QStringLiteral("selectionRange"))) {
        // DocumentSymbol 格式 — 用 selectionRange 高亮符号名
        selRange = sym.value(QStringLiteral("selectionRange")).toMap();
    } else if (sym.contains(QStringLiteral("location"))) {
        // SymbolInformation 格式 — location.range 覆盖符号名
        QVariantMap loc = sym.value(QStringLiteral("location")).toMap();
        selRange = loc.value(QStringLiteral("range")).toMap();
    } else if (sym.contains(QStringLiteral("range"))) {
        // 退化情况：直接用 range
        selRange = sym.value(QStringLiteral("range")).toMap();
    }

    if (!selRange.isEmpty()) {
        QVariantMap start = selRange.value(QStringLiteral("start")).toMap();
        QVariantMap end = selRange.value(QStringLiteral("end")).toMap();
        s.nameLine = start.value(QStringLiteral("line")).toInt();
        s.nameStartCol = start.value(QStringLiteral("character")).toInt();
        s.nameEndCol = end.value(QStringLiteral("character")).toInt();
    }

    if (!s.name.isEmpty() && s.nameEndCol > s.nameStartCol) {
        out.append(s);
    }

    // 递归处理子符号（DocumentSymbol 格式支持嵌套作用域）
    QVariant childrenVar = sym.value(QStringLiteral("children"));
    if (childrenVar.isValid()) {
        QVariantList children = childrenVar.toList();
        for (const QVariant& child : children) {
            parseSymbolRecursive(child.toMap(), out);
        }
    }
}

QTextCharFormat CodeSyntaxHighlighter::formatForSymbolKind(int kind) const
{
    // LSP SymbolKind 映射：
    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#symbolKind
    // 1=File 2=Module 3=Namespace 4=Package 5=Class 6=Method 7=Property
    // 8=Field 9=Constructor 10=Enum 11=Interface 12=Function 13=Variable
    // 14=Constant 15=String 16=Number 17=Boolean 18=Array 19=Object
    // 20=Key 21=Null 22=EnumMember 23=Struct 24=Event 25=Operator 26=TypeParameter

    switch (kind) {
    // 函数声明：Function(12), Method(6), Constructor(9)
    case 6: case 9: case 12:
        return m_functionDeclFormat;
    // 类型定义：Class(5), Interface(11), Struct(23), Enum(10), TypeParameter(26)
    case 5: case 10: case 11: case 23: case 26:
        return m_typeDefFormat;
    // 成员变量：Field(8), Property(7)
    case 7: case 8:
        return m_memberVarFormat;
    // 局部变量：Variable(13)
    case 13:
        return m_localVarFormat;
    // 常量：Constant(14), EnumMember(22) — 复用已有常量格式
    case 14: case 22:
        return m_constantFormat;
    // 其他类型（Module/Namespace/Package/Event/Operator/...）不特殊高亮
    default:
        return m_typeDefFormat;
    }
}
