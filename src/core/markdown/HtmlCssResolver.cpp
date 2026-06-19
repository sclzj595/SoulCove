#include "core/markdown/HtmlCssResolver.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTextStream>
#include <Logger.hpp>

QString HtmlCssResolver::resolveExternalCss(const QString& htmlContent,
                                            const QString& htmlFilePath,
                                            const QString& workDir)
{
    QString result = htmlContent;
    QFileInfo htmlInfo(htmlFilePath);
    QDir htmlDir = htmlInfo.absoluteDir();

    // 匹配 <link rel="stylesheet" ...> 标签（大小写不敏感，属性顺序容错）
    QRegularExpression linkRegex(
        QStringLiteral("<link\\s[^>]*?rel\\s*=\\s*[\"']stylesheet[\"'][^>]*?>"),
        QRegularExpression::CaseInsensitiveOption);

    // 收集所有匹配，从后往前替换避免索引偏移
    QList<QRegularExpressionMatch> matches;
    auto it = linkRegex.globalMatch(result);
    while (it.hasNext()) {
        matches.append(it.next());
    }

    for (int i = matches.size() - 1; i >= 0; --i) {
        const QRegularExpressionMatch& match = matches[i];
        QString linkTag = match.captured(0);
        QString href = extractHref(linkTag);

        if (href.isEmpty() || !isLocalHref(href)) continue;

        // 策略1：相对 HTML 文件目录解析相对路径
        QString cssPath = htmlDir.filePath(href);

        // 策略2：在工作目录下递归查找同名文件
        if (!QFile::exists(cssPath) && !workDir.isEmpty()) {
            cssPath = findFileInWorkDir(workDir, QFileInfo(href).fileName());
        }

        if (cssPath.isEmpty() || !QFile::exists(cssPath)) {
            LOG_DEBUG("[HtmlCssResolver] CSS 文件未找到: " << href.toStdString());
            continue;
        }

        QString cssContent = readFileContent(cssPath);
        if (cssContent.isEmpty()) continue;

        // 将 <link ...> 替换为 <style>...</style>
        QString styleTag = QStringLiteral("<style>\n") + cssContent +
                           QStringLiteral("\n</style>");
        result.replace(match.capturedStart(), match.capturedLength(), styleTag);
        LOG_DEBUG("[HtmlCssResolver] 内联注入 CSS: " << href.toStdString()
                  << " -> " << cssPath.toStdString());
    }

    return result;
}

QString HtmlCssResolver::extractHref(const QString& linkTag)
{
    QRegularExpression hrefRegex(
        QStringLiteral("href\\s*=\\s*[\"']([^\"']+)[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = hrefRegex.match(linkTag);
    return match.hasMatch() ? match.captured(1) : QString();
}

bool HtmlCssResolver::isLocalHref(const QString& href)
{
    if (href.isEmpty()) return false;
    // 跳过远程 URL：http://, https://, data:, //, ftp://
    if (href.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
        href.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
        href.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive) ||
        href.startsWith(QStringLiteral("//")) ||
        href.startsWith(QStringLiteral("ftp://"), Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

QString HtmlCssResolver::findFileInWorkDir(const QString& workDir, const QString& fileName)
{
    QDir dir(workDir);
    if (!dir.exists()) return QString();

    // 递归查找同名文件
    QDirIterator it(workDir, QStringList() << fileName,
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        return it.next();  // 返回第一个匹配
    }
    return QString();
}

QString HtmlCssResolver::readFileContent(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    return content;
}
