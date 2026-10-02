#ifndef IPLUGINAPI_H
#define IPLUGINAPI_H

#include <QString>
#include <functional>

/// @brief 宿主暴露给插件的能力接口（M7 插件系统四件套之一）
///
/// 由 PluginAPI 实现、PluginManager 在插件 initialize() 时传入。
/// 插件只允许通过此接口访问宿主能力（依赖倒置，插件不链接宿主内部模块）。
///
/// v1 能力范围：
/// - 统一日志
/// - 注册命令（接入 CommandRegistry，命令面板可触发）
/// - 插件私有数据目录
/// - 宿主版本查询
///
/// 后续扩展（保持向后兼容，只加不改）：
/// - 文档访问（当前编辑器内容只读）
/// - UI 面板注册
/// - 事件订阅（文件打开/保存等）
class IPluginAPI
{
public:
    virtual ~IPluginAPI() = default;

    /// 写入统一日志系统（前缀 "Plugin/<name>"）
    virtual void log(const QString& message) = 0;

    /// 注册命令（接入宿主 CommandRegistry，命令面板可搜索触发）
    /// @param id 命令ID，约定 "plugin.<插件名>.<动作>"（如 "plugin.hello.greet"）
    /// @param description 命令描述（命令面板展示）
    /// @param handler 回调（在主线程执行）
    /// @return false 表示注册失败（如命令ID已被占用）
    virtual bool registerCommand(const QString& id, const QString& description,
                                 std::function<void()> handler) = 0;

    /// 插件私有可写数据目录（不存在时自动创建）
    /// @param pluginName 插件唯一标识（name()）
    virtual QString pluginDataDir(const QString& pluginName) const = 0;

    /// 宿主应用版本
    virtual QString applicationVersion() const = 0;
};

#endif // IPLUGINAPI_H
