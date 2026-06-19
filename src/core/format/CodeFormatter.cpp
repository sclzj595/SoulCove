#include "core/format/CodeFormatter.h"
#include "Logger.hpp"

#include <QProcess>
#include <QDebug>

// ========== 单例实现 ==========

CodeFormatter::CodeFormatter()
    : QObject(nullptr)
{
}

CodeFormatter& CodeFormatter::instance()
{
    static CodeFormatter inst;
    return inst;
}

// ========== clang-format 检测 ==========

bool CodeFormatter::isClangFormatAvailable() const
{
    // 已检测过则直接返回缓存结果
    if (m_clangFormatChecked)
        return m_hasClangFormat;

    // const_cast 用于延迟初始化单例的 mutable 状态
    auto* self = const_cast<CodeFormatter*>(this);

    QProcess process;
    process.start(QStringLiteral("clang-format"), {QStringLiteral("--version")});
    if (process.waitForFinished(3000)) {
        self->m_hasClangFormat = (process.exitStatus() == QProcess::NormalExit &&
                                   process.exitCode() == 0);
        LOG_DEBUG_S("CodeFormatter", "isClangFormatAvailable", "clang-format 可用:" << self->m_hasClangFormat);
    } else {
        self->m_hasClangFormat = false;
        LOG_DEBUG_S("CodeFormatter", "isClangFormatAvailable", "clang-format 不可用（超时或未安装）");
    }

    self->m_clangFormatChecked = true;
    return m_hasClangFormat;
}

// ========== 格式化入口 ==========

QString CodeFormatter::format(const QString& code, const QString& filePath)
{
    if (code.isEmpty())
        return code;

    // 优先使用 clang-format
    if (isClangFormatAvailable()) {
        QProcess process;
        QStringList args;
        args << QStringLiteral("-style=file");
        if (!filePath.isEmpty()) {
            // 使用文件后缀推断语言类型
            QString assumeFile = filePath;
            // 如果路径不含后缀，使用 .cpp 作为默认
            if (!assumeFile.contains(QLatin1Char('.')))
                assumeFile += QStringLiteral(".cpp");
            args << QStringLiteral("--assume-filename=") + assumeFile;
        }
        process.start(QStringLiteral("clang-format"), args);

        if (process.write(code.toUtf8()) == -1) {
            LOG_DEBUG_S("CodeFormatter", "format", "写入 clang-format 失败");
            return formatBuiltin(code);
        }
        process.closeWriteChannel();

        if (process.waitForFinished(5000) &&
            process.exitStatus() == QProcess::NormalExit &&
            process.exitCode() == 0) {
            QString result = QString::fromUtf8(process.readAllStandardOutput());
            LOG_DEBUG_S("CodeFormatter", "format", "clang-format 格式化成功，长度:"
                     << result.length());
            return result;
        } else {
            LOG_DEBUG_S("CodeFormatter", "format", "clang-format 格式化失败，回退到内置格式化"
                     << process.readAllStandardError());
        }
    }

    // 回退到内置格式化
    return formatBuiltin(code);
}

// ========== 内置简单格式化 ==========

QString CodeFormatter::formatBuiltin(const QString& code)
{
    QStringList lines = code.split(QLatin1Char('\n'));
    int indentLevel = 0;
    bool inBlockComment = false;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];

        // 处理块注释 /* ... */
        if (!inBlockComment) {
            int commentStart = line.indexOf(QStringLiteral("/*"));
            if (commentStart >= 0) {
                int commentEnd = line.indexOf(QStringLiteral("*/"), commentStart + 2);
                if (commentEnd < 0)
                    inBlockComment = true;
            }
        } else {
            int commentEnd = line.indexOf(QStringLiteral("*/"));
            if (commentEnd >= 0)
                inBlockComment = false;
            continue;  // 注释行不处理缩进
        }

        // 去除行尾空格
        line = line.trimmed();

        // 跳过空行和纯注释行（保留原缩进为0）
        if (line.isEmpty())
            continue;

        // 计算当前行的花括号变化
        int braceDelta = 0;
        bool lineHasOpenBrace = line.contains(QLatin1Char('{'));
        bool lineHasCloseBrace = line.contains(QLatin1Char('}'));

        // 如果该行以 } 开头（如 } else {），先减少缩进
        if (line.startsWith(QLatin1Char('}')))
            indentLevel = qMax(0, indentLevel - 1);

        // 应用缩进
        QString indentStr;
        if (m_indentStyle == IndentStyle::Tabs) {
            indentStr = QString(indentLevel, QLatin1Char('\t'));
        } else {
            indentStr = QString(indentLevel * m_indentSize, QLatin1Char(' '));
        }
        lines[i] = indentStr + line;

        // 行末有 { 则下一行增加缩进
        if (lineHasOpenBrace && !lineHasCloseBrace)
            ++indentLevel;
        // 如果该行有 } 且不以 { 开头（即闭合括号），减少缩进
        else if (lineHasCloseBrace && !line.startsWith(QLatin1Char('}')))
            indentLevel = qMax(0, indentLevel - 1);
    }

    // 确保文件末尾只有一个换行
    QString result = lines.join(QLatin1Char('\n'));
    while (result.endsWith(QLatin1Char('\n')) && result.size() > 1)
        result.chop(1);
    result += QLatin1Char('\n');

    return result;
}

// ========== 配置方法 ==========

void CodeFormatter::setIndentStyle(IndentStyle style)
{
    m_indentStyle = style;
}

void CodeFormatter::setIndentSize(int size)
{
    m_indentSize = qBound(1, size, 16);
}
