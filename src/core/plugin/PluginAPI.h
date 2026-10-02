#ifndef PLUGINAPI_H
#define PLUGINAPI_H

#include "interfaces/plugin/IPluginAPI.h"

#include <QString>
#include <QHash>
#include <functional>

class CommandRegistry;

/// @brief 插件 API 实现（桥接宿主能力，M7 四件套之三）
///
/// 将 IPluginAPI 的调用转发到宿主内部模块：
/// - log()               → Logger 统一日志
/// - registerCommand()   → CommandRegistry（Widget 持有，经 PluginManager::setCommandRegistry 注入）
/// - pluginDataDir()     → AppLocalDataLocation/plugins/<name>/
/// - currentDocument()   → 文档提供者回调（Widget 注入）
/// - subscribeEvent()    → 事件注册表（Widget::onUpdate 经 PluginManager::dispatchEvent 派发）
///
/// 所有权安全：
/// - 每条命令/订阅记录所有者插件名；PluginManager::shutdownAll 在 shutdown 前调
///   removePluginRegistrations(name) 清理，避免悬垂回调
///
/// 线程约定：所有方法仅在主线程调用（插件初始化、命令与事件回调都在主线程）。
class PluginAPI : public IPluginAPI
{
public:
    explicit PluginAPI(QString appVersion);

    // —— IPluginAPI ——
    void log(const QString& message) override;
    bool registerCommand(const QString& id, const QString& description,
                         std::function<void()> handler) override;
    QString pluginDataDir(const QString& pluginName) const override;
    QString applicationVersion() const override;
    DocumentInfo currentDocument() const override;
    int subscribeEvent(const QString& event,
                       std::function<void(const QVariant& data)> handler) override;
    bool unsubscribeEvent(int subscriptionId) override;

    // —— 宿主侧注入（由 PluginManager 调用，插件不可见）——

    /// 注入宿主命令注册表（Widget::registerCommands 后由 PluginManager 调用）
    void setCommandRegistry(CommandRegistry* registry);

    /// 注入当前文档提供者（Widget 持有 m_currentTextEdit，lambda 安全捕获）
    void setDocumentProvider(std::function<DocumentInfo()> provider);

    /// 事件派发（Widget::onUpdate 转发宿主观察者事件）
    void dispatchEvent(const QString& event, const QVariant& data);

    /// 设置"当前初始化中的插件"上下文（subscribeEvent 记录所有者用）
    void setCurrentPluginOwner(const QString& pluginName);

    /// 清理指定插件注册的全部资源（命令 + 订阅），插件 shutdown 前调用
    void removePluginRegistrations(const QString& pluginName);

private:
    struct EventSubscription {
        QString owner;                                  ///< 所有者插件名
        QString event;                                  ///< 事件名
        std::function<void(const QVariant&)> handler;   ///< 回调
    };

    QString m_appVersion;
    CommandRegistry* m_commandRegistry = nullptr;       ///< 宿主命令注册表（非持有）
    QHash<QString, QString> m_commandOwnerByCommand;    ///< 命令ID → 所有者插件（卸载时清理）
    std::function<DocumentInfo()> m_documentProvider;   ///< 当前文档提供者（非持有）
    QHash<int, EventSubscription> m_subscriptions;      ///< 订阅ID → 订阅
    int m_nextSubscriptionId = 1;
    QString m_currentPluginOwner;                       ///< 当前初始化中的插件名
};

#endif // PLUGINAPI_H
