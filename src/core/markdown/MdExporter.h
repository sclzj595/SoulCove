#ifndef MDEXPORTER_H
#define MDEXPORTER_H

#include <QObject>
#include <QString>

/// @brief Markdown 文档导出器
///
/// 支持的导出格式：
/// - HTML: 独立HTML文件（内嵌CSS样式，可直接浏览器打开）
/// - PDF: 通过QPrinter+QTextDocument渲染生成PDF
///
/// 特性：
/// - 自动转换Markdown → HTML → PDF
/// - 可选包含目录导航
/// - A4页面布局，自适应页边距
/// - 导出进度回调
class MdExporter : public QObject
{
    Q_OBJECT

public:
    /// @brief 导出格式枚举
    enum class ExportFormat {
        HTML,   // HTML文件
        PDF     // PDF文件
    };

    /// @brief 导出选项
    struct ExportOptions {
        bool includeToc;           ///< 是否包含目录
        bool includeLineNumbers;  ///< 代码块是否显示行号
        QString title;            ///< 文档标题（用于HTML标题）
        QString author;           ///< 作者信息

        ExportOptions()
            : includeToc(true)
            , includeLineNumbers(false)
        {}
    };

    explicit MdExporter(QObject* parent = nullptr);

    /// @brief 设置Markdown解析器接口
    void setParser(class IMarkdownParser* parser);

    /// @brief 导出文档到文件
    /// @param markdown 原始Markdown文本
    /// @param filePath 输出文件路径（扩展名决定格式）
    /// @param options 导出选项
    /// @return 是否成功
    bool exportToFile(const QString& markdown,
                      const QString& filePath,
                      const ExportOptions& options = ExportOptions());

    /// @brief 导出为HTML字符串（不写入文件）
    QString toHtmlString(const QString& markdown,
                         const ExportOptions& options = ExportOptions());

    /// @brief P3-M02 子项4: 转换 Markdown 为富文本 HTML 字符串（用于剪贴板复制到 Word/邮件等）
    /// @param markdown 原始 Markdown 文本
    /// @return 带内联样式的完整 HTML 字符串（适合 QTextDocument/QClipboard 渲染）
    QString copyAsRichText(const QString& markdown);

    /// @brief 获取支持的文件过滤器（用于保存对话框）
    static QString fileFilter();

signals:
    /// @brief 导出进度更新 (0-100)
    void exportProgress(int percent);

    /// @brief 导出完成
    void exportFinished(bool success, const QString& message);

private:
    /// @brief 导出为HTML文件
    bool exportHtml(const QString& markdown, const QString& filePath, const ExportOptions& options);

    /// @brief 导出为PDF文件（通过QPrinter+QTextDocument渲染）
    bool exportPdf(const QString& markdown, const QString& filePath, const ExportOptions& options);

    /// @brief 生成完整HTML文档（带独立CSS，用于HTML导出/浏览器）
    QString generateHtmlDocument(const QString& htmlBody,
                                 const ExportOptions& options) const;

    /// @brief 生成QTextDocument友好的HTML（用于PDF导出，使用pt单位）
    QString generatePdfHtmlDocument(const QString& htmlBody,
                                    const ExportOptions& options) const;

    /// @brief 从HTML中提取标题生成TOC导航
    static QString generateTocFromHtml(const QString& htmlBody);

    class IMarkdownParser* m_parser = nullptr;
};

#endif // MDEXPORTER_H
