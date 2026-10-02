#ifndef IPLUGINAPI_H
#define IPLUGINAPI_H

#include <QString>
#include <QVariant>
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
/// v1.1 新增：
/// - 文档访问（当前编辑器只读快照）
/// - 事件订阅（fileOpened / fileSaved / fileClosed / encodingChanged）
///
/// 后续扩展（保持向后兼容，只加不改）：
/// - UI 面板注册
/// - 插件市场元数据格式
class IPluginAPI
{
public:
    virtual ~IPluginAPI() = default;

    /// 当前文档只读快照
    struct DocumentInfo {
        QString path;   ///< 文件完整路径（无打开文件时为空）
        QString text;   ///< 编辑器当前全文
        bool valid = false;  ///< 是否存在有效的当前文档
    };

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

    // ===== v1.1: 文档访问与事件订阅 =====

    /// 获取当前文档只读快照（无打开文件时 valid=false）
    /// 注意：text 为全文档拷贝，插件侧应避免高频轮询
    virtual DocumentInfo currentDocument() const = 0;

    /// 订阅宿主事件（插件 shutdown 时自动退订，无需手动清理）
    /// @param event 事件名：fileOpened / fileSaved / fileClosed / encodingChanged
    /// @param handler 回调（主线程执行；data 为事件负载数据）
    /// @return 订阅ID（>0 成功；<=0 失败，如事件名不支持）
    virtual int subscribeEvent(const QString& event,
                               std::function<void(const QVariant& data)> handler) = 0;

    /// 退订事件
    /// @return true 表示订阅存在并已移除
    virtual bool unsubscribeEvent(int subscriptionId) = 0;
};

#endif // IPLUGINAPI_H
