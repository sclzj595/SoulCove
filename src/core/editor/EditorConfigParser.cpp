#include "core/editor/EditorConfigParser.h"
#include "Logger.hpp"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

// ========== 公共入口：从文件路径向上查找并合并 .editorconfig 配置 ==========

EditorConfig EditorConfigParser::parse(const QString& filePath)
{
    EditorConfig result;
    if (filePath.isEmpty()) return result;

    QFileInfo fi(filePath);
    QString targetFileName = fi.fileName();
    if (targetFileName.isEmpty()) return result;

    // 从文件所在目录向上查找 .editorconfig
    QDir dir = fi.absoluteDir();
    int maxDepth = 32;  // 防止无限向上查找（如 Windows 根目录循环）
    while (maxDepth-- > 0 && dir.exists()) {
        QString configPath = dir.absoluteFilePath(QStringLiteral(".editorconfig"));
        QFile f(configPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            bool isRoot = false;
            EditorConfig cfg = parseFile(configPath, targetFileName, isRoot);

            // 合并：最近的 .editorconfig 优先级最高（仅填充 result 中未设置的字段）
            // 按规范：上层（更远）的 .editorconfig 不应覆盖下层（更近）已设置的属性
            if (result.indentStyle.isEmpty() && !cfg.indentStyle.isEmpty())
                result.indentStyle = cfg.indentStyle;
            if (result.indentSize < 0 && cfg.indentSize > 0)
                result.indentSize = cfg.indentSize;
            if (result.tabWidth < 0 && cfg.tabWidth > 0)
                result.tabWidth = cfg.tabWidth;
            if (result.endOfLine.isEmpty() && !cfg.endOfLine.isEmpty())
                result.endOfLine = cfg.endOfLine;
            if (result.charset.isEmpty() && !cfg.charset.isEmpty())
                result.charset = cfg.charset;
            // bool 字段：仅当 result 未被下层显式设置时才继承（简化处理：下层有则用下层）
            // 由于 bool 无法区分"未设置"和"false"，这里采用：下层 isRoot 内已合并多 section，
            // 上层仅在 result 完全未触发这些字段时才生效
            if (!result.insertFinalNewline && cfg.insertFinalNewline)
                result.insertFinalNewline = cfg.insertFinalNewline;
            if (!result.trimTrailingWhitespace && cfg.trimTrailingWhitespace)
                result.trimTrailingWhitespace = cfg.trimTrailingWhitespace;
            if (result.maxLineLength < 0 && cfg.maxLineLength > 0)
                result.maxLineLength = cfg.maxLineLength;

            // root=true 时停止向上查找
            if (isRoot) break;
        }
        // 移动到父目录
        if (!dir.cdUp()) break;
    }

    return result;
}

// ========== 解析单个 .editorconfig 文件 ==========

EditorConfig EditorConfigParser::parseFile(const QString& configPath,
                                           const QString& targetFileName,
                                           bool& isRoot)
{
    EditorConfig result;
    isRoot = false;

    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return result;

    // 简易 INI 解析：section 行 [glob]，key=value 行
    // 不支持多行值、不支持注释中的特殊字符
    QTextStream in(&f);
    QString currentGlob;
    bool currentSectionMatches = false;  // 当前 section 是否匹配目标文件

    // 缓存当前 section 的属性，匹配成功后合并到 result
    // （多个匹配 section 按出现顺序合并，后者覆盖前者）
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) ||
            line.startsWith(QLatin1Char(';'))) {
            continue;  // 空行或注释
        }

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            // 新 section 开始
            currentGlob = line.mid(1, line.length() - 2).trimmed();
            QRegularExpression re = globToRegex(currentGlob);
            currentSectionMatches = re.match(targetFileName).hasMatch();
            continue;
        }

        // key=value 行
        int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        QString key = line.left(eq).trimmed().toLower();
        QString value = normalizeValue(line.mid(eq + 1));

        // root 是全局属性（不属于任何 section）
        if (key == QStringLiteral("root")) {
            isRoot = (value == QStringLiteral("true"));
            continue;
        }

        // 仅当前 section 匹配时才记录属性
        if (!currentSectionMatches) continue;

        if (key == QStringLiteral("indent_style")) {
            result.indentStyle = value;
        } else if (key == QStringLiteral("indent_size")) {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok && n > 0) result.indentSize = n;
            else if (value == QStringLiteral("tab")) result.indentSize = -2;  // 特殊值：跟随 tab_width
        } else if (key == QStringLiteral("tab_width")) {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok && n > 0) result.tabWidth = n;
        } else if (key == QStringLiteral("end_of_line")) {
            result.endOfLine = value;
        } else if (key == QStringLiteral("charset")) {
            result.charset = value;
        } else if (key == QStringLiteral("insert_final_newline")) {
            result.insertFinalNewline = (value == QStringLiteral("true"));
        } else if (key == QStringLiteral("trim_trailing_whitespace")) {
            result.trimTrailingWhitespace = (value == QStringLiteral("true"));
        } else if (key == QStringLiteral("max_line_length")) {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok && n > 0) result.maxLineLength = n;
        }
    }

    // indent_size=tab 时回退到 tab_width（规范要求）
    if (result.indentSize == -2) {
        result.indentSize = (result.tabWidth > 0) ? result.tabWidth : -1;
    }
    // tab_width 未设置时回退到 indent_size（规范要求）
    if (result.tabWidth < 0 && result.indentSize > 0) {
        result.tabWidth = result.indentSize;
    }

    return result;
}

// ========== editorconfig glob → QRegularExpression ==========

QRegularExpression EditorConfigParser::globToRegex(const QString& glob)
{
    // editorconfig glob 语法（简化版）：
    //   *      → [^/]*             （不匹配路径分隔符）
    //   **     → .*                （匹配任意字符含 /）
    //   ?      → [^/]              （单个字符，不匹配 /）
    //   {a,b}  → (a|b)             （枚举）
    //   [abc]  → [abc]             （字符集，原样保留）
    //   [!abc] → [^abc]            （否定字符集）
    // 其他特殊字符: 转义
    QString pattern = QStringLiteral("^");
    int i = 0;
    while (i < glob.size()) {
        QChar c = glob[i];
        if (c == QLatin1Char('*')) {
            if (i + 1 < glob.size() && glob[i + 1] == QLatin1Char('*')) {
                pattern += QStringLiteral(".*");
                i += 2;
            } else {
                pattern += QStringLiteral("[^/]*");
                i += 1;
            }
        } else if (c == QLatin1Char('?')) {
            pattern += QStringLiteral("[^/]");
            i += 1;
        } else if (c == QLatin1Char('{')) {
            // 找到匹配的 }
            int end = glob.indexOf(QLatin1Char('}'), i + 1);
            if (end > i) {
                QString content = glob.mid(i + 1, end - i - 1);
                // 用 | 替换 ,
                content.replace(QLatin1Char(','), QLatin1Char('|'));
                pattern += QStringLiteral("(?:") + content + QStringLiteral(")");
                i = end + 1;
            } else {
                pattern += QStringLiteral("\\{");
                i += 1;
            }
        } else if (c == QLatin1Char('[')) {
            // 字符集：找到匹配的 ]
            int end = glob.indexOf(QLatin1Char(']'), i + 1);
            if (end > i) {
                QString content = glob.mid(i + 1, end - i - 1);
                if (content.startsWith(QLatin1Char('!'))) {
                    pattern += QStringLiteral("[^") + content.mid(1) + QStringLiteral("]");
                } else {
                    pattern += QStringLiteral("[") + content + QStringLiteral("]");
                }
                i = end + 1;
            } else {
                pattern += QStringLiteral("\\[");
                i += 1;
            }
        } else {
            // 转义正则特殊字符
            if (QStringLiteral(".+()|^$\\").indexOf(c) >= 0) {
                pattern += QLatin1Char('\\');
            }
            pattern += c;
            i += 1;
        }
    }
    pattern += QStringLiteral("$");
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

// ========== 值标准化（去空白、小写）==========

QString EditorConfigParser::normalizeValue(const QString& value)
{
    return value.trimmed().toLower();
}
