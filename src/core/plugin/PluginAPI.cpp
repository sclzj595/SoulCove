#include "core/plugin/PluginAPI.h"

#include "controller/CommandRegistry.h"
#include "Logger.hpp"

#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>

/// 宿主支持订阅的事件白名单（与 FileOperator notifyObservers 事件名对齐）
static const QStringList kSupportedEvents = {
    QStringLiteral("fileOpened"),
    QStringLiteral("fileSaved"),
    QStringLiteral("fileClosed"),
    QStringLiteral("encodingChanged"),
};

PluginAPI::PluginAPI(QString appVersion)
    : m_appVersion(std::move(appVersion))
{
}

void PluginAPI::log(const QString& message)
{
    // 统一进入宿主日志系统，前缀 PluginAPI（插件名由调用方自带或约定写在消息内）
    LOG_INFO_S("PluginAPI", "log", message.toStdString());
}

bool PluginAPI::registerCommand(const QString& id, const QString& description,
                                std::function<void()> handler)
{
    if (!m_commandRegistry) {
        LOG_WARN_S("PluginAPI", "registerCommand",
                   "命令注册表未注入，无法注册命令: " << id.toStdString());
        return false;
    }
    if (id.isEmpty() || !handler) {
        LOG_WARN_S("PluginAPI", "registerCommand", "非法命令注册请求");
        return false;
    }
    // O37 借鉴：CommandRegistry 按 ID 精确匹配；插件命令冲突时后到者失败，先到者优先
    if (m_commandRegistry->contains(id)) {
        LOG_WARN_S("PluginAPI", "registerCommand",
                   "命令ID冲突，注册失败: " << id.toStdString());
        return false;
    }
    m_commandRegistry->registerCommand(id, std::move(handler));
    m_commandOwnerByCommand[id] = m_currentPluginOwner;
    LOG_INFO_S("PluginAPI", "registerCommand",
               "插件命令已注册: " << id.toStdString() << " (" << description.toStdString() << ")");
    return true;
}

QString PluginAPI::pluginDataDir(const QString& pluginName) const
{
    // 插件私有目录: <AppLocalData>/plugins/<pluginName>/，不存在则创建
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/plugins/") + pluginName;
    if (!QFileInfo::exists(dir)) {
        QDir().mkpath(dir);
    }
    return dir;
}

QString PluginAPI::applicationVersion() const
{
    return m_appVersion;
}

IPluginAPI::DocumentInfo PluginAPI::currentDocument() const
{
    return m_documentProvider ? m_documentProvider() : IPluginAPI::DocumentInfo{};
}

int PluginAPI::subscribeEvent(const QString& event,
                              std::function<void(const QVariant& data)> handler)
{
    if (!kSupportedEvents.contains(event)) {
        LOG_WARN_S("PluginAPI", "subscribeEvent",
                   "不支持的事件名: " << event.toStdString());
        return -1;
    }
    if (!handler) {
        LOG_WARN_S("PluginAPI", "subscribeEvent", "空的订阅回调");
        return -1;
    }
    const int id = m_nextSubscriptionId++;
    m_subscriptions[id] = EventSubscription{ m_currentPluginOwner, event, std::move(handler) };
    LOG_INFO_S("PluginAPI", "subscribeEvent",
               "插件 " << m_currentPluginOwner.toStdString() << " 订阅事件 "
                      << event.toStdString() << " (id=" << id << ")");
    return id;
}

bool PluginAPI::unsubscribeEvent(int subscriptionId)
{
    return m_subscriptions.remove(subscriptionId) > 0;
}

void PluginAPI::dispatchEvent(const QString& event, const QVariant& data)
{
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        if (it.value().event == event && it.value().handler) {
            it.value().handler(data);
        }
    }
}

void PluginAPI::setCommandRegistry(CommandRegistry* registry)
{
    m_commandRegistry = registry;
}

void PluginAPI::setDocumentProvider(std::function<DocumentInfo()> provider)
{
    m_documentProvider = std::move(provider);
}

void PluginAPI::setCurrentPluginOwner(const QString& pluginName)
{
    m_currentPluginOwner = pluginName;
}

void PluginAPI::removePluginRegistrations(const QString& pluginName)
{
    // 清理该插件注册的命令（从 CommandRegistry 注销，命令面板中失效）
    for (auto it = m_commandOwnerByCommand.begin(); it != m_commandOwnerByCommand.end(); ) {
        if (it.value() == pluginName) {
            if (m_commandRegistry) {
                m_commandRegistry->unregisterCommand(it.key());
            }
            LOG_DEBUG_S("PluginAPI", "removePluginRegistrations",
                        "移除插件命令: " << it.key().toStdString());
            it = m_commandOwnerByCommand.erase(it);
        } else {
            ++it;
        }
    }

    // 清理该插件的事件订阅（防悬垂回调 — 插件实例即将卸载）
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ) {
        if (it.value().owner == pluginName) {
            LOG_DEBUG_S("PluginAPI", "removePluginRegistrations",
                        "移除插件订阅: " << pluginName.toStdString()
                                        << " (id=" << it.key() << ")");
            it = m_subscriptions.erase(it);
        } else {
            ++it;
        }
    }
}
