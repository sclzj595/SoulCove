#include "HeaderSymbolScanner.h"

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

    QRegularExpression includeRegex(QStringLiteral("#include\\s*\"([^\"]+)\""));
    auto it = includeRegex.globalMatch(sourceContent);
    while (it.hasNext()) {
        QString includePath = it.next().captured(1);
        QString headerPath = sourceDir.filePath(includePath);
        if (!QFile::exists(headerPath))
            headerPath = sourceDir.filePath(QStringLiteral("../include/") + includePath);
        if (!QFile::exists(headerPath)) continue;

        QString headerContent = readFileContent(headerPath);
        if (headerContent.isEmpty()) continue;

        auto symbols = extractCppSymbols(headerContent);
        for (const auto& sym : symbols) {
            if (!allSymbols.contains(sym)) allSymbols.append(sym);
        }
    }
    return allSymbols;
}

QList<QPair<QString, QString>> HeaderSymbolScanner::extractCppSymbols(const QString& content)
{
    QList<QPair<QString, QString>> symbols;

    QRegularExpression classRegex(QStringLiteral("\\b(?:class|struct)\\s+(\\w+)"));
    auto classIt = classRegex.globalMatch(content);
    while (classIt.hasNext())
        symbols.append({classIt.next().captured(1), QStringLiteral("typeDef")});

    QRegularExpression enumRegex(QStringLiteral("\\benum\\s+(?:class\\s+)?(\\w+)"));
    auto enumIt = enumRegex.globalMatch(content);
    while (enumIt.hasNext())
        symbols.append({enumIt.next().captured(1), QStringLiteral("typeDef")});

    QRegularExpression typedefRegex(QStringLiteral("\\btypedef\\s+.*?(\\w+)\\s*;"));
    auto tdIt = typedefRegex.globalMatch(content);
    while (tdIt.hasNext()) {
        QString name = tdIt.next().captured(1);
        if (name != QStringLiteral("void") && name.length() > 1)
            symbols.append({name, QStringLiteral("typeDef")});
    }

    QRegularExpression usingRegex(QStringLiteral("\\busing\\s+(\\w+)\\s*="));
    auto useIt = usingRegex.globalMatch(content);
    while (useIt.hasNext())
        symbols.append({useIt.next().captured(1), QStringLiteral("typeDef")});

    QRegularExpression defineRegex(QStringLiteral("#define\\s+(\\w+)"));
    auto defIt = defineRegex.globalMatch(content);
    while (defIt.hasNext()) {
        QString name = defIt.next().captured(1);
        if (name.length() > 2)
            symbols.append({name, QStringLiteral("constant")});
    }

    QRegularExpression funcRegex(QStringLiteral("\\b(?:[a-zA-Z_]\\w*\\s+)+([a-zA-Z_]\\w*)\\s*\\("));
    auto funcIt = funcRegex.globalMatch(content);
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
