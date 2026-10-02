#ifndef PLUGINLOADER_H
#define PLUGINLOADER_H

#include "interfaces/plugin/IPlugin.h"

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <memory>

class QPluginLoader;

/// @brief 单个插件的加载结果记录（M7 四件套之二）
struct PluginRecord
{
    QString filePath;                 ///< 动态库完整路径
    QString name;                     ///< 插件标识（取自元数据，缺失时回退接口 name()）
    QString version;                  ///< 版本（元数据）
    QString description;              ///< 描述（元数据）
    QString error;                    ///< 加载/校验失败原因（成功时为空）
    IPlugin* instance = nullptr;      ///< 插件实例（loader 存活期间有效）
    bool initialized = false;         ///< initialize() 是否成功

    bool isValid() const { return instance != nullptr && error.isEmpty(); }
};

/// @brief 插件加载器（封装 QPluginLoader 的 RAII 包装）
///
/// 职责单一：把一个动态库变成可用的 IPlugin 实例，或把它安全卸载。
/// 元数据（name/version/description）优先读 JSON 元数据文件，
/// 缺失时回退到插件接口方法，保证最小合规插件也能加载。
class PluginLoader
{
public:
    PluginLoader() = default;

    /// 加载指定路径的插件动态库
    /// @param filePath 动态库路径（*.dll / *.so / *.dylib）
    /// @return 记录（error 非空表示失败；instance 非空表示可用）
    PluginRecord load(const QString& filePath);

    /// 卸载插件（先置空实例指针再卸载动态库）
    /// @param record load() 返回的记录；成功卸载后 instance 置空
    /// @return 卸载是否成功（false 时记录 error）
    bool unload(PluginRecord& record);

private:
    /// 插件必须实现 IPlugin 接口，否则视为无效宿主库并立即卸载
    static IPlugin* castToPlugin(QPluginLoader& loader);
};

#endif // PLUGINLOADER_H
