#include "core/editor/HeaderSymbolScanner.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include <QSet>

QList<QPair<QString, QString>> HeaderSymbolScanner::scanForExternalSymbols(
    const QString& sourceFilePath, const QString& sourceContent)
{
    QString suffix = QFileInfo(sourceFilePath).suffix().toLower();

    if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("c") ||
        suffix == QStringLiteral("cc") || suffix == QStringLiteral("cxx") ||
        suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp") ||
        suffix == QStringLiteral("hxx") || suffix == QStringLiteral("inl")) {
        return scanCppHeaders(sourceFilePath, sourceContent);
    }
    if (suffix == QStringLiteral("py") || suffix == QStringLiteral("pyw")) {
        return scanPythonModules(sourceFilePath, sourceContent);
    }
    if (suffix == QStringLiteral("js") || suffix == QStringLiteral("jsx") ||
        suffix == QStringLiteral("ts") || suffix == QStringLiteral("tsx") ||
        suffix == QStringLiteral("mjs")) {
        return scanJsModules(sourceFilePath, sourceContent);
    }
    return {};
}

QList<QPair<QString, QString>> HeaderSymbolScanner::scanCppHeaders(
    const QString& sourceFilePath, const QString& sourceContent)
{
    QList<QPair<QString, QString>> allSymbols;
    QFileInfo sourceInfo(sourceFilePath);
    QDir sourceDir = sourceInfo.absoluteDir();

    // H2: 同时解析引号 #include "..." 和尖括号 #include <...>（尖括号仅当文件在项目内可找到时生效）
    QRegularExpression includeRegex(QStringLiteral("#include\\s*[\"<]([^\">]+)[\">]"));
    auto it = includeRegex.globalMatch(sourceContent);
    QSet<QString> visited;  // 环检测：已扫描的头文件路径

    while (it.hasNext()) {
        QString includePath = it.next().captured(1);
        // H2: 使用增强的路径解析（多候选位置查找）
        QString headerPath = resolveHeaderPath(sourceDir, includePath);
        if (headerPath.isEmpty()) continue;

        // 递归扫描（含传递性 #include，深度限制 3 层）
        scanCppHeaderRecursive(headerPath, allSymbols, visited, 0);
    }
    return allSymbols;
}

QString HeaderSymbolScanner::resolveHeaderPath(const QDir& sourceDir, const QString& includePath)
{
    // H2: 多候选位置查找头文件，解决跨目录 #include 解析失败问题
    //
    // 候选位置（按优先级）：
    // 1. 源文件同目录 + includePath
    // 2. 源文件同目录 + ../include/ + includePath
    // 3. 从源目录向上遍历祖先目录，尝试 ancestor/includePath（最多 5 层）
    //    覆盖 "src/ui/shell/Widget.cpp" → "src/core/editor/Foo.h" 的场景
    //    （includePath 为 "core/editor/Foo.h"，从 src/ 开始能找到）

    // 候选 1: 源文件同目录
    QString candidate = sourceDir.filePath(includePath);
    if (QFile::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();

    // 候选 2: ../include/
    candidate = sourceDir.filePath(QStringLiteral("../include/") + includePath);
    if (QFile::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();

    // 候选 3: 向上遍历祖先目录（最多 5 层）
    QDir ancestorDir = sourceDir;
    for (int i = 0; i < 5; ++i) {
        if (!ancestorDir.cdUp()) break;
        candidate = ancestorDir.filePath(includePath);
        if (QFile::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();
    }

    return QString();  // 未找到
}

void HeaderSymbolScanner::scanCppHeaderRecursive(const QString& headerPath,
                                                  QList<QPair<QString, QString>>& allSymbols,
                                                  QSet<QString>& visited, int depth)
{
    // H2: 递归扫描头文件 — 提取当前头文件符号 + 传递性扫描其 #include
    // 深度限制 3 层，防止无限递归；visited 集合防止循环引用

    QString absPath = QFileInfo(headerPath).absoluteFilePath();
    if (visited.contains(absPath)) return;  // 环检测
    if (depth > 3) return;                  // 深度限制
    visited.insert(absPath);

    QString headerContent = readFileContent(absPath);
    if (headerContent.isEmpty()) return;

    // 提取当前头文件的符号
    auto symbols = extractCppSymbols(headerContent);
    for (const auto& sym : symbols) {
        if (!allSymbols.contains(sym)) allSymbols.append(sym);
    }

    // 传递性扫描：解析当前头文件中的 #include "..." / <...>
    QFileInfo headerInfo(absPath);
    QDir headerDir = headerInfo.absoluteDir();
    QRegularExpression includeRegex(QStringLiteral("#include\\s*[\"<]([^\">]+)[\">]"));
    auto it = includeRegex.globalMatch(headerContent);
    while (it.hasNext()) {
        QString nestedInclude = it.next().captured(1);
        QString nestedPath = resolveHeaderPath(headerDir, nestedInclude);
        if (!nestedPath.isEmpty()) {
            scanCppHeaderRecursive(nestedPath, allSymbols, visited, depth + 1);
        }
    }
}

QList<QPair<QString, QString>> HeaderSymbolScanner::extractCppSymbols(const QString& content)
{
    QList<QPair<QString, QString>> symbols;

    // H2: 先剥离注释和字符串，避免误提取注释中的符号名
    // 简单处理：移除行注释 //... 和块注释 /*...*/ 和字符串 "..."
    QString cleanContent = content;
    cleanContent.replace(QRegularExpression(QStringLiteral("//[^\n]*")), QString());
    cleanContent.replace(QRegularExpression(QStringLiteral("/\\*[\\s\\S]*?\\*/")), QString());
    cleanContent.replace(QRegularExpression(QStringLiteral("\"[^\"]*\"")), QStringLiteral("\"\""));

    QRegularExpression classRegex(QStringLiteral("\\b(?:class|struct)\\s+(\\w+)"));
    auto classIt = classRegex.globalMatch(cleanContent);
    while (classIt.hasNext())
        symbols.append({classIt.next().captured(1), QStringLiteral("typeDef")});

    QRegularExpression enumRegex(QStringLiteral("\\benum\\s+(?:class\\s+)?(\\w+)"));
    auto enumIt = enumRegex.globalMatch(cleanContent);
    while (enumIt.hasNext())
        symbols.append({enumIt.next().captured(1), QStringLiteral("typeDef")});

    QRegularExpression typedefRegex(QStringLiteral("\\btypedef\\s+.*?(\\w+)\\s*;"));
    auto tdIt = typedefRegex.globalMatch(cleanContent);
    while (tdIt.hasNext()) {
        QString name = tdIt.next().captured(1);
        if (name != QStringLiteral("void") && name.length() > 1)
            symbols.append({name, QStringLiteral("typeDef")});
    }

    QRegularExpression usingRegex(QStringLiteral("\\busing\\s+(\\w+)\\s*="));
    auto useIt = usingRegex.globalMatch(cleanContent);
    while (useIt.hasNext())
        symbols.append({useIt.next().captured(1), QStringLiteral("typeDef")});

    QRegularExpression defineRegex(QStringLiteral("#define\\s+(\\w+)"));
    auto defIt = defineRegex.globalMatch(cleanContent);
    while (defIt.hasNext()) {
        QString name = defIt.next().captured(1);
        if (name.length() > 2)
            symbols.append({name, QStringLiteral("constant")});
    }

    QRegularExpression funcRegex(QStringLiteral("\\b(?:[a-zA-Z_]\\w*\\s+)+([a-zA-Z_]\\w*)\\s*\\("));
    auto funcIt = funcRegex.globalMatch(cleanContent);
    static const QSet<QString> controlKws = {
        QStringLiteral("if"), QStringLiteral("for"), QStringLiteral("while"),
        QStringLiteral("switch"), QStringLiteral("return"), QStringLiteral("sizeof"),
        QStringLiteral("static_cast"), QStringLiteral("dynamic_cast"),
        QStringLiteral("const_cast"), QStringLiteral("reinterpret_cast")
    };
    while (funcIt.hasNext()) {
        QString name = funcIt.next().captured(1);
        if (!controlKws.contains(name))
            symbols.append({name, QStringLiteral("funcDecl")});
    }

    // H2: 提取全局变量 / extern 声明 / 单例实例
    // 匹配: extern Type varName;  或  static Type varName;  或  Type varName;
    // 排除: 函数声明（有括号）、类/结构体/枚举定义（已上面处理）
    // 例: extern MySingleton g_singleton; → g_singleton 作为 constant
    //     static MySingleton* s_instance; → s_instance 作为 constant
    QRegularExpression globalVarRegex(QStringLiteral(
        "^\\s*(?:extern\\s+|static\\s+)?(?:const\\s+|constexpr\\s+)?"
        "[A-Za-z_]\\w*(?:\\s*[*&])*\\s+([A-Za-z_]\\w*)\\s*(?:=|;)"
    ), QRegularExpression::MultilineOption);
    auto gvIt = globalVarRegex.globalMatch(cleanContent);
    static const QSet<QString> typeKws = {
        QStringLiteral("class"), QStringLiteral("struct"), QStringLiteral("enum"),
        QStringLiteral("union"), QStringLiteral("typedef"), QStringLiteral("using"),
        QStringLiteral("namespace"), QStringLiteral("return"), QStringLiteral("if"),
        QStringLiteral("for"), QStringLiteral("while"), QStringLiteral("switch"),
        QStringLiteral("case"), QStringLiteral("break"), QStringLiteral("continue"),
        QStringLiteral("default"), QStringLiteral("throw"), QStringLiteral("try"),
        QStringLiteral("catch"), QStringLiteral("new"), QStringLiteral("delete"),
        QStringLiteral("void"), QStringLiteral("int"), QStringLiteral("char"),
        QStringLiteral("bool"), QStringLiteral("float"), QStringLiteral("double"),
        QStringLiteral("long"), QStringLiteral("short"), QStringLiteral("unsigned"),
        QStringLiteral("signed"), QStringLiteral("auto"), QStringLiteral("const"),
        QStringLiteral("static"), QStringLiteral("extern"), QStringLiteral("virtual"),
        QStringLiteral("override"), QStringLiteral("inline"), QStringLiteral("explicit"),
        QStringLiteral("friend"), QStringLiteral("operator"), QStringLiteral("public"),
        QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("template"),
        QStringLiteral("typename"), QStringLiteral("this"), QStringLiteral("nullptr"),
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("noexcept"),
        QStringLiteral("constexpr"), QStringLiteral("mutable"), QStringLiteral("volatile")
    };
    while (gvIt.hasNext()) {
        QString name = gvIt.next().captured(1);
        if (name.length() < 2) continue;
        if (typeKws.contains(name)) continue;
        // 全局变量/单例实例作为 constant 角色高亮
        if (!symbols.contains({name, QStringLiteral("constant")}) &&
            !symbols.contains({name, QStringLiteral("typeDef")})) {
            symbols.append({name, QStringLiteral("constant")});
        }
    }

    return symbols;
}

QList<QPair<QString, QString>> HeaderSymbolScanner::scanPythonModules(
    const QString& sourceFilePath, const QString& sourceContent)
{
    QList<QPair<QString, QString>> allSymbols;
    QFileInfo sourceInfo(sourceFilePath);
    QDir sourceDir = sourceInfo.absoluteDir();

    QRegularExpression importRegex(QStringLiteral(
        "(?:from\\s+(\\w+)\\s+import\\s+(\\w+))|(?:import\\s+(\\w+))"));
    auto it = importRegex.globalMatch(sourceContent);
    while (it.hasNext()) {
        auto match = it.next();
        QString moduleName = match.captured(1).isEmpty() ? match.captured(3) : match.captured(1);
        QString specificName = match.captured(2);

        if (!specificName.isEmpty())
            allSymbols.append({specificName, QStringLiteral("typeDef")});

        if (!moduleName.isEmpty()) {
            QString modulePath = sourceDir.filePath(moduleName + QStringLiteral(".py"));
            if (QFile::exists(modulePath)) {
                auto symbols = extractPythonSymbols(readFileContent(modulePath));
                for (const auto& sym : symbols) {
                    if (!allSymbols.contains(sym)) allSymbols.append(sym);
                }
            }
        }
    }
    return allSymbols;
}

QList<QPair<QString, QString>> HeaderSymbolScanner::extractPythonSymbols(const QString& content)
{
    QList<QPair<QString, QString>> symbols;
    QRegularExpression classRegex(QStringLiteral("^class\\s+(\\w+)"));
    auto cIt = classRegex.globalMatch(content);
    while (cIt.hasNext())
        symbols.append({cIt.next().captured(1), QStringLiteral("typeDef")});

    QRegularExpression funcRegex(QStringLiteral("^def\\s+(\\w+)"));
    auto fIt = funcRegex.globalMatch(content);
    while (fIt.hasNext())
        symbols.append({fIt.next().captured(1), QStringLiteral("funcDecl")});
    return symbols;
}

QList<QPair<QString, QString>> HeaderSymbolScanner::scanJsModules(
    const QString& sourceFilePath, const QString& sourceContent)
{
    QList<QPair<QString, QString>> allSymbols;
    QFileInfo sourceInfo(sourceFilePath);
    QDir sourceDir = sourceInfo.absoluteDir();

    QStringList modulePaths;
    QRegularExpression requireRegex(QStringLiteral("require\\s*\\(\\s*['\"]([^'\"]+)['\"]\\s*\\)"));
    auto rIt = requireRegex.globalMatch(sourceContent);
    while (rIt.hasNext()) modulePaths.append(rIt.next().captured(1));

    QRegularExpression importRegex(QStringLiteral("import\\s+.*?from\\s+['\"]([^'\"]+)['\"]"));
    auto iIt = importRegex.globalMatch(sourceContent);
    while (iIt.hasNext()) modulePaths.append(iIt.next().captured(1));

    for (const QString& modPath : modulePaths) {
        if (!modPath.startsWith('.') && !modPath.startsWith('/')) continue;
        QString fullPath = sourceDir.filePath(modPath);
        if (!QFile::exists(fullPath)) {
            for (const QString& ext : {".js", ".ts", ".jsx", ".tsx"}) {
                if (QFile::exists(fullPath + ext)) { fullPath += ext; break; }
            }
        }
        if (!QFile::exists(fullPath)) {
            QString idx = fullPath + QStringLiteral("/index.js");
            if (QFile::exists(idx)) fullPath = idx;
        }
        if (!QFile::exists(fullPath)) continue;

        auto symbols = extractJsSymbols(readFileContent(fullPath));
        for (const auto& sym : symbols) {
            if (!allSymbols.contains(sym)) allSymbols.append(sym);
        }
    }
    return allSymbols;
}

QList<QPair<QString, QString>> HeaderSymbolScanner::extractJsSymbols(const QString& content)
{
    QList<QPair<QString, QString>> symbols;
    QRegularExpression funcRegex(QStringLiteral("\\bfunction\\s+(\\w+)"));
    auto fIt = funcRegex.globalMatch(content);
    while (fIt.hasNext())
        symbols.append({fIt.next().captured(1), QStringLiteral("funcDecl")});

    QRegularExpression classRegex(QStringLiteral("\\bclass\\s+(\\w+)"));
    auto cIt = classRegex.globalMatch(content);
    while (cIt.hasNext())
        symbols.append({cIt.next().captured(1), QStringLiteral("typeDef")});

    QRegularExpression constRegex(QStringLiteral("\\b(?:export\\s+)?const\\s+(\\w+)"));
    auto kIt = constRegex.globalMatch(content);
    while (kIt.hasNext()) {
        QString name = kIt.next().captured(1);
        if (name.length() > 1)
            symbols.append({name, QStringLiteral("constant")});
    }
    return symbols;
}

QString HeaderSymbolScanner::readFileContent(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    return content;
}
