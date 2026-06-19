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
    QString expandSnippet(const CodeSnippet& snippet);

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
