#ifndef IPLUGIN_H
#define IPLUGIN_H

#include <QObject>   // Q_DECLARE_INTERFACE 依赖 qobjectdefs
#include <QString>

class IPluginAPI;

/// @brief 插件抽象接口（M7 插件系统四件套之一）
///
/// 所有 SoulCove 插件必须实现此接口，以 Qt 动态库（QPluginLoader）形式加载。
/// 插件实现类需同时继承 QObject，并使用：
///   Q_PLUGIN_METADATA(IID SoulCovePluginIID FILE "metadata.json")
///   Q_INTERFACES(IPlugin)
///
/// 生命周期: loaded → initialize() → [运行期] → shutdown() → unloaded
///
/// ABI 约定：
/// - 接口虚函数表一旦发布不可变更；新增能力只能通过新增接口或递增 IID 版本
/// - IID: "com.soulcove.plugin/1.0"（主版本不匹配的插件将被拒绝加载）
class IPlugin
{
public:
    virtual ~IPlugin() = default;

    /// 插件唯一标识（反向域名风格，如 "com.soulcove.hello"）
    virtual QString name() const = 0;

    /// 语义化版本号（如 "1.0.0"）
    virtual QString version() const = 0;

    /// 一句话描述（展示在插件信息中）
    virtual QString description() const = 0;

    /// 初始化（在插件加载后调用一次；宿主功能此时已可用）
    /// @param api 宿主提供的插件 API（日志/命令注册/数据目录）
    /// @return false 表示初始化失败，PluginManager 将拒绝启用该插件并记录错误
    virtual bool initialize(IPluginAPI* api) = 0;

    /// 关闭（应用退出前调用一次；调用后插件对象将被卸载，不得再持有宿主资源）
    virtual void shutdown() = 0;
};

/// 插件接口 IID（Q_DECLARE_INTERFACE 供 QPluginLoader / qobject_cast 做类型安全转换）
#define SoulCovePluginIID "com.soulcove.plugin/1.0"
Q_DECLARE_INTERFACE(IPlugin, SoulCovePluginIID)

#endif // IPLUGIN_H
