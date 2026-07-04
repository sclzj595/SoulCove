#ifndef EDITORCONFIGPARSER_H
#define EDITORCONFIGPARSER_H

#include <QString>
#include <QStringList>
#include <QRegularExpression>

/// @brief .editorconfig 配置项结构体
///
/// 字段语义参考 https://editorconfig.org/ 规范：
/// - indentStyle: "space" / "tab" / 空（未指定）
/// - indentSize: 缩进字符数（-1 表示未指定；"tab" 时与 tabWidth 关联）
/// - tabWidth: Tab 显示宽度（-1 表示未指定，回退到 indentSize）
/// - endOfLine: "lf" / "crlf" / "cr" / 空（未指定）
/// - charset: "utf-8" / "utf-8-bom" / "latin1" / "utf-16be" / "utf-16le" / 空
/// - insertFinalNewline: 是否在文件末尾插入空行（未指定时为 false）
/// - trimTrailingWhitespace: 是否去除行尾空白（未指定时为 false）
/// - maxLineLength: 最大行长（-1 表示未指定）
struct EditorConfig {
    QString indentStyle;
    int     indentSize = -1;
    int     tabWidth   = -1;
    QString endOfLine;
    QString charset;
    bool    insertFinalNewline      = false;
    bool    trimTrailingWhitespace  = false;
    int     maxLineLength           = -1;

    /// 是否有任何字段被显式设置（用于判断是否需要应用配置）
    bool isValid() const {
        return !indentStyle.isEmpty() || indentSize > 0 || tabWidth > 0 ||
               !endOfLine.isEmpty() || !charset.isEmpty() ||
               insertFinalNewline || trimTrailingWhitespace || maxLineLength > 0;
    }
};

/// @brief .editorconfig 文件解析器（静态工具类）
///
/// 职责：从指定文件路径向上查找 .editorconfig 文件并解析，
///       按 glob 模式匹配返回该文件应应用的配置。
///
/// 设计说明：
/// - 静态工具类（无状态），与 FileController 风格一致
/// - 遵循 editorconfig 规范：
///   * 从文件所在目录向上查找 .editorconfig
///   * 最近的 .editorconfig 优先级最高（属性覆盖）
///   * root=true 时停止向上查找
///   * 支持 [*] [*.cpp] [*.{c,cpp}] 等 glob 模式
/// - 不依赖第三方库，纯 Qt 实现（QFile + QRegularExpression + 简易 glob 转 regex）
class EditorConfigParser
{
public:
    /// 解析指定文件路径对应的 .editorconfig 配置
    /// @param filePath 目标文件绝对路径
    /// @return 合并后的 EditorConfig（无配置时返回 isValid()==false 的默认结构）
    static EditorConfig parse(const QString& filePath);

private:
    EditorConfigParser() = delete;

    /// 解析单个 .editorconfig 文件内容
    /// @param configPath .editorconfig 文件路径
    /// @param targetFileName 目标文件名（用于 glob 匹配，如 "main.cpp"）
    /// @param isRoot 输出参数：该文件是否标记 root=true
    /// @return 该文件中匹配目标文件的所有配置项（已合并多个 section）
    static EditorConfig parseFile(const QString& configPath,
                                  const QString& targetFileName,
                                  bool& isRoot);

    /// 将 editorconfig glob 模式转换为 QRegularExpression
    /// 支持: * / ** / ? / {a,b,c} / [abc] / [!abc]
    /// @param glob editorconfig glob 字符串
    /// @return 编译好的正则表达式（带锚点 ^...$）
    static QRegularExpression globToRegex(const QString& glob);

    /// 转换 editorconfig 字符串值到标准形式（小写、去空白）
    static QString normalizeValue(const QString& value);
};

#endif // EDITORCONFIGPARSER_H
