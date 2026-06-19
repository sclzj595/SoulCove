#ifndef HTMLCSSRESOLVER_H
#define HTMLCSSRESOLVER_H

#include <QString>
#include <QList>
#include <QPair>

/// @brief HTML CSS 解析器 — 解析 <link> 标签，查找本地 CSS 文件并内联注入
///
/// 设计目的：
/// QTextBrowser 不支持外部 CSS 文件引用（<link rel="stylesheet" href="...">），
/// 需要将外部 CSS 内容内联为 <style> 标签后才能正确渲染。
///
/// 查找策略（按优先级）：
/// 1. 相对 HTML 文件所在目录解析相对路径（./css/a.css、../b.css）
/// 2. 在工作目录下递归查找同名文件
/// 3. 找不到则保留原 <link> 标签（QTextBrowser 会忽略它）
///
/// 仅处理本地文件（相对路径或纯文件名），跳过 http/https/data: 等远程 URL
class HtmlCssResolver
{
public:
    /// @brief 解析 HTML 中的 <link rel="stylesheet"> 标签，将本地 CSS 内联为 <style>
    /// @param htmlContent 原始 HTML 内容
    /// @param htmlFilePath HTML 文件完整路径（用于解析相对路径）
    /// @param workDir 工作目录（用于递归查找 CSS 文件）
    /// @return 处理后的 HTML（外部 CSS 已内联注入）
    static QString resolveExternalCss(const QString& htmlContent,
                                      const QString& htmlFilePath,
                                      const QString& workDir);

    /// @brief 在工作目录下递归查找指定文件名的文件路径
    /// @param workDir 工作目录
    /// @param fileName 目标文件名（如 "style.css"）
    /// @return 找到的完整路径，未找到返回空字符串
    static QString findFileInWorkDir(const QString& workDir, const QString& fileName);

private:
    /// @brief 从 <link> 标签中提取 href 属性值
    static QString extractHref(const QString& linkTag);

    /// @brief 判断 href 是否为本地文件（非 http/https/data:）
    static bool isLocalHref(const QString& href);

    /// @brief 安全读取文件内容
    static QString readFileContent(const QString& path);
};

#endif // HTMLCSSRESOLVER_H
