#include "core/editor/CodeSyntaxHighlighter.h"
#include "core/config/ThemeManager.h"
#include <QSet>

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

    // P3/P4: 注释域细分高亮格式
    m_doxyFormat.setFontWeight(QFont::Bold);   // Doxygen 标签加粗
    m_todoFormat.setFontItalic(true);           // TODO 标记斜体

    // L13: 语义高亮格式 — 函数声明用斜体区分正则匹配的函数调用
    m_functionDeclFormat.setFontItalic(true);

    updateThemeColors();
    setupGenericRules();
    setupFallbackHeuristics();
}

QColor CodeSyntaxHighlighter::colorForRole(const QString& role) const
{
    // R5: 哈希查表 O(1)，替代 17 个 if-else 字符串比较
    auto it = m_roleColorMap.constFind(role);
    if (it != m_roleColorMap.constEnd()) {
        return it.value();
    }
    return ThemeManager::instance().currentPalette().fgPrimary;
}

void CodeSyntaxHighlighter::updateThemeColors()
{
    // R5: 构建 role→color 映射表（主题切换时重建一次，colorForRole 查表 O(1)）
    const auto& sx = ThemeManager::instance().currentPalette().syntax;
    m_roleColorMap.clear();
    m_roleColorMap[QStringLiteral("keyword")]      = sx.keyword;
    m_roleColorMap[QStringLiteral("control")]      = sx.control;
    m_roleColorMap[QStringLiteral("string")]       = sx.string;
    m_roleColorMap[QStringLiteral("number")]       = sx.number;
    m_roleColorMap[QStringLiteral("comment")]      = sx.comment;
    m_roleColorMap[QStringLiteral("function")]     = sx.function;
    m_roleColorMap[QStringLiteral("type")]         = sx.type;
    m_roleColorMap[QStringLiteral("preprocessor")] = sx.preprocessor;
    m_roleColorMap[QStringLiteral("builtin")]      = sx.builtin;
    m_roleColorMap[QStringLiteral("decorator")]    = sx.decorator;
    m_roleColorMap[QStringLiteral("constant")]     = sx.constant;
    m_roleColorMap[QStringLiteral("tag")]          = sx.tag;
    m_roleColorMap[QStringLiteral("yamlKey")]      = sx.yamlKey;
    m_roleColorMap[QStringLiteral("tomlKey")]      = sx.tomlKey;
    m_roleColorMap[QStringLiteral("tomlSection")]  = sx.tomlSection;
    m_roleColorMap[QStringLiteral("funcDecl")]     = sx.funcDecl;
    m_roleColorMap[QStringLiteral("typeDef")]      = sx.typeDef;
    m_roleColorMap[QStringLiteral("memberVar")]    = sx.memberVar;
    m_roleColorMap[QStringLiteral("localVar")]     = sx.localVar;
    m_roleColorMap[QStringLiteral("doxy")]         = sx.doxy;     // P3: Doxygen 标签
    m_roleColorMap[QStringLiteral("todo")]         = sx.todo;     // P4: TODO 标记
    m_roleColorMap[QStringLiteral("headerPath")]   = sx.headerPath; // Bug1: 头文件路径

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

    // P3/P4: 注释域细分高亮格式颜色
    m_doxyFormat.setForeground(colorForRole(QStringLiteral("doxy")));
    m_todoFormat.setForeground(colorForRole(QStringLiteral("todo")));
    // 保留字体属性
    m_doxyFormat.setFontWeight(QFont::Bold);
    m_todoFormat.setFontItalic(true);

    // Bug1: #include 头文件路径格式（链接风格，下划线区分可点击）
    m_headerPathFormat.setForeground(colorForRole(QStringLiteral("headerPath")));
    m_headerPathFormat.setFontUnderline(true);

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

    // R5: 同步更新启发式兜底规则颜色（仅更新 format，不重建正则规则）
    // setupFallbackHeuristics() 在构造函数中已构建一次正则，此处只需刷新颜色
    for (auto& rule : m_fallbackRules) {
        if (!rule.formatRole.isEmpty()) {
            rule.format.setForeground(colorForRole(rule.formatRole));
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
    // P3/P4: 收集注释区间，用于后续 Doxygen 标签 / TODO 标记细分高亮
    // 每个元素为 (start, length)，表示一段连续的注释文本范围
    QList<QPair<int, int>> commentRegions;

    // 0. P1-3: 启发式兜底高亮 — 最低优先级，作为基础着色
    //    先应用兜底规则，后续的静态规则/外部符号/注释/语义高亮会覆盖兜底配色
    //    这样注释/字符串中的标识符不会被误高亮（被后续规则覆盖）
    //
    //    H2: 始终应用启发式兜底（即使 LSP Ready）
    //    原因：LSP documentSymbol 只高亮符号"声明位置"，不高亮"使用位置"。
    //    例如 MySingleton 在 .cpp 中作为类型使用时，LSP 不会标记使用位置。
    //    启发式兜底（PascalCase→类型）在使用位置提供基础着色，
    //    LSP 语义高亮在声明位置覆盖（步骤3 优先级更高），两者互补不冲突。
    for (const auto& rule : m_fallbackRules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // 1. 应用单行规则（关键字/字符串/数字/单行注释等）— 覆盖兜底配色
    for (const auto& rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            int matchStart = match.capturedStart();
            int matchLen = match.capturedLength();
            setFormat(matchStart, matchLen, rule.format);
            // P3/P4: 记录单行注释区间（formatRole == "comment"）
            if (rule.formatRole == QStringLiteral("comment")) {
                commentRegions.append(qMakePair(matchStart, matchLen));
            }
        }
    }

    // 1.5 应用外部符号规则（来自 #include/import 的本地头文件符号）
    //     在普通规则之后、块注释之前应用，可覆盖普通关键字配色
    //     例：源文件中引用的自定义类名 ILineNumber 会被高亮为类型色
    // C02-3: 命中时记录行号到 m_externalSymbolLines，供 setExternalSymbols 增量重高亮
    bool extHit = false;
    for (const auto& rule : m_externalRules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            extHit = true;
        }
    }
    if (extHit) {
        m_externalSymbolLines.insert(currentBlock().blockNumber());
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
            // P3/P4: 记录块注释区间
            commentRegions.append(qMakePair(startIndex, commentLength));
            // 继续查找本行后续的 /*（处理一行多个块注释的情况）
            startIndex = text.indexOf(QStringLiteral("/*"), startIndex + commentLength);
        }
    }

    // P3/P4: 注释域细分高亮 — 在已着色的注释区间内扫描 Doxygen 标签和 TODO 标记
    // 作为内置基础高亮，无论 LSP 是否在线都生效（兜底能力，不受 clangd 状态影响）
    for (const auto& region : commentRegions) {
        highlightCommentInternals(region.first, region.second, text);
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

// ============================================================
// P3/P4: 注释域细分高亮 — Doxygen 标签 + TODO 标记
// ============================================================

void CodeSyntaxHighlighter::highlightCommentInternals(int start, int length, const QString& text)
{
    if (length <= 0 || start < 0 || start + length > text.length()) return;

    // 截取注释区间子串，正则匹配在子串上进行，结果偏移回原文本坐标
    QString commentText = text.mid(start, length);

    // P3: Doxygen 标签 — 注释域内以 @ 开头的标准关键字
    // 匹配: @brief @data @author @param @tparam @return @returns @note @warning
    //       @see @since @version @date @throws @throw @exception @code @endcode
    //       @file @mainpage @section @subsection @bug @todo @deprecated
    // 样式: 橙黄/浅紫色 + 加粗，与普通注释文字区分
    static const QRegularExpression doxyRegex(QStringLiteral(
        "@(?:brief|data|author|param|tparam|return|returns|note|warning|"
        "see|since|version|date|throws|throw|exception|code|endcode|"
        "file|mainpage|section|subsection|bug|todo|deprecated|"
        "internal|endinternal|class|struct|enum|fn|var|def|typedef|namespace)\\b"
    ));

    QRegularExpressionMatchIterator it = doxyRegex.globalMatch(commentText);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(start + match.capturedStart(), match.capturedLength(), m_doxyFormat);
    }

    // P4: TODO/FIXME/NOTE 待办标记 — 对齐 Java 编辑器体验
    // 匹配: TODO FIXME NOTE XXX HACK
    // 样式: 暖红色 + 斜体，突出待修改标记，快速定位代码遗留点
    static const QRegularExpression todoRegex(QStringLiteral(
        "\\b(?:TODO|FIXME|NOTE|XXX|HACK)\\b"
    ));

    it = todoRegex.globalMatch(commentText);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        setFormat(start + match.capturedStart(), match.capturedLength(), m_todoFormat);
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

    // C++ 预处理指令（不含 include，include 由专门的规则用头文件路径色高亮）
    HighlightRule preprocessorRule;
    preprocessorRule.pattern = QRegularExpression(QStringLiteral("#\\s*(?:define|ifdef|ifndef|if|else|elif|endif|pragma|undef|error|warning|line)\\b[^\n]*"));
    preprocessorRule.format = m_preprocessorFormat;
    preprocessorRule.formatRole = QStringLiteral("preprocessor");
    m_rules.append(preprocessorRule);

    // Bug1: #include 指令关键字 → 预处理色（与 #define 等一致）
    HighlightRule includeDirectiveRule;
    includeDirectiveRule.pattern = QRegularExpression(QStringLiteral("#\\s*include\\b"));
    includeDirectiveRule.format = m_preprocessorFormat;
    includeDirectiveRule.formatRole = QStringLiteral("preprocessor");
    m_rules.append(includeDirectiveRule);

    // C++ 单行注释
    addSingleLineCommentRule(QStringLiteral("//[^\n]*"));

    // ===== [VSCode风格增强] 新增规则 =====

    // 函数名高亮：匹配 identifier 后紧跟 (
    // P1-1 修复：添加负向前瞻排除控制流关键字，避免 if(/for(/while(/switch(/catch( 等被误高亮为函数色
    // 原正则 \b[a-zA-Z_]\w*\s*(?=\() 会覆盖关键字高亮（setFormat 后写覆盖先写）
    HighlightRule funcRule;
    funcRule.pattern = QRegularExpression(QStringLiteral(
        "\\b(?!if\\b|else\\b|for\\b|while\\b|do\\b|switch\\b|case\\b|catch\\b|"
        "return\\b|throw\\b|sizeof\\b|new\\b|delete\\b|alignof\\b|typeid\\b|"
        "co_await\\b|co_yield\\b|co_return\\b|static_assert\\b|noexcept\\b)"
        "[a-zA-Z_]\\w*\\s*(?=\\()"
    ));
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

    // Bug3: Qt 标准库常用类型 — 使用类型专属高亮配色，与普通变量区分
    // 涵盖 Qt Core/Gui/Widgets/Network/Sql 等模块常用类
    addKeywordRules({
        // Qt Core 基础类型
        QStringLiteral("QString"), QStringLiteral("QStringList"), QStringLiteral("QList"),
        QStringLiteral("QVector"), QStringLiteral("QMap"), QStringLiteral("QHash"),
        QStringLiteral("QSet"), QStringLiteral("QPair"), QStringLiteral("QVariant"),
        QStringLiteral("QVariantMap"), QStringLiteral("QVariantList"),
        QStringLiteral("QByteArray"), QStringLiteral("QChar"), QStringLiteral("QBitArray"),
        QStringLiteral("QDateTime"), QStringLiteral("QDate"), QStringLiteral("QTime"),
        QStringLiteral("QTimer"), QStringLiteral("QObject"), QStringLiteral("QEvent"),
        QStringLiteral("QEventLoop"), QStringLiteral("QUrl"), QStringLiteral("QUrlQuery"),
        QStringLiteral("QFile"), QStringLiteral("QFileInfo"), QStringLiteral("QDir"),
        QStringLiteral("QFileDialog"), QStringLiteral("QTextStream"), QStringLiteral("QDataStream"),
        QStringLiteral("QSettings"), QStringLiteral("QProcess"), QStringLiteral("QThread"),
        QStringLiteral("QMutex"), QStringLiteral("QMutexLocker"), QStringLiteral("QSemaphore"),
        QStringLiteral("QWaitCondition"), QStringLiteral("QFuture"), QStringLiteral("QFutureWatcher"),
        QStringLiteral("QConcurrent"), QStringLiteral("QtConcurrent"),
        QStringLiteral("QJsonDocument"), QStringLiteral("QJsonObject"), QStringLiteral("QJsonArray"),
        QStringLiteral("QJsonValue"), QStringLiteral("QJsonParseError"),
        QStringLiteral("QRegularExpression"), QStringLiteral("QRegularExpressionMatch"),
        QStringLiteral("QPoint"), QStringLiteral("QPointF"), QStringLiteral("QRect"),
        QStringLiteral("QRectF"), QStringLiteral("QSize"), QStringLiteral("QSizeF"),
        QStringLiteral("QLine"), QStringLiteral("QLineF"), QStringLiteral("QMargins"),
        QStringLiteral("QColor"), QStringLiteral("QFont"), QStringLiteral("QFontMetrics"),
        QStringLiteral("QPalette"), QStringLiteral("QBrush"), QStringLiteral("QPen"),
        QStringLiteral("QImage"), QStringLiteral("QPixmap"), QStringLiteral("QIcon"),
        QStringLiteral("QPainter"), QStringLiteral("QPainterPath"),
        QStringLiteral("QUuid"), QStringLiteral("QLocale"), QStringLiteral("QTranslator"),
        QStringLiteral("QCoreApplication"), QStringLiteral("QApplication"),
        // Qt Widgets
        QStringLiteral("QWidget"), QStringLiteral("QMainWindow"), QStringLiteral("QDialog"),
        QStringLiteral("QPushButton"), QStringLiteral("QLabel"), QStringLiteral("QLineEdit"),
        QStringLiteral("QTextEdit"), QStringLiteral("QPlainTextEdit"), QStringLiteral("QComboBox"),
        QStringLiteral("QCheckBox"), QStringLiteral("QRadioButton"), QStringLiteral("QButtonGroup"),
        QStringLiteral("QSpinBox"), QStringLiteral("QDoubleSpinBox"), QStringLiteral("QSlider"),
        QStringLiteral("QProgressBar"), QStringLiteral("QTabWidget"), QStringLiteral("QTabBar"),
        QStringLiteral("QListWidget"), QStringLiteral("QTreeWidget"), QStringLiteral("QTableWidget"),
        QStringLiteral("QScrollArea"), QStringLiteral("QSplitter"), QStringLiteral("QGroupBox"),
        QStringLiteral("QVBoxLayout"), QStringLiteral("QHBoxLayout"), QStringLiteral("QGridLayout"),
        QStringLiteral("QFormLayout"), QStringLiteral("QStackedWidget"),
        QStringLiteral("QToolBar"), QStringLiteral("QStatusBar"), QStringLiteral("QMenuBar"),
        QStringLiteral("QMenu"), QStringLiteral("QAction"), QStringLiteral("QActionGroup"),
        QStringLiteral("QDockWidget"), QStringLiteral("QMessageBox"), QStringLiteral("QInputDialog"),
        QStringLiteral("QColorDialog"), QStringLiteral("QFontDialog"),
        QStringLiteral("QHeaderView"), QStringLiteral("QFrame"),
        QStringLiteral("QTextDocument"), QStringLiteral("QTextCursor"), QStringLiteral("QTextBlock"),
        QStringLiteral("QTextCharFormat"), QStringLiteral("QTextFormat"),
        QStringLiteral("QSyntaxHighlighter"), QStringLiteral("QCompleter"),
        QStringLiteral("QShortcut"), QStringLiteral("QKeySequence"),
        QStringLiteral("QFileSystemWatcher"), QStringLiteral("QFileSystemModel"),
        QStringLiteral("QStandardItemModel"), QStringLiteral("QAbstractItemModel"),
        QStringLiteral("QStringListModel"), QStringLiteral("QSortFilterProxyModel"),
        // Qt Network
        QStringLiteral("QNetworkAccessManager"), QStringLiteral("QNetworkRequest"),
        QStringLiteral("QNetworkReply"), QStringLiteral("QNetworkCookie"),
        QStringLiteral("QTcpSocket"), QStringLiteral("QTcpServer"), QStringLiteral("QUdpSocket"),
        QStringLiteral("QSslSocket"), QStringLiteral("QLocalSocket"), QStringLiteral("QLocalServer"),
        // Qt Sql
        QStringLiteral("QSqlDatabase"), QStringLiteral("QSqlQuery"), QStringLiteral("QSqlError"),
        QStringLiteral("QSqlRecord"), QStringLiteral("QSqlTableModel")
    }, m_typeFormat, QStringLiteral("type"));

    // Bug1: #include 头文件路径 → headerPath 专属色（下划线链接风格，区别于普通字符串）
    // 使用 lookbehind 仅匹配路径部分，不覆盖 #include 关键字的高亮
    HighlightRule includePathRule;
    includePathRule.pattern = QRegularExpression(
        QStringLiteral("(?<=#\\s*include\\s+)(?:<[^>]+>|\"[^\"]+\")"));
    includePathRule.format = m_headerPathFormat;
    includePathRule.formatRole = QStringLiteral("headerPath");
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
    // P0 C02: 增量高亮 — 只重高亮有语义符号变化的行，避免全量 rehighlight
    // 收集旧符号所在行号（需要重高亮以移除旧配色）
    QSet<int> affectedLines;
    for (auto it = m_symbolsByLine.constBegin(); it != m_symbolsByLine.constEnd(); ++it) {
        affectedLines.insert(it.key());
    }

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
        affectedLines.insert(s.nameLine);
    }

    // P0 C02: 只重高亮受影响的行，而非全量 rehighlight
    // 对于 10k+ 行文件，全量 rehighlight 可能耗时数百毫秒，增量重高亮仅耗时数毫秒
    QTextDocument* doc = document();
    if (doc) {
        for (int line : affectedLines) {
            QTextBlock block = doc->findBlockByNumber(line);
            if (block.isValid()) {
                rehighlightBlock(block);
            }
        }
    }
}

void CodeSyntaxHighlighter::clearSemanticSymbols()
{
    if (m_symbolsByLine.isEmpty()) return;

    // P0 C02: 增量重高亮 — 只重高亮有语义符号的行
    QSet<int> affectedLines;
    for (auto it = m_symbolsByLine.constBegin(); it != m_symbolsByLine.constEnd(); ++it) {
        affectedLines.insert(it.key());
    }
    m_symbolsByLine.clear();

    QTextDocument* doc = document();
    if (doc) {
        for (int line : affectedLines) {
            QTextBlock block = doc->findBlockByNumber(line);
            if (block.isValid()) {
                rehighlightBlock(block);
            }
        }
    }
}

void CodeSyntaxHighlighter::setExternalSymbols(const QList<QPair<QString, QString>>& symbols)
{
    // C02-3: 增量高亮 — 只重高亮受影响行，避免全量 rehighlight
    // 受影响行 = 旧命中行（需重高亮清除旧配色）+ 含新增符号名的行（需重高亮应用新配色）
    if (symbols.isEmpty() && m_externalRules.isEmpty()) return;

    // 收集新符号名集合（与规则构建的过滤条件一致：长度>=2 且角色为 funcDecl/typeDef/constant）
    QSet<QString> newNames;
    for (const auto& sym : symbols) {
        if (sym.first.length() >= 2 &&
            (sym.second == QStringLiteral("funcDecl") ||
             sym.second == QStringLiteral("typeDef") ||
             sym.second == QStringLiteral("constant"))) {
            newNames.insert(sym.first);
        }
    }

    // 1. 旧命中行需重高亮（清除旧配色或应用新配色）
    QSet<int> affectedLines = m_externalSymbolLines;

    // 2. 含新增符号名的行需重高亮（应用新配色）
    //    用 QString::contains 预筛，rehighlightBlock 时由正则精确匹配（可能含少量误判，无害）
    QSet<QString> addedNames = newNames - m_externalSymbolNames;
    if (!addedNames.isEmpty()) {
        QTextDocument* doc = document();
        if (doc) {
            for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
                const QString& text = block.text();
                for (const QString& name : addedNames) {
                    if (text.contains(name, Qt::CaseSensitive)) {
                        affectedLines.insert(block.blockNumber());
                        break;  // 该行已加入，无需检查其他符号
                    }
                }
            }
        }
    }

    // 更新符号名集合
    m_externalSymbolNames = newNames;

    // 清空命中行缓存（rehighlightBlock 时由 highlightBlock 重新填充）
    m_externalSymbolLines.clear();

    // 重建外部规则（保留原有逻辑：\b 边界匹配 + 角色映射格式）
    m_externalRules.clear();
    for (const auto& sym : symbols) {
        const QString& name = sym.first;
        const QString& role = sym.second;
        if (name.isEmpty() || name.length() < 2) continue;  // 过滤过短符号

        if (role != QStringLiteral("funcDecl") &&
            role != QStringLiteral("typeDef") &&
            role != QStringLiteral("constant")) {
            continue;
        }

        HighlightRule rule;
        // 转义正则特殊字符（符号名一般是 \w+，但保险起见）
        rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(name)));
        rule.formatRole = role;

        // 按角色选择格式
        if (role == QStringLiteral("funcDecl")) {
            rule.format = m_functionDeclFormat;
        } else if (role == QStringLiteral("constant")) {
            // H2: constant 角色使用常量格式
            rule.format = m_constantFormat;
        } else {  // typeDef
            rule.format = m_typeDefFormat;
        }
        m_externalRules.append(rule);
    }

    // 增量重高亮：只重高亮受影响行，而非全量 rehighlight
    // 对 10k+ 行文件，全量 rehighlight 可能耗时数百毫秒，增量重高亮仅耗时数毫秒
    // 注意：外部符号规则为单行正则，不改变 blockState，rehighlightBlock 不会破坏 /* */ 状态机
    QTextDocument* doc = document();
    if (doc) {
        for (int line : affectedLines) {
            QTextBlock block = doc->findBlockByNumber(line);
            if (block.isValid()) {
                rehighlightBlock(block);
            }
        }
    }
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

// ============================================================
// P1-3: 双轨高亮降级 — 启发式兜底
// ============================================================

void CodeSyntaxHighlighter::setLspState(LspHighlightState state)
{
    if (m_lspState == state) return;
    m_lspState = state;
    // 状态变化时重高亮（启用/禁用启发式兜底）
    rehighlight();
}

void CodeSyntaxHighlighter::setupFallbackHeuristics()
{
    // P1-3: 启发式兜底规则 — LSP 未就绪时用命名约定推断标识符类型
    // 这些规则在 highlightBlock 的步骤0中应用，仅当 m_lspState != Ready 时生效
    // 优先级最低：先于静态规则应用，被关键字/字符串/注释/语义高亮覆盖
    m_fallbackRules.clear();

    // 规则1: PascalCase 标识符 → 类型（首字母大写，后跟小写字母或数字）
    // 匹配: MyClass, ILineNumber, QWidget, CodeSyntaxHighlighter
    // 排除: 全大写（ALL_CAPS 由规则3处理）、单个字母
    HighlightRule pascalRule;
    pascalRule.pattern = QRegularExpression(QStringLiteral(
        "\\b[A-Z][a-z][a-zA-Z0-9_]*\\b"
    ));
    pascalRule.format = m_typeDefFormat;
    pascalRule.formatRole = QStringLiteral("typeDef");
    m_fallbackRules.append(pascalRule);

    // 规则2: m_ 前缀标识符 → 成员变量
    // 匹配: m_count, m_lspManager, m_filePath
    HighlightRule memberRule;
    memberRule.pattern = QRegularExpression(QStringLiteral(
        "\\bm_[a-zA-Z_][a-zA-Z0-9_]*\\b"
    ));
    memberRule.format = m_memberVarFormat;
    memberRule.formatRole = QStringLiteral("memberVar");
    m_fallbackRules.append(memberRule);

    // 规则3: ALL_CAPS 标识符 → 常量（至少2个字符，含下划线或连续大写）
    // 匹配: MAX_SIZE, DEFAULT_TIMEOUT, PI, NULL
    // 排除: 单个字母（如 I, A）避免误高亮
    HighlightRule constRule;
    constRule.pattern = QRegularExpression(QStringLiteral(
        "\\b[A-Z][A-Z0-9_]{1,}\\b"
    ));
    constRule.format = m_constantFormat;
    constRule.formatRole = QStringLiteral("constant");
    m_fallbackRules.append(constRule);
}
