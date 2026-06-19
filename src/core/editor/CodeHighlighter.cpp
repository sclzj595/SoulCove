#include "core/editor/CodeHighlighter.h"

QHash<QString, CodeHighlighter::LanguageRules> CodeHighlighter::s_rules;
bool CodeHighlighter::s_initialized = false;

void CodeHighlighter::initRules()
{
    if (s_initialized) return;
    s_initialized = true;

    // ===== C / C++ =====
    {
        LanguageRules r;
        // 预处理器指令
        r.patterns.append({QRegularExpression(QStringLiteral(R"(#\s*\w+)")),            QStringLiteral("hl-pp")});
        // 注释
        r.patterns.append({QRegularExpression(QStringLiteral(R"(//[^\n]*)")),             QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(/\*[\s\S]*?\*/)")),       QStringLiteral("hl-cmt")});
        // 字符串
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),    QStringLiteral("hl-str")});
        // 字符
        r.patterns.append({QRegularExpression(QStringLiteral(R"('(?:[^'\\]|\\.)*')")),    QStringLiteral("hl-str")});
        // 数字
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b\d+\.?\d*[fFlL]?\b)")),QStringLiteral("hl-num")});
        // 类型（单段完整正则，避免多行R字符串拼接引入换行符导致正则非法）
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:int|char|float|double|bool|void|long|short|unsigned|signed|auto|size_t|uint8_t|int32_t|wchar_t|std::string|std::vector|std::map)\b)")),
            QStringLiteral("hl-type")});
        // 关键字
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:if|else|for|while|do|switch|case|break|continue|return|goto|class|struct|enum|union|namespace|public|private|protected|virtual|override|final|static|const|constexpr|inline|extern|volatile|new|delete|this|template|typename|typedef|try|catch|throw|noexcept|sizeof|nullptr|true|false)\b)")),
            QStringLiteral("hl-kw")});
        // 函数调用
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b([a-zA-Z_]\w*)\s*\()")), QStringLiteral("hl-fn")});

        s_rules[QStringLiteral("c")]     = r;
        s_rules[QStringLiteral("cpp")]   = r;
        s_rules[QStringLiteral("c++")]   = r;
        s_rules[QStringLiteral("h")]     = r;
        s_rules[QStringLiteral("hpp")]   = r;
        s_rules[QStringLiteral("cxx")]   = r;
        s_rules[QStringLiteral("cc")]    = r;
    }

    // ===== Python =====
    {
        LanguageRules r;
        // 注释
        r.patterns.append({QRegularExpression(QStringLiteral(R"(#[^\n]*)")),             QStringLiteral("hl-cmt")});
        // 字符串
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\"\"\"[\s\S]*?\"\"\")")), QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"('''[\s\S]*?''')")),      QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),   QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"('(?:[^'\\]|\\.)*')")),   QStringLiteral("hl-str")});
        // 数字
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b\d+\.?\d*\b)")),       QStringLiteral("hl-num")});
        // 关键字
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:def|class|import|from|as|return|yield|if|elif|else|for|while|try|except|finally|with|pass|break|continue|raise|assert|del|global|nonlocal|lambda|and|or|not|in|is|True|False|None|self)\b)")),
            QStringLiteral("hl-kw")});
        // 装饰器
        r.patterns.append({QRegularExpression(QStringLiteral(R"(@\w+)")),                QStringLiteral("hl-pp")});
        // 函数定义/调用
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b(?:def)\s+(\w+))")),   QStringLiteral("hl-fn")});

        s_rules[QStringLiteral("python")] = r;
        s_rules[QStringLiteral("py")]     = r;
    }

    // ===== JavaScript / TypeScript =====
    {
        LanguageRules r;
        // 注释
        r.patterns.append({QRegularExpression(QStringLiteral(R"(//[^\n]*)")),             QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(/\*[\s\S]*?\*/)")),       QStringLiteral("hl-cmt")});
        // 字符串
        r.patterns.append({QRegularExpression(QStringLiteral(R"(`(?:[^`\\]|\\.)*`)")),    QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),    QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"('(?:[^'\\]|\\.)*')")),    QStringLiteral("hl-str")});
        // 数字
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b\d+\.?\d*[eE]?\d*\b)")),QStringLiteral("hl-num")});
        // 关键字
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:const|let|var|function|class|extends|import|export|default|from|as|return|yield|await|async|if|else|for|while|do|switch|case|break|continue|try|catch|throw|new|delete|typeof|instanceof|in|of|this|super|true|false|null|undefined|void|static|get|set)\b)")),
            QStringLiteral("hl-kw")});

        s_rules[QStringLiteral("javascript")] = r;
        s_rules[QStringLiteral("js")]         = r;
        s_rules[QStringLiteral("typescript")] = r;
        s_rules[QStringLiteral("ts")]         = r;
        s_rules[QStringLiteral("jsx")]        = r;
        s_rules[QStringLiteral("tsx")]        = r;
    }

    // ===== Java =====
    {
        LanguageRules r;
        // 注释
        r.patterns.append({QRegularExpression(QStringLiteral(R"(//[^\n]*)")),             QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(/\*[\s\S]*?\*/)")),       QStringLiteral("hl-cmt")});
        // 字符串
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),    QStringLiteral("hl-str")});
        // 注解
        r.patterns.append({QRegularExpression(QStringLiteral(R"(@\w+)")),                QStringLiteral("hl-pp")});
        // 数字
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b\d+\.?\d*[fFlL]?\b)")),QStringLiteral("hl-num")});
        // 类型
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:int|char|float|double|boolean|void|long|short|byte|String|Integer|List|Map|Object)\b)")),
            QStringLiteral("hl-type")});
        // 关键字
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:public|private|protected|static|final|abstract|class|interface|extends|implements|new|return|if|else|for|while|do|switch|case|break|continue|try|catch|throw|throws|import|package|this|super|true|false|null|synchronized|volatile|transient)\b)")),
            QStringLiteral("hl-kw")});

        s_rules[QStringLiteral("java")] = r;
    }

    // ===== Shell / Bash =====
    {
        LanguageRules r;
        r.patterns.append({QRegularExpression(QStringLiteral(R"(#[^\n]*)")),             QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),   QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"('(?:[^'\\]|\\.)*')")),   QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:if|then|else|elif|fi|for|while|do|done|case|esac|function|return|exit|export|local|readonly|echo|source|cd|ls|pwd|mkdir|rm|cp|mv|cat|grep|sed|awk|chmod)\b)")),
            QStringLiteral("hl-kw")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(^\s*(?:sudo|apt|yum|brew|git|npm|pip|docker)\b)")),
            QStringLiteral("hl-fn")});

        s_rules[QStringLiteral("bash")]   = r;
        s_rules[QStringLiteral("sh")]     = r;
        s_rules[QStringLiteral("shell")]  = r;
        s_rules[QStringLiteral("zsh")]    = r;
    }

    // ===== JSON =====
    {
        LanguageRules r;
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),   QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b\d+\.?\d*[eE]?\d*\b)")),QStringLiteral("hl-num")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b(?:true|false|null)\b)")), QStringLiteral("hl-kw")});

        s_rules[QStringLiteral("json")] = r;
    }

    // ===== YAML =====
    {
        LanguageRules r;
        r.patterns.append({QRegularExpression(QStringLiteral(R"(#[^\n]*)")),             QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),   QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b(?:true|false|null|yes|no)\b)")), QStringLiteral("hl-kw")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(^[\s]*[\w-]+:)")),       QStringLiteral("hl-fn")});

        s_rules[QStringLiteral("yaml")] = r;
        s_rules[QStringLiteral("yml")]  = r;
    }

    // ===== Go =====
    {
        LanguageRules r;
        r.patterns.append({QRegularExpression(QStringLiteral(R"(//[^\n]*)")),             QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(/\*[\s\S]*?\*/)")),       QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),    QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(`[^`]*`)")),              QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b\d+\.?\d*[eE]?\d*\b)")),QStringLiteral("hl-num")});
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:func|package|import|type|struct|interface|map|chan|go|defer|return|if|else|for|range|switch|case|break|continue|var|const|nil|true|false|select|fallthrough|string|int|float64|bool|byte|error)\b)")),
            QStringLiteral("hl-kw")});

        s_rules[QStringLiteral("go")]   = r;
    }

    // ===== Rust =====
    {
        LanguageRules r;
        r.patterns.append({QRegularExpression(QStringLiteral(R"(//[^\n]*)")),             QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral("/\\*[\\s\\S]*?\\*/")),       QStringLiteral("hl-cmt")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),    QStringLiteral("hl-str")});
        r.patterns.append({QRegularExpression(QStringLiteral(R"(\b\d+\.?\d*[fui](?:8|16|32|64|128|size)?\b)")), QStringLiteral("hl-num")});
        r.patterns.append({QRegularExpression(
            QStringLiteral(R"(\b(?:fn|let|mut|const|static|pub|use|mod|struct|enum|impl|trait|match|if|else|for|while|loop|in|return|self|Self|super|crate|where|as|ref|move|async|await|true|false|Some|None|Ok|Err|Option|Result|Vec|String|i32|u32|f64|bool|char|str|usize)\b)")),
            QStringLiteral("hl-kw")});

        s_rules[QStringLiteral("rust")] = r;
        s_rules[QStringLiteral("rs")]   = r;
    }
}

const CodeHighlighter::LanguageRules& CodeHighlighter::rulesForLanguage(const QString& lang)
{
    initRules();
    QString lower = lang.toLower().trimmed();
    if (s_rules.contains(lower))
        return s_rules[lower];
    // fallback: 尝试通用 C 风格高亮
    if (s_rules.contains(QStringLiteral("cpp")))
        return s_rules[QStringLiteral("cpp")];
    // 最终 fallback: 空规则
    static LanguageRules emptyRules;
    return emptyRules;
}

QString CodeHighlighter::escapeHtml(const QString& text)
{
    QString result = text;
    result.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    result.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    result.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    return result;
}

QString CodeHighlighter::highlightBlock(const QString& code, const QString& lang)
{
    if (code.trimmed().isEmpty()) return QString();

    const LanguageRules& rules = rulesForLanguage(lang);
    if (rules.patterns.isEmpty()) return code;

    // 用占位符标记已处理的区域，防止嵌套替换
    struct Range { qsizetype start; qsizetype end; QString cls; };
    QVector<Range> ranges;

    for (const auto& [re, cls] : rules.patterns) {
        QRegularExpressionMatchIterator it = re.globalMatch(code);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            ranges.append({m.capturedStart(), m.capturedEnd(), cls});
        }
    }

    if (ranges.isEmpty()) return code;

    // 按起始位置排序，长的优先（让更具体的规则优先）
    std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) {
        if (a.start != b.start) return a.start < b.start;
        return (a.end - a.start) > (b.end - b.start); // 长的优先
    });

    // 合并重叠区间（不重叠的保留）
    QVector<Range> merged;
    for (const auto& r : ranges) {
        bool overlaps = false;
        for (const auto& m : merged) {
            if (r.start < m.end && r.end > m.start) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) merged.append(r);
    }

    // 按开始位置排序
    std::sort(merged.begin(), merged.end(), [](const Range& a, const Range& b) {
        return a.start < b.start;
    });

    // 构建高亮结果
    QString result;
    int pos = 0;
    for (const auto& r : merged) {
        if (r.start > pos)
            result += code.mid(pos, r.start - pos);
        result += QStringLiteral("<span class=\"%1\">").arg(r.cls);
        result += code.mid(r.start, r.end - r.start);
        result += QStringLiteral("</span>");
        pos = r.end;
    }
    if (pos < code.length())
        result += code.mid(pos);

    return result;
}

QString CodeHighlighter::highlightHtml(const QString& html)
{
    // 匹配 <code class="language-xxx"> 或 <code> 标签
    static QRegularExpression codeRe(
        QStringLiteral(R"re(<code(?:\s+class="language-(\w+)")?\s*>([\s\S]*?)</code>)re"),
        QRegularExpression::CaseInsensitiveOption
    );

    QString result = html;
    QRegularExpressionMatchIterator it = codeRe.globalMatch(html);

    // 从后往前替换，避免偏移问题
    QVector<QPair<int, int>> replacements; // {start, length}
    QVector<QString> newTexts;

    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QString lang = m.captured(1);
        QString code = m.captured(2);
        QString highlighted = highlightBlock(code, lang);

        replacements.append({m.capturedStart(2), m.capturedLength(2)});
        newTexts.append(highlighted);
    }

    // 从后往前替换
    for (int i = replacements.size() - 1; i >= 0; --i) {
        result.replace(replacements[i].first, replacements[i].second, newTexts[i]);
    }

    return result;
}