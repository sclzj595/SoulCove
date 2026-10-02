#include "core/plugin/PluginManager.h"

#include "core/plugin/PluginAPI.h"
#include "controller/CommandRegistry.h"
#include "Logger.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>

PluginManager& PluginManager::instance()
{
    static PluginManager inst;
    return inst;
}

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager()
{
    shutdownAll();
}

void PluginManager::setCommandRegistry(CommandRegistry* registry)
{
    ensureApi();
    m_api->setCommandRegistry(registry);
}

void PluginManager::setDocumentProvider(std::function<IPluginAPI::DocumentInfo()> provider)
{
    ensureApi();
    m_api->setDocumentProvider(std::move(provider));
}

void PluginManager::dispatchEvent(const QString& event, const QVariant& data)
{
    if (m_api && !m_records.isEmpty()) {
        m_api->dispatchEvent(event, data);
    }
}

void PluginManager::ensureApi()
{
    if (!m_api) {
        // API 在首次注入注册表或首次加载插件时惰性创建（需要应用版本号）
        m_api = std::make_unique<PluginAPI>(QCoreApplication::applicationVersion());
    }
}

QStringList PluginManager::libraryNameFilters()
{
    return { QStringLiteral("*.dll"), QStringLiteral("*.so"), QStringLiteral("*.dylib") };
}

int PluginManager::loadPlugins(const QString& dir)
{
    const QString pluginDir = dir.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/plugins")
        : dir;

    QDir d(pluginDir);
    if (!d.exists()) {
        LOG_DEBUG_S("PluginManager", "loadPlugins",
                    "插件目录不存在，跳过加载: " << pluginDir.toStdString());
        return 0;
    }

    const QStringList files = d.entryList(libraryNameFilters(), QDir::Files);
    if (files.isEmpty()) {
        LOG_DEBUG_S("PluginManager", "loadPlugins",
                    "插件目录为空: " << pluginDir.toStdString());
        return 0;
    }

    if (!m_api) {
        m_api = std::make_unique<PluginAPI>(QCoreApplication::applicationVersion());
    }

    int okCount = 0;
    for (const QString& file : files) {
        const QString filePath = d.absoluteFilePath(file);

        // 重复加载保护（同一文件路径只加载一次）
        if (m_loaders.contains(filePath)) {
            LOG_DEBUG_S("PluginManager", "loadPlugins",
                        "插件已加载，跳过: " << filePath.toStdString());
            continue;
        }

        PluginRecord record = PluginLoader().load(filePath);
        m_loaders[filePath] = new QPluginLoader(filePath);   // 保持 loader 存活，实例指针才有效

        if (!record.isValid()) {
            record.error = record.error.isEmpty() ? QStringLiteral("加载失败") : record.error;
            LOG_WARN_S("PluginManager", "loadPlugins",
                       "插件不可用: " << filePath.toStdString() << " - "
                                     << record.error.toStdString());
            emit pluginFailed(record.name.isEmpty() ? file : record.name, record.error);
            m_records.push_back(std::move(record));
            continue;
        }

        // 初始化（错误隔离：单个插件失败不影响其余插件）
        // v1.1: 设置所有者上下文 — initialize() 期间的命令/订阅注册归属该插件
        m_api->setCurrentPluginOwner(record.name);
        if (record.instance->initialize(m_api.get())) {
            record.initialized = true;
            ++okCount;
            LOG_INFO_S("PluginManager", "loadPlugins",
                       "插件初始化成功: " << record.name.toStdString() << " v"
                                         << record.version.toStdString());
            emit pluginLoaded(record.name, record.version);
        } else {
            record.error = QStringLiteral("initialize() 返回 false");
            LOG_WARN_S("PluginManager", "loadPlugins",
                       "插件初始化失败: " << record.name.toStdString());
            emit pluginFailed(record.name, record.error);
        }

        m_records.push_back(std::move(record));
    }

    LOG_INFO_S("PluginManager", "loadPlugins",
               "插件加载完成: " << okCount << "/" << files.size() << " 成功");
    return okCount;
}

void PluginManager::shutdownAll()
{
    if (m_records.isEmpty()) return;

    // 逆序 shutdown（后加载的插件先关闭，降低依赖顺序风险）
    // v1.1: shutdown 前先移除该插件注册的命令与事件订阅（防悬垂回调）
    PluginLoader loader;
    for (auto it = m_records.rbegin(); it != m_records.rend(); ++it) {
        if (it->initialized && it->instance) {
            if (m_api) m_api->removePluginRegistrations(it->name);
            it->instance->shutdown();
            it->initialized = false;
            LOG_DEBUG_S("PluginManager", "shutdownAll",
                        "插件已关闭: " << it->name.toStdString());
        }
    }
    for (auto& rec : m_records) {
        loader.unload(rec);
    }
    qDeleteAll(m_loaders);
    m_loaders.clear();
    m_records.clear();
    LOG_INFO_S("PluginManager", "shutdownAll", "全部插件已卸载");
}

IPlugin* PluginManager::plugin(const QString& name) const
{
    for (const auto& rec : m_records) {
        if (rec.name == name && rec.instance) return rec.instance;
    }
    return nullptr;
}

bool PluginManager::isInitialized(const QString& name) const
{
    for (const auto& rec : m_records) {
        if (rec.name == name) return rec.initialized;
    }
    return false;
}
