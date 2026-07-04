#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QMap>

/// @file LanguageRegistry.h
/// @brief 语言注册表 — 单一数据源（Single Source of Truth）
///
/// 设计模式：注册表模式（Registry Pattern）
/// 集中管理 langId ↔ 文件后缀 的双向映射，消除 Widget/LspManager 中的重复硬编码。
/// 新增语言只需在此注册，无需修改任何消费方（开闭原则 OCP）。
///
/// 使用方式：
///   LanguageRegistry::instance().langIdForFile("main.cpp") → "cpp"
///   LanguageRegistry::instance().suffixesForLangId("cpp") → {"cpp","c","h","hpp",...}
///   LanguageRegistry::instance().matches("cpp", "main.cpp") → true

/// @brief 语言描述符（每种语言的元数据）
struct LanguageDescriptor {
    QString langId;              ///< 内部语言ID（如 "cpp", "python"）
    QString lspLanguageId;       ///< LSP 协议 languageId（如 "cpp", "python"）
    QStringList suffixes;        ///< 文件后缀列表（不含点，如 "cpp", "h"）
};

/// @brief 语言注册表（单例模式）
class LanguageRegistry
{
public:
    /// 获取单例实例
    static LanguageRegistry& instance();

    /// 根据文件路径获取语言ID
    /// @param filePath 文件路径（如 "src/main.cpp"）
    /// @return 语言ID（如 "cpp"），不支持的语言返回空字符串
    QString langIdForFile(const QString& filePath) const;

    /// 根据文件后缀获取语言ID
    /// @param suffix 文件后缀（不含点，如 "cpp", "h"）
    /// @return 语言ID（如 "cpp"），不支持的语言返回空字符串
    QString langIdForSuffix(const QString& suffix) const;

    /// 根据语言ID获取该语言支持的所有文件后缀
    /// @param langId 语言ID（如 "cpp"）
    /// @return 后缀列表（如 {"cpp","c","h","hpp","cc","cxx","hxx","inl"}）
    QStringList suffixesForLangId(const QString& langId) const;

    /// 判断文件是否属于指定语言
    /// @param langId 语言ID
    /// @param filePath 文件路径
    /// @return 是否匹配
    bool matches(const QString& langId, const QString& filePath) const;

    /// 根据语言ID获取 LSP 协议 languageId
    /// @param langId 内部语言ID
    /// @return LSP languageId（如 "cpp" → "cpp", "python" → "python"）
    QString lspLanguageId(const QString& langId) const;

    /// 获取所有已注册语言ID列表
    QList<QString> allLangIds() const;

private:
    LanguageRegistry();

    /// 注册一种语言
    void registerLanguage(const QString& langId,
                          const QString& lspLanguageId,
                          const QStringList& suffixes);

    // suffix → langId 反查表
    QHash<QString, QString> m_suffixToLangId;
    // langId → LanguageDescriptor
    QMap<QString, LanguageDescriptor> m_descriptors;

    // P1: 文件路径 → langId 内存缓存（mutable 因 langIdForFile 是 const）
    // 重复打开、同目录文件直接读缓存，避免 QFileInfo::suffix() 字符串操作开销
    mutable QHash<QString, QString> m_filePathCache;
};
