#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include "core/plugin/PluginLoader.h"
#include "interfaces/plugin/IPlugin.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <memory>

class CommandRegistry;
class QPluginLoader;

/// @brief 插件管理器（单例，M7 四件套之一）
///
/// 职责：
/// - 扫描插件目录，经 PluginLoader 加载全部候选动态库
/// - 依次调用 initialize()（传入共享 PluginAPI）
/// - 应用退出时调用 shutdownAll() 收口
/// - 维护已加载插件清单与错误信息（供设置页/关于页展示）
///
/// 约定：
/// - 仅 IDE 产品线在启动时调用 loadPlugins()（ProductConfig 差异化）
/// - 单个插件初始化失败不影响其他插件（错误隔离）
/// - 重复加载（同一文件路径）自动跳过
class PluginManager : public QObject
{
    Q_OBJECT

public:
    static PluginManager& instance();

    ~PluginManager() override;
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    /// 注入宿主命令注册表（Widget::registerCommands 后调用；插件命令经此进入命令面板）
    void setCommandRegistry(CommandRegistry* registry);

    /// 扫描并加载目录下全部插件动态库（*.dll/*.so/*.dylib）
    /// @param dir 插件目录（默认应用目录下 plugins/）
    /// @return 成功初始化的插件数量
    int loadPlugins(const QString& dir = QString());

    /// 退出收口：逆序 shutdown 全部已初始化插件并卸载动态库
    void shutdownAll();

    /// 已加载（含初始化失败）的插件记录
    const QList<PluginRecord>& plugins() const { return m_records; }

    /// 按插件标识查找实例
    IPlugin* plugin(const QString& name) const;

    /// 插件是否已成功初始化
    bool isInitialized(const QString& name) const;

signals:
    void pluginLoaded(const QString& name, const QString& version);
    void pluginFailed(const QString& name, const QString& error);

private:
    explicit PluginManager(QObject* parent = nullptr);

    /// 平台动态库后缀过滤（*.dll / *.so / *.dylib）
    static QStringList libraryNameFilters();

    QList<PluginRecord> m_records;                        ///< 加载记录（含失败记录，用于诊断）
    QHash<QString, QPluginLoader*> m_loaders;             ///< filePath → loader（保持存活，手动管理）
    std::unique_ptr<class PluginAPI> m_api;               ///< 共享插件 API（M7 四件套之三）
};

#endif // PLUGINMANAGER_H
