#include "core/editor/DoxygenGenerator.h"

#include <QTextCursor>
#include <QTextBlock>
#include <QRegularExpression>
#include <QStringList>

// ========== 公开入口 ==========

void DoxygenGenerator::insertComment(QTextCursor cursor)
{
    QTextBlock startBlock = cursor.block();
    const QString sig = detectFunctionSignature(startBlock);
    if (sig.isEmpty()) return;

    const QStringList parts = sig.split('|');
    if (parts.size() < 3) return;
    const QString& lang = parts[0];
    const QString& funcName = parts[1];
    const QString  rawParams = parts[2];

    // 提取当前函数定义行的缩进
    const QString indent = extractIndent(startBlock);

    // 解析参数列表
    const QStringList paramList = parseParams(rawParams);

    // 生成注释文本
    QString comment;
    if (lang == QStringLiteral("py")) {
        comment = generatePythonDocstring(indent, paramList);
    } else {
        comment = generateCppDoxygen(indent, funcName, paramList);
    }

    // 定位到函数定义行开头，上方插入注释
    QTextBlock funcBlock = startBlock;
    for (int i = 0; i < 60 && funcBlock.isValid(); ++i) {
        if (funcBlock.text().contains(funcName))
            break;
        funcBlock = funcBlock.previous();
    }

    QTextCursor insertCur(funcBlock);
    insertCur.movePosition(QTextCursor::StartOfBlock);
    insertCur.insertText(comment + QStringLiteral("\n"));
}

// ========== 私有实现 ==========

QString DoxygenGenerator::detectFunctionSignature(const QTextBlock& startBlock)
{
    QTextBlock block = startBlock;
    // 向上搜索最多 60 行，找最近的函数定义
    for (int i = 0; i < 60 && block.isValid(); ++i) {
        const QString line = block.text().trimmed();
        if (line.isEmpty()) { block = block.previous(); continue; }

        // Python: def funcName(params):
        static const QRegularExpression pyRe(
            QStringLiteral(R"(^\s*def\s+(\w+)\s*\(\s*([^)]*)\s*\))"));
        QRegularExpressionMatch m = pyRe.match(line);
        if (m.hasMatch()) {
            return QStringLiteral("py|") + m.captured(1) + QStringLiteral("|") + m.captured(2);
        }

        // C/C++ 函数: type name(params)
        static const QRegularExpression cppRe(
            QStringLiteral(R"(^\s*(?:[\w:*&<>,\s]+?)\s+(\w+)\s*\(\s*([^)]*)\s*\))"));
        m = cppRe.match(line);
        if (m.hasMatch()) {
            const QString name = m.captured(1);
            // 排除常见控制流关键字
            static const QStringList kw = { QStringLiteral("if"),QStringLiteral("else"),
                QStringLiteral("for"),QStringLiteral("while"),QStringLiteral("switch"),
                QStringLiteral("return"),QStringLiteral("catch"),QStringLiteral("do") };
            if (!kw.contains(name))
                return QStringLiteral("cpp|") + name + QStringLiteral("|") + m.captured(2);
        }

        block = block.previous();
    }
    return QString();
}

QString DoxygenGenerator::extractIndent(const QTextBlock& startBlock)
{
    QTextBlock block = startBlock;
    for (int i = 0; i < 60 && block.isValid(); ++i) {
        const QString line = block.text();
        if (!line.trimmed().isEmpty()) {
            static const QRegularExpression indentRe(QStringLiteral(R"(^(\s*))"));
            auto m = indentRe.match(line);
            if (m.hasMatch()) return m.captured(1);
        }
        block = block.previous();
    }
    return QString();
}

QStringList DoxygenGenerator::parseParams(const QString& rawParams)
{
    QStringList paramList;
    if (rawParams.isEmpty()) return paramList;

    const QStringList rawList = rawParams.split(',');
    for (const auto& p : rawList) {
        QString trimmed = p.trimmed();
        if (trimmed.isEmpty()) continue;
        // 提取参数名 (C++ 去类型前缀, Python 去默认值)
        static const QRegularExpression nameRe(QStringLiteral(R"(\b(\w+)\s*$)"));
        auto nameM = nameRe.match(trimmed);
        if (nameM.hasMatch()) {
            QString n = nameM.captured(1);
            if (n != QStringLiteral("self") && n != QStringLiteral("cls"))
                paramList.append(n);
        }
    }
    return paramList;
}

QString DoxygenGenerator::generatePythonDocstring(const QString& indent,
                                                  const QStringList& params)
{
    QString comment;
    comment += indent + QStringLiteral("\"\"\"\n");
    comment += indent + QStringLiteral("\n");
    if (!params.isEmpty()) {
        comment += indent + QStringLiteral("Args:\n");
        for (const auto& p : params)
            comment += indent + QStringLiteral("    ") + p + QStringLiteral(": \n");
    }
    comment += indent + QStringLiteral("Returns:\n");
    comment += indent + QStringLiteral("    \n");
    comment += indent + QStringLiteral("\"\"\"");
    return comment;
}

QString DoxygenGenerator::generateCppDoxygen(const QString& indent,
                                             const QString& funcName,
                                             const QStringList& params)
{
    QString comment;
    comment += indent + QStringLiteral("/**\n");
    comment += indent + QStringLiteral(" * @brief \n");
    for (const auto& p : params)
        comment += indent + QStringLiteral(" * @param ") + p + QStringLiteral(" \n");
    // 非 void 函数且非构造/析构 → 加 @return
    if (!funcName.startsWith('~'))
        comment += indent + QStringLiteral(" * @return \n");
    comment += indent + QStringLiteral(" */");
    return comment;
}
