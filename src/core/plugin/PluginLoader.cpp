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
    // 所有权移交：真正执行 load() 的 loader 指针交由调用方（PluginManager）接管存活期，
    // 实例指针的有效性依赖它；卸载时必须用它（而非临时实例）执行 unload()
    record.loader = loader.release();
    LOG_INFO_S("PluginLoader", "load",
               "插件加载成功: " << record.name.toStdString() << " v" << record.version.toStdString()
                               << " (" << filePath.toStdString() << ")");
    return record;
}

bool PluginLoader::unload(PluginRecord& record)
{
    if (record.filePath.isEmpty()) return false;

    record.instance = nullptr;

    // 优先用真正执行过 load() 的 loader（PluginManager::m_loaders 持有）；
    // 记录未携带 loader（加载失败的记录）时无库可卸载，直接返回。
    QPluginLoader* loaderPtr = record.loader;
    record.loader = nullptr;
    if (!loaderPtr) return false;

    const bool ok = loaderPtr->unload();
    if (!ok) {
        record.error = loaderPtr->errorString();
        LOG_WARN_S("PluginLoader", "unload",
                   "插件卸载失败: " << record.filePath.toStdString() << " - "
                                   << record.error.toStdString());
    } else {
        LOG_INFO_S("PluginLoader", "unload",
                   "插件已卸载: " << record.filePath.toStdString());
    }
    return ok;
}

IPlugin* PluginLoader::castToPlugin(QPluginLoader& loader)
{
    QObject* root = loader.instance();
    if (!root) return nullptr;
    return qobject_cast<IPlugin*>(root);
}
