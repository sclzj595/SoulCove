#ifndef SPELLCHECKER_H
#define SPELLCHECKER_H

#include <QString>
#include <QStringList>
#include <QSet>
#include <QList>

/// @brief 拼写错误区间（文档内的字符位置）
struct SpellMisspelledRange {
    int start;    // 单词起始位置（文档字符偏移）
    int length;   // 单词长度
    QString word; // 原始单词文本
};

/// @brief 轻量拼写检查器（单例）
///
/// 职责：
/// - 内置基础英文词典（常见单词，嵌入源码）
/// - 用户词典持久化（AppConfigLocation/user_dictionary.txt，每行一个单词）
/// - isCorrect() / suggestions() / addToDictionary() API
/// - checkText() 批量扫描文本返回拼写错误区间
///
/// 设计说明：
/// - 单例（与 ConfigManager 风格一致），全局共享词典
/// - 词典统一小写存储，查询时大小写不敏感
/// - 建议算法基于 Levenshtein 编辑距离（距离 1~2 内的词典词优先）
/// - 代码标识符过滤：跳过 CamelCase / 含数字的 token，仅检查纯字母单词
class SpellChecker
{
public:
    /// 获取单例实例
    static SpellChecker& instance();

    /// 判断单词是否拼写正确（词典中存在，大小写不敏感）
    bool isCorrect(const QString& word) const;

    /// 返回单词的拼写建议（基于编辑距离，最多 maxCount 个）
    QStringList suggestions(const QString& word, int maxCount = 5) const;

    /// 添加单词到用户词典并立即持久化
    void addToDictionary(const QString& word);

    /// 启用/禁用拼写检查（禁用时 checkText 返回空列表）
    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled; }

    /// 检查文本，返回所有拼写错误区间
    /// 仅检查纯字母单词（跳过 CamelCase 标识符与含数字 token）
    QList<SpellMisspelledRange> checkText(const QString& text) const;

    /// 重新加载用户词典（外部修改词典文件后调用）
    void reloadUserDictionary();

private:
    SpellChecker();
    ~SpellChecker() = default;
    SpellChecker(const SpellChecker&) = delete;
    SpellChecker& operator=(const SpellChecker&) = delete;

    void loadBuiltinDictionary();
    void loadUserDictionary();
    void saveUserDictionary();
    QString userDictionaryPath() const;

    /// 判断单词是否为代码标识符（应跳过不检查）
    /// 规则：含数字/下划线，或混合大小写（CamelCase/PascalCase）视为标识符
    static bool isCodeIdentifier(const QString& word);

    /// Levenshtein 编辑距离（动态规划，O(m*n)）
    static int levenshtein(const QString& a, const QString& b);

    QSet<QString> m_dictionary;   // 内置 + 用户词典合并视图（小写）
    QSet<QString> m_userWords;    // 仅用户词典（小写，用于持久化）
    bool m_enabled = false;
};

#endif // SPELLCHECKER_H
