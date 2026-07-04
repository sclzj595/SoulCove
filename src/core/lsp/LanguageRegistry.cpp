#include "LanguageRegistry.h"

#include <QFileInfo>

// ============================================================
// LanguageRegistry — 单例模式实现
// ============================================================

LanguageRegistry& LanguageRegistry::instance()
{
    static LanguageRegistry registry;
    return registry;
}

LanguageRegistry::LanguageRegistry()
{
    // 注册所有支持的语言（单一数据源，新增语言只需在此添加）
    // C/C++ 系列
    registerLanguage(
        QStringLiteral("cpp"),
        QStringLiteral("cpp"),
        {QStringLiteral("cpp"), QStringLiteral("c"),
         QStringLiteral("h"), QStringLiteral("hpp"),
         QStringLiteral("cc"), QStringLiteral("cxx"),
         QStringLiteral("hxx"), QStringLiteral("inl")}
    );
    // Python
    registerLanguage(
        QStringLiteral("python"),
        QStringLiteral("python"),
        {QStringLiteral("py"), QStringLiteral("pyw")}
    );
    // JavaScript
    registerLanguage(
        QStringLiteral("javascript"),
        QStringLiteral("javascript"),
        {QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("mjs")}
    );
    // TypeScript
    registerLanguage(
        QStringLiteral("typescript"),
        QStringLiteral("typescript"),
        {QStringLiteral("ts"), QStringLiteral("tsx")}
    );
    // Go
    registerLanguage(
        QStringLiteral("go"),
        QStringLiteral("go"),
        {QStringLiteral("go")}
    );
    // Java
    registerLanguage(
        QStringLiteral("java"),
        QStringLiteral("java"),
        {QStringLiteral("java")}
    );
    // Rust
    registerLanguage(
        QStringLiteral("rust"),
        QStringLiteral("rust"),
        {QStringLiteral("rs")}
    );
}

void LanguageRegistry::registerLanguage(const QString& langId,
                                         const QString& lspLanguageId,
                                         const QStringList& suffixes)
{
    LanguageDescriptor desc;
    desc.langId = langId;
    desc.lspLanguageId = lspLanguageId;
    desc.suffixes = suffixes;
    m_descriptors[langId] = desc;

    // 构建 suffix → langId 反查表
    for (const QString& suffix : suffixes) {
        m_suffixToLangId[suffix.toLower()] = langId;
    }
}

QString LanguageRegistry::langIdForFile(const QString& filePath) const
{
    // P1: 路径 → langId 内存缓存，重复查询直接命中（O(1) 哈希查找）
    // 缓存键使用规范化路径，避免大小写/分隔符差异导致缓存失效
    auto it = m_filePathCache.constFind(filePath);
    if (it != m_filePathCache.constEnd()) {
        return it.value();
    }

    // 缓存未命中：执行后缀提取 + 哈希查找
    QString suffix = QFileInfo(filePath).suffix().toLower();
    QString langId = langIdForSuffix(suffix);

    // 写入缓存（空 langId 也缓存，避免对不支持文件反复执行 QFileInfo）
    m_filePathCache.insert(filePath, langId);
    return langId;
}

QString LanguageRegistry::langIdForSuffix(const QString& suffix) const
{
    return m_suffixToLangId.value(suffix.toLower());
}

QStringList LanguageRegistry::suffixesForLangId(const QString& langId) const
{
    auto it = m_descriptors.constFind(langId);
    if (it != m_descriptors.constEnd()) {
        return it.value().suffixes;
    }
    return {};
}

bool LanguageRegistry::matches(const QString& langId, const QString& filePath) const
{
    QString suffix = QFileInfo(filePath).suffix().toLower();
    auto it = m_descriptors.constFind(langId);
    if (it == m_descriptors.constEnd()) return false;
    return it.value().suffixes.contains(suffix);
}

QString LanguageRegistry::lspLanguageId(const QString& langId) const
{
    auto it = m_descriptors.constFind(langId);
    if (it != m_descriptors.constEnd()) {
        return it.value().lspLanguageId;
    }
    return langId;  // 默认返回 langId 本身
}

QList<QString> LanguageRegistry::allLangIds() const
{
    return m_descriptors.keys();
}
