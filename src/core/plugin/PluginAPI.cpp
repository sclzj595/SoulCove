#include "core/plugin/PluginAPI.h"

#include "controller/CommandRegistry.h"
#include "Logger.hpp"

#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>

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
    m_commandOwners[id] = description;
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

void PluginAPI::setCommandRegistry(CommandRegistry* registry)
{
    m_commandRegistry = registry;
}
