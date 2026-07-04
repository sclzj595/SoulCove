#ifndef HEADERSYMBOLSCANNER_H
#define HEADERSYMBOLSCANNER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QPair>
#include <QDir>
#include <QSet>

/// @brief 头文件符号扫描器 — 解析 #include/import 引入的本地文件，提取符号名
///
/// 设计模式：
/// - 策略模式：按语言后缀选择不同的解析策略（C++/Python/JS）
/// - 单一职责：只负责"扫描文件 → 提取符号名"，不负责高亮
///
/// 解耦要点：
/// - 不依赖 LSP，纯正则解析，离线可用
/// - 返回 (符号名, 语义角色) 列表，由 CodeSyntaxHighlighter 消费
/// - 只解析本地文件（#include "..." / import module），不解析系统库
class HeaderSymbolScanner
{
public:
    /// @brief 扫描源文件中的 #include/import 引入的本地文件，提取符号名
    /// @param sourceFilePath 源文件路径（用于解析相对路径）
    /// @param sourceContent 源文件内容
    /// @return 符号列表 (符号名, 语义角色)，角色与 CodeSyntaxHighlighter::colorForRole 一致
    static QList<QPair<QString, QString>> scanForExternalSymbols(
        const QString& sourceFilePath, const QString& sourceContent);

private:
    /// C/C++: 解析 #include "..." 指令，读取本地头文件，提取符号
    static QList<QPair<QString, QString>> scanCppHeaders(
        const QString& sourceFilePath, const QString& sourceContent);

    /// Python: 解析 import / from...import 语句，读取本地 .py 文件，提取符号
    static QList<QPair<QString, QString>> scanPythonModules(
        const QString& sourceFilePath, const QString& sourceContent);

    /// JS/TS: 解析 require() / import 语句，读取本地 .js/.ts 文件，提取符号
    static QList<QPair<QString, QString>> scanJsModules(
        const QString& sourceFilePath, const QString& sourceContent);

    /// 从 C/C++ 文件内容中提取符号名（class/struct/enum/function/typedef/define/全局变量）
    static QList<QPair<QString, QString>> extractCppSymbols(const QString& content);

    /// 从 Python 文件内容中提取符号名（class/def）
    static QList<QPair<QString, QString>> extractPythonSymbols(const QString& content);

    /// 从 JS/TS 文件内容中提取符号名（function/class/const）
    static QList<QPair<QString, QString>> extractJsSymbols(const QString& content);

    /// H2: 解析头文件路径 — 从多个候选位置查找头文件
    /// @param sourceDir 源文件所在目录
    /// @param includePath #include 指令中的路径
    /// @return 找到的头文件绝对路径，未找到返回空字符串
    static QString resolveHeaderPath(const QDir& sourceDir, const QString& includePath);

    /// H2: 递归扫描头文件（传递性扫描 #include 的头文件，带深度限制和环检测）
    /// @param headerPath 起始头文件路径
    /// @param allSymbols 输出：累积的符号列表
    /// @param visited 已访问文件集合（防止循环引用）
    /// @param depth 当前递归深度（限制最大 3 层）
    static void scanCppHeaderRecursive(const QString& headerPath,
                                       QList<QPair<QString, QString>>& allSymbols,
                                       QSet<QString>& visited, int depth);

    /// 安全读取文件内容（文件不存在时返回空字符串）
    static QString readFileContent(const QString& path);
};

#endif // HEADERSYMBOLSCANNER_H
