#include "core/plugin/PluginLoader.h"

#include "Logger.hpp"

#include <QPluginLoader>

PluginRecord PluginLoader::load(const QString& filePath)
{
    PluginRecord record;
    record.filePath = filePath;

    auto loader = std::make_unique<QPluginLoader>(filePath);

    // 1. 元数据读取（JSON 文件由 Q_PLUGIN_METADATA(FILE ...) 指定）
    const QJsonObject meta = loader->metaData().value(QStringLiteral("MetaData")).toObject();
    record.name = meta.value(QStringLiteral("name")).toString();
    record.version = meta.value(QStringLiteral("version")).toString();
    record.description = meta.value(QStringLiteral("description")).toString();

    // 2. 加载动态库
    if (!loader->load()) {
        record.error = loader->errorString();
        LOG_WARN_S("PluginLoader", "load",
                   "插件加载失败: " << filePath.toStdString() << " - "
                                   << record.error.toStdString());
        return record;   // loader 析构自动释放
    }

    // 3. 接口校验 — 不是 IPlugin 实现的库直接拒载（类型安全，qobject_cast 保证 ABI 匹配）
    IPlugin* plugin = castToPlugin(*loader);
    if (!plugin) {
        record.error = QStringLiteral("库未实现 IPlugin 接口 (IID %1)").arg(SoulCovePluginIID);
        LOG_WARN_S("PluginLoader", "load",
                   "无效插件（接口不匹配）: " << filePath.toStdString());
        loader->unload();
        return record;
    }

    // 4. 元数据回退：JSON 缺失时用接口方法补齐
    if (record.name.isEmpty())     record.name = plugin->name();
    if (record.version.isEmpty())  record.version = plugin->version();
    if (record.description.isEmpty()) record.description = plugin->description();

    record.instance = plugin;
    // 释放 unique_ptr 所有权由调用方（PluginManager）接管存活期
    loader.release();
    LOG_INFO_S("PluginLoader", "load",
               "插件加载成功: " << record.name.toStdString() << " v" << record.version.toStdString()
                               << " (" << filePath.toStdString() << ")");
    return record;
}

bool PluginLoader::unload(PluginRecord& record)
{
    if (record.filePath.isEmpty()) return false;

    // 用临时 QPluginLoader 关联同一文件执行卸载（Qt 保证同路径 loader 状态共享）
    QPluginLoader loader(record.filePath);
    const bool ok = loader.unload();
    if (!ok) {
        record.error = loader.errorString();
        LOG_WARN_S("PluginLoader", "unload",
                   "插件卸载失败: " << record.filePath.toStdString() << " - "
                                   << record.error.toStdString());
    } else {
        LOG_INFO_S("PluginLoader", "unload",
                   "插件已卸载: " << record.filePath.toStdString());
    }
    record.instance = nullptr;
    return ok;
}

IPlugin* PluginLoader::castToPlugin(QPluginLoader& loader)
{
    QObject* root = loader.instance();
    if (!root) return nullptr;
    return qobject_cast<IPlugin*>(root);
}
