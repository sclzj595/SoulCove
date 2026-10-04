#include "core/ai/AIProviderStore.h"
#include "Logger.hpp"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

// ========== 厂家预设（OpenAI 兼容协议） ==========

QList<AIProviderStore::VendorPreset> AIProviderStore::vendorPresets()
{
    return {
        { QStringLiteral("智谱 GLM"),
          QStringLiteral("https://open.bigmodel.cn/api/paas/v4"),
          QStringLiteral("glm-4-flash") },
        { QStringLiteral("DeepSeek"),
          QStringLiteral("https://api.deepseek.com/v1"),
          QStringLiteral("deepseek-chat") },
        { QStringLiteral("Kimi"),
          QStringLiteral("https://api.moonshot.cn/v1"),
          QStringLiteral("moonshot-v1-8k") },
        { QStringLiteral("通义千问"),
          QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1"),
          QStringLiteral("qwen-turbo") },
        { QStringLiteral("OpenAI"),
          QStringLiteral("https://api.openai.com/v1"),
          QStringLiteral("gpt-4o-mini") },
        { QStringLiteral("Claude"),
          QStringLiteral("https://api.anthropic.com"),
          QStringLiteral("claude-sonnet-4-5") },
        { QStringLiteral("Gemini"),
          QStringLiteral("https://generativelanguage.googleapis.com"),
          QStringLiteral("gemini-2.0-flash") },
        { QStringLiteral("Ollama (本地)"),
          QStringLiteral("http://localhost:11434/v1"),
          QStringLiteral("qwen2.5-coder:7b") },
        { QStringLiteral("自定义"), QString(), QString() },
    };
}

AIProviderStore& AIProviderStore::instance()
{
    static AIProviderStore s_instance;
    return s_instance;
}

AIProviderStore::AIProviderStore()
{
    load();
}

QString AIProviderStore::configFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
           + QStringLiteral("/ai/ai_providers.json");
}

AIProvider AIProviderStore::activeProvider() const
{
    for (const AIProvider& p : m_providers) {
        if (p.enabled && p.id == m_activeId) return p;
    }
    for (const AIProvider& p : m_providers) {
        if (p.enabled) return p;
    }
    return AIProvider();
}

void AIProviderStore::setProviders(const QList<AIProvider>& providers, const QString& activeId)
{
    m_providers = providers;
    m_activeId = activeId;
    // 校验 activeId 有效性（指向不存在或已禁用条目时回落到第一个启用项）
    bool valid = false;
    for (const AIProvider& p : m_providers) {
        if (p.enabled && p.id == m_activeId) { valid = true; break; }
    }
    if (!valid) {
        m_activeId.clear();
        for (const AIProvider& p : m_providers) {
            if (p.enabled) { m_activeId = p.id; break; }
        }
    }
    save();
    emit providersChanged();
}

void AIProviderStore::load()
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_DEBUG_S("AIProviderStore", "load", "配置文件不存在或不可读，使用空配置");
        return;
    }

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_DEBUG_S("AIProviderStore", "load", "配置解析失败，使用空配置");
        return;
    }

    const QJsonObject root = doc.object();
    m_activeId = root.value(QStringLiteral("activeProvider")).toString();

    m_providers.clear();
    const QJsonArray arr = root.value(QStringLiteral("providers")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        AIProvider p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.name = o.value(QStringLiteral("name")).toString();
        p.vendor = o.value(QStringLiteral("vendor")).toString();
        p.baseUrl = o.value(QStringLiteral("baseUrl")).toString();
        p.apiKey = o.value(QStringLiteral("apiKey")).toString();
        p.model = o.value(QStringLiteral("model")).toString();
        p.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (p.id.isEmpty()) p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_providers.append(p);
    }
    LOG_DEBUG_S("AIProviderStore", "load", "已加载" << m_providers.size() << "个 AI 服务商配置");
}

void AIProviderStore::save()
{
    const QString path = configFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("activeProvider"), m_activeId);

    QJsonArray arr;
    for (const AIProvider& p : m_providers) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), p.id);
        o.insert(QStringLiteral("name"), p.name);
        o.insert(QStringLiteral("vendor"), p.vendor);
        o.insert(QStringLiteral("baseUrl"), p.baseUrl);
        o.insert(QStringLiteral("apiKey"), p.apiKey);
        o.insert(QStringLiteral("model"), p.model);
        o.insert(QStringLiteral("enabled"), p.enabled);
        arr.append(o);
    }
    root.insert(QStringLiteral("providers"), arr);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_DEBUG_S("AIProviderStore", "save", "配置写入失败:" << path.toStdString());
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}
