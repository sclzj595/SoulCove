#ifndef CODEFORMATTER_H
#define CODEFORMATTER_H

#include <QObject>
#include <QString>
#include <QProcess>

/// @brief 代码格式化器（单例）
/// 优先使用 clang-format 进行格式化，若不可用则使用内置简单格式化
/// 支持 Ctrl+Shift+I 快捷键触发
class CodeFormatter : public QObject
{
    Q_OBJECT
public:
    /// 获取全局单例实例
    static CodeFormatter& instance();

    /// 格式化文本内容，返回格式化后的文本
    /// 如果 clang-format 不可用，使用内置缩进格式化
    /// @param code      待格式化的代码文本
    /// @param filePath  文件路径（用于 clang-format --assume-filename）
    /// @return 格式化后的文本
    QString format(const QString& code, const QString& filePath = QString());

    /// 检查 clang-format 是否可用
    bool isClangFormatAvailable() const;

    /// 设置缩进风格（用于内置格式化）
    enum class IndentStyle { Spaces, Tabs };
    void setIndentStyle(IndentStyle style);
    void setIndentSize(int size);

private:
    CodeFormatter();
    ~CodeFormatter() = default;

    // 禁止拷贝
    CodeFormatter(const CodeFormatter&) = delete;
    CodeFormatter& operator=(const CodeFormatter&) = delete;

    /// 使用内置简单格式化：统一缩进、去除行尾空格、文件末尾换行、花括号风格调整
    QString formatBuiltin(const QString& code);

    IndentStyle m_indentStyle = IndentStyle::Spaces;
    int m_indentSize = 4;
    bool m_clangFormatChecked = false;
    bool m_hasClangFormat = false;
};

#endif // CODEFORMATTER_H
