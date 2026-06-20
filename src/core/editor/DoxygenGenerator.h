#ifndef DOXYGENGENERATOR_H
#define DOXYGENGENERATOR_H

#include <QString>

class QTextCursor;
class QTextBlock;

/// @brief Doxygen/Docstring 注释生成器
///
/// 职责：从编辑器当前光标位置向上扫描函数签名，
/// 自动生成 Python Google Style docstring 或 C/C++ Doxygen 注释。
///
/// 设计说明：
/// - 纯静态工具类（无状态），与 EditorActions/FileController 风格一致
/// - 仅依赖 Qt 公开 API（QTextCursor/QTextBlock），不依赖 MyTextEdit 私有成员
/// - 由 MyTextEdit::contextMenuEvent 调用（"生成注释"菜单项 / Ctrl+Shift+D）
class DoxygenGenerator
{
public:
    /// 在当前光标位置上方插入 Doxygen/Docstring 注释
    /// @param cursor 当前文本光标（用于定位函数定义行）
    static void insertComment(QTextCursor cursor);

private:
    DoxygenGenerator() = delete;

    /// 向上扫描最多 60 行，检测函数签名
    /// @return "py|name|params" 或 "cpp|name|params"，未检测到返回空串
    static QString detectFunctionSignature(const QTextBlock& startBlock);

    /// 从函数定义行提取缩进字符串
    static QString extractIndent(const QTextBlock& startBlock);

    /// 解析参数列表，提取参数名（去除类型前缀和默认值）
    static QStringList parseParams(const QString& rawParams);

    /// 生成 Python Google Style docstring
    static QString generatePythonDocstring(const QString& indent,
                                           const QStringList& params);

    /// 生成 C/C++ Doxygen 注释
    static QString generateCppDoxygen(const QString& indent,
                                      const QString& funcName,
                                      const QStringList& params);
};

#endif // DOXYGENGENERATOR_H
