#ifndef SNIPPETMANAGER_H
#define SNIPPETMANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>

/// @brief 代码片段数据结构
struct CodeSnippet {
    QString id;            // 唯一ID
    QString name;          // 显示名称
    QString description;   // 描述
    QString language;      // 适用语言: "all"/"cpp"/"python"/"javascript"
    QString prefix;        // 触发前缀（输入前缀+Tab触发）
    QString body;          // 片段内容（支持 $1 $2 ${1:default} 占位符）
    QString shortcut;      // 可选快捷键
    QDateTime createdTime;
    QDateTime modifiedTime;
};

/// @brief 代码片段管理器（单例）
/// 管理常用代码模板/片段，支持 CRUD、搜索、触发检测、展开占位符
class SnippetManager : public QObject
{
    Q_OBJECT

public:
    static SnippetManager& instance();

    // === CRUD ===
    QList<CodeSnippet> allSnippets() const;
    QList<CodeSnippet> snippetsForLanguage(const QString& lang) const;
    CodeSnippet snippet(const QString& id) const;
    void addSnippet(const CodeSnippet& snippet);
    void removeSnippet(const QString& id);
    void updateSnippet(const CodeSnippet& snippet);

    // === 搜索 ===
    QList<CodeSnippet> search(const QString& keyword) const;

    // === 触发检测（输入前缀时检查是否有匹配snippet）===
    CodeSnippet findTrigger(const QString& prefix) const;

    /// @brief 展开 snippet: 将 $1 $2 占位符替换为可跳转光标位置标记
    /// 返回展开后的文本，同时记录占位符位置供编辑器使用
    /// @param snippet  要展开的片段
    /// @param selection  当前编辑器选中文本，用于替换 $SELECTION 变量（为空则替换为空串）
    QString expandSnippet(const CodeSnippet& snippet, const QString& selection = QString());

    // === VSCode 格式兼容（P2-H02 子项3）===
    /// @brief 从 VSCode snippet JSON 文件导入片段
    /// VSCode 格式: { "name": { "prefix","body":[...],"description" } }
    /// @param filePath  VSCode snippet JSON 文件路径
    /// @param language  指定导入片段的语言，为空则使用 "all"
    /// @return 导入成功返回 true
    bool importFromVscodeJson(const QString& filePath, const QString& language = QString());
    /// @brief 导出片段为 VSCode snippet JSON 格式
    /// @param filePath  目标文件路径
    /// @param language  仅导出指定语言的片段，为空则导出全部
    /// @return 导出成功返回 true
    bool exportToVscodeJson(const QString& filePath, const QString& language = QString()) const;

    // === 持久化 ===
    void saveToFile();
    void loadFromFile();

private:
    SnippetManager();
    QMap<QString, CodeSnippet> m_snippets;  // id → snippet
    QString m_storagePath;                   // JSON存储路径

    // 内置默认 snippets
    void loadDefaultSnippets();

    /// 生成唯一ID
    static QString generateId();
};

#endif // SNIPPETMANAGER_H
