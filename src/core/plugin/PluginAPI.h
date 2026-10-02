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
/// - log()          → Logger 统一日志
/// - registerCommand() → CommandRegistry（Widget 持有，经 PluginManager::setCommandRegistry 注入）
/// - pluginDataDir()   → AppLocalDataLocation/plugins/<name>/
///
/// 线程约定：所有方法仅在主线程调用（插件初始化与命令回调都在主线程）。
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

    /// 注入宿主命令注册表（Widget::registerCommands 后由 PluginManager 调用）
    void setCommandRegistry(CommandRegistry* registry);

private:
    QString m_appVersion;
    CommandRegistry* m_commandRegistry = nullptr;   ///< 宿主命令注册表（非持有）
    QHash<QString, QString> m_commandOwners;        ///< 命令ID → 注册插件（用于冲突判定与卸载提示）
};

#endif // PLUGINAPI_H
