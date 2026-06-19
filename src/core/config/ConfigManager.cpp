#include "core/config/ConfigManager.h"
#include "Logger.hpp"
#include <QMutexLocker>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

// ========== 单例实现 ==========
ConfigManager& ConfigManager::instance()
{
    static ConfigManager s_instance;
    return s_instance;
}

ConfigManager::ConfigManager()
{
    // 确保可写配置文件存在（首次启动从资源模板拷贝）
    ensureWritableConfig();

    // 使用用户可写目录中的配置文件
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                         + "/scNotebook.ini";
    m_settings = new QSettings(configPath, QSettings::IniFormat);
    loadAll();
}

/// @brief 确保可写配置文件存在
/// 首次运行时从资源文件中的默认配置模板拷贝到用户目录
void ConfigManager::ensureWritableConfig()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QString configPath = configDir + "/scNotebook.ini";

    if (!QFile::exists(configPath)) {
        QDir().mkpath(configDir);

        // 从资源文件读取默认配置模板
        QFile defaultConfig(":/sys_param");
        if (defaultConfig.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QFile::copy(defaultConfig.fileName(), configPath);
            defaultConfig.close();
            LOG_DEBUG_S("ConfigManager", "ensureWritableConfig", "首次启动，已从资源模板创建默认配置:" << configPath);
        } else {
            LOG_DEBUG_S("ConfigManager", "ensureWritableConfig", "警告：无法读取默认配置模板，将创建空配置文件");
        }
    } else {
        LOG_DEBUG_S("ConfigManager", "ensureWritableConfig", "配置文件已存在:" << configPath);
    }

    // 设置文件权限为可读写
    QFile::setPermissions(configPath,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
}

// ========== IConfigManager 接口实现 ==========
QVariant ConfigManager::getValue(const QString& key, const QVariant& defaultValue) const
{
    QMutexLocker locker(&m_mutex);
    return m_settings->value(key, defaultValue);
}

void ConfigManager::setValue(const QString& key, const QVariant& value)
{
    QMutexLocker locker(&m_mutex);
    m_settings->setValue(key, value);
    locker.unlock();
    // 观察者通知：配置变更信号
    emit configChanged(key, value);
}

void ConfigManager::remove(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    m_settings->remove(key);
}

void ConfigManager::sync()
{
    QMutexLocker locker(&m_mutex);
    m_settings->sync();
}

void ConfigManager::loadAll()
{
    QMutexLocker locker(&m_mutex);
    LOG_DEBUG_S("ConfigManager", "loadAll", "配置加载完成，行号:"
             << m_settings->value("Display/showLineNumbers", true).toBool()
             << "字体:" << m_settings->value("Display/fontSize", 12).toInt()
             << "主题:" << m_settings->value("Display/theme", "dark").toString());
}

void ConfigManager::saveAll()
{
    sync();
    LOG_DEBUG_S("ConfigManager", "saveAll", "配置保存完成");
}

bool ConfigManager::contains(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    return m_settings->contains(key);
}

// ========== 便捷访问方法 ==========
bool ConfigManager::showLineNumbers() const
{
    return getValue("Display/showLineNumbers", true).toBool();
}

int ConfigManager::fontSize() const
{
    return getValue("Display/fontSize", 14).toInt();
}

QString ConfigManager::theme() const
{
    return getValue("Display/theme", "purple").toString();
}

bool ConfigManager::autoSave() const
{
    return getValue("Editor/autoSave", false).toBool();
}

QString ConfigManager::windowGeometry() const
{
    return getValue("Window/geometry", QString()).toString();
}

bool ConfigManager::showCompletion() const
{
    return getValue("Editor/showCompletion", true).toBool();
}

void ConfigManager::setShowLineNumbers(bool show)
{
    setValue("Display/showLineNumbers", show);
}

void ConfigManager::setFontSize(int size)
{
    setValue("Display/fontSize", size);
}

void ConfigManager::setTheme(const QString& theme)
{
    setValue("Display/theme", theme);
}

void ConfigManager::setAutoSave(bool enable)
{
    setValue("Editor/autoSave", enable);
}

void ConfigManager::setWindowGeometry(const QString& geometry)
{
    setValue("Window/geometry", geometry);
}

void ConfigManager::setShowCompletion(bool show)
{
    setValue("Editor/showCompletion", show);
}

// === 窗口最大化状态 ===
bool ConfigManager::windowMaximized() const
{
    return getValue("Window/maximized", false).toBool();
}

void ConfigManager::setWindowMaximized(bool maximized)
{
    setValue("Window/maximized", maximized);
}

// === LSP 语言服务器配置 ===

QString ConfigManager::lspPythonPath() const
{
    return getValue("LSP/pythonServer", QString()).toString();
}

QString ConfigManager::lspCppPath() const
{
    return getValue("LSP/cppServer", QString()).toString();
}

QString ConfigManager::lspJsPath() const
{
    return getValue("LSP/jsServer", QString()).toString();
}

bool ConfigManager::lspAutoStart() const
{
    return getValue("LSP/autoStart", true).toBool();
}

void ConfigManager::setLspPythonPath(const QString& path)
{
    setValue("LSP/pythonServer", path);
}

void ConfigManager::setLspCppPath(const QString& path)
{
    setValue("LSP/cppServer", path);
}

void ConfigManager::setLspJsPath(const QString& path)
{
    setValue("LSP/jsServer", path);
}

void ConfigManager::setLspAutoStart(bool enable)
{
    setValue("LSP/autoStart", enable);
}
