#ifndef AIPROVIDERSTORE_H
#define AIPROVIDERSTORE_H

#include <QObject>
#include <QList>
#include <QString>

/// @brief AI 服务商配置条目（M8 O33）
///
/// 所有主流云厂商均通过 OpenAI 兼容协议（/chat/completions）接入，
/// 用户只需在可视化面板填写：名称 / 厂家 / BaseUrl / API Key / 模型
struct AIProvider {
    QString id;        ///< 唯一标识（UUID）
    QString name;      ///< 显示名（用户自定义，如"我的智谱"）
    QString vendor;    ///< 厂家（智谱 GLM / DeepSeek / Kimi / 通义千问 / OpenAI / 自定义）
    QString baseUrl;   ///< OpenAI 兼容 API 根地址（不含 /chat/completions）
    QString apiKey;    ///< API Key（仅保存在本地配置文件，不参与配置导出）
    QString model;     ///< 模型名（如 glm-4-flash / deepseek-chat）
    bool enabled = true;
};

/// @brief AI 服务商配置仓库（单例，M8 O33）
///
/// 持久化为 ai_providers.json（module.json 风格）：
/// { "version": 1, "activeProvider": "<id>", "providers": [ {...} ] }
class AIProviderStore : public QObject
{
    Q_OBJECT

public:
    /// 厂家预设（OpenAI 兼容协议）：名称 / 默认 BaseUrl / 默认模型
    struct VendorPreset {
        QString vendor;
        QString baseUrl;
        QString model;
    };
    static QList<VendorPreset> vendorPresets();

    static AIProviderStore& instance();

    const QList<AIProvider>& providers() const { return m_providers; }

    /// 当前生效的 Provider（优先 activeId 且 enabled，否则第一个 enabled，均无则返回空条目）
    AIProvider activeProvider() const;
    QString activeProviderId() const { return m_activeId; }

    /// 整体写回（触发持久化 + providersChanged 信号）
    void setProviders(const QList<AIProvider>& providers, const QString& activeId);

    void load();
    void save();

signals:
    /// 服务商配置变化（设置页保存后，对话面板刷新下拉）
    void providersChanged();

private:
    AIProviderStore();
    QString configFilePath() const;

    QList<AIProvider> m_providers;
    QString m_activeId;
};

#endif // AIPROVIDERSTORE_H
