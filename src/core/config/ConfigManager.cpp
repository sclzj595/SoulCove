#include "core/config/ConfigManager.h"
#include "Logger.hpp"
#include <QMutexLocker>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

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
                         + "/SoulCove.ini";
    m_settings = new QSettings(configPath, QSettings::IniFormat);
    loadAll();
}

/// @brief 确保可写配置文件存在
/// 首次运行时从资源文件中的默认配置模板拷贝到用户目录
void ConfigManager::ensureWritableConfig()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QString configPath = configDir + "/SoulCove.ini";

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

// === C03-5: 导航栈持久化 ===
// 序列化格式：navigation/recentStack = JSON 数组字符串
// 每个元素：{ "file": "<path>", "line": <int>, "col": <int> }
// 选择 JSON 字符串而非 QSettings 数组：单键值存储便于跨版本兼容与人工调试查看

void ConfigManager::saveNavigationStack(const QList<QPair<QString, QPair<int,int>>>& stack)
{
    QJsonArray arr;
    for (const auto& entry : stack) {
        QJsonObject obj;
        obj.insert(QStringLiteral("file"), entry.first);
        obj.insert(QStringLiteral("line"), entry.second.first);
        obj.insert(QStringLiteral("col"), entry.second.second);
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    setValue("navigation/recentStack", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

QList<QPair<QString, QPair<int,int>>> ConfigManager::loadNavigationStack() const
{
    QList<QPair<QString, QPair<int,int>>> result;
    QString jsonStr = getValue("navigation/recentStack", QString()).toString();
    if (jsonStr.isEmpty()) return result;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        LOG_DEBUG_S("ConfigManager", "loadNavigationStack",
                    "解析导航栈 JSON 失败: " << err.errorString().toStdString());
        return result;
    }
    const QJsonArray arr = doc.array();
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        QJsonObject obj = v.toObject();
        QString file = obj.value(QStringLiteral("file")).toString();
        int line = obj.value(QStringLiteral("line")).toInt(1);
        int col = obj.value(QStringLiteral("col")).toInt(1);
        if (file.isEmpty()) continue;
        result.append({ file, { line, col } });
    }
    return result;
}

// === C02-4 性能监控面板（调试模式）===

bool ConfigManager::performanceMonitorEnabled() const
{
    // 默认 false，生产环境关闭，避免影响性能
    return getValue("Debug/performanceMonitor", false).toBool();
}

void ConfigManager::setPerformanceMonitorEnabled(bool enable)
{
    setValue("Debug/performanceMonitor", enable);
}

// === P1 C05-2: 构建配置 ===

QString ConfigManager::qtPrefixPath() const
{
    // 对应 CMAKE_PREFIX_PATH，留空则使用环境变量
    return getValue("Build/qtPath", QString()).toString();
}

void ConfigManager::setQtPrefixPath(const QString& path)
{
    setValue("Build/qtPath", path);
}

QString ConfigManager::compilerPath() const
{
    // 编译器路径（MinGW/MSVC），留空则使用系统默认
    return getValue("Build/compilerPath", QString()).toString();
}

void ConfigManager::setCompilerPath(const QString& path)
{
    setValue("Build/compilerPath", path);
}

QString ConfigManager::buildType() const
{
    // 默认 Debug（与 CMake 单配置生成器 MinGW Makefiles 默认行为一致）
    return getValue("Build/buildType", QStringLiteral("Debug")).toString();
}

void ConfigManager::setBuildType(const QString& type)
{
    setValue("Build/buildType", type);
}

QString ConfigManager::cmakeExtraArgs() const
{
    // CMake 额外参数，如 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    return getValue("Build/cmakeExtraArgs", QString()).toString();
}

void ConfigManager::setCmakeExtraArgs(const QString& args)
{
    setValue("Build/cmakeExtraArgs", args);
}

QString ConfigManager::opensslPath() const
{
    // 对应 OPENSSL_ROOT_DIR，留空则使用环境变量
    return getValue("Build/openSslPath", QString()).toString();
}

void ConfigManager::setOpensslPath(const QString& path)
{
    setValue("Build/openSslPath", path);
}

// === P2-H04: 最近工作区列表持久化 ===
// 存储格式：workspace/recent = JSON 数组字符串（与导航栈一致的存储策略）
// 每个元素为工作区文件绝对路径，最近在前，上限 10 个

QStringList ConfigManager::recentWorkspaces() const
{
    QStringList result;
    QString jsonStr = getValue("workspace/recent", QString()).toString();
    if (jsonStr.isEmpty()) return result;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        LOG_DEBUG_S("ConfigManager", "recentWorkspaces",
                    "解析最近工作区 JSON 失败: " << err.errorString().toStdString());
        return result;
    }
    const QJsonArray arr = doc.array();
    for (const QJsonValue& v : arr) {
        if (v.isString()) {
            QString path = v.toString();
            if (!path.isEmpty()) result.append(path);
        }
    }
    return result;
}

void ConfigManager::addRecentWorkspace(const QString& filePath)
{
    if (filePath.isEmpty()) return;

    QStringList recent = recentWorkspaces();
    // 去重并提升到顶部
    recent.removeAll(filePath);
    recent.prepend(filePath);
    // 上限 10 个
    while (recent.size() > 10) {
        recent.removeLast();
    }

    QJsonArray arr;
    for (const QString& p : recent) {
        arr.append(p);
    }
    QJsonDocument doc(arr);
    setValue("workspace/recent", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

// === P3-M04 子项3: 调试器配置 ===
// 默认 "gdb"，留空时 DebugManager 回退到 "gdb"（依赖系统 PATH 解析）

QString ConfigManager::debuggerPath() const
{
    return getValue("Debug/debuggerPath", QStringLiteral("gdb")).toString();
}

void ConfigManager::setDebuggerPath(const QString& path)
{
    setValue("Debug/debuggerPath", path);
}

// === P3-M05: 国际化与本地化 ===
// 语言代码存储键: app/language；默认 "zh_CN"，可选 "zh_CN" / "en_US" / "system"
// UI 缩放因子存储键: Display/uiScaleFactor；默认 100，可选 100 / 125 / 150

QString ConfigManager::language() const
{
    // "system" 表示跟随系统语言（由 I18nManager 解析为具体语言代码）
    return getValue("app/language", QStringLiteral("zh_CN")).toString();
}

void ConfigManager::setLanguage(const QString& language)
{
    setValue("app/language", language);
}

int ConfigManager::uiScaleFactor() const
{
    // 默认 100%，可选 100 / 125 / 150（超出范围兜底为 100）
    int factor = getValue("Display/uiScaleFactor", 100).toInt();
    if (factor != 100 && factor != 125 && factor != 150) {
        factor = 100;
    }
    return factor;
}

void ConfigManager::setUiScaleFactor(int factor)
{
    // 规范化到合法值集合，避免外部传入非法值
    if (factor != 100 && factor != 125 && factor != 150) {
        factor = 100;
    }
    setValue("Display/uiScaleFactor", factor);
}

// === P3-M03 子项1: 默认行尾（EOL）配置 ===

QString ConfigManager::defaultEol() const
{
    // 默认 LF（跨平台一致），可选值: "LF" / "CRLF" / "CR"
    return getValue("Editor/defaultEol", QStringLiteral("LF")).toString();
}

void ConfigManager::setDefaultEol(const QString& eol)
{
    setValue("Editor/defaultEol", eol);
}

// === P3-M03 子项5: 拼写检查开关 ===

bool ConfigManager::spellCheckEnabled() const
{
    // 默认关闭（拼写检查会增加 paintEvent 开销）
    return getValue("Editor/spellCheck", false).toBool();
}

void ConfigManager::setSpellCheckEnabled(bool enabled)
{
    setValue("Editor/spellCheck", enabled);
}

// === P3-M02 子项1: Markdown TOC 折叠状态持久化 ===
// 存储格式：mdToc/collapsedState = JSON 对象字符串
// { "/path/file1.md": [10, 25], "/path/file2.md": [40] }
// 仅保留最近 20 个文件（按 QHash 迭代顺序，写入时淘汰最旧）
// 使用 JSON 字符串单键存储，便于跨版本兼容与人工调试

QHash<QString, QList<int>> ConfigManager::mdTocCollapsedState() const
{
    QHash<QString, QList<int>> result;
    QString jsonStr = getValue("mdToc/collapsedState", QString()).toString();
    if (jsonStr.isEmpty()) return result;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_DEBUG_S("ConfigManager", "mdTocCollapsedState",
                    "解析 TOC 折叠状态 JSON 失败: " << err.errorString().toStdString());
        return result;
    }
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonValue& v = it.value();
        if (!v.isArray()) continue;
        QList<int> lines;
        const QJsonArray arr = v.toArray();
        for (const QJsonValue& lv : arr) {
            if (lv.isDouble()) lines.append(lv.toInt());
        }
        result.insert(it.key(), lines);
    }
    return result;
}

void ConfigManager::setMdTocCollapsedState(const QHash<QString, QList<int>>& state)
{
    // 仅持久化最近 20 个文件：QHash 迭代顺序不保证，按 JSON 写入顺序截断
    // （此处采用 QHash 自然迭代，写入时超过 20 个文件则丢弃尾部）
    QJsonObject obj;
    int count = 0;
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        if (count >= 20) break;  // 上限 20 个文件
        QJsonArray arr;
        for (int line : it.value()) arr.append(line);
        obj.insert(it.key(), arr);
        ++count;
    }
    QJsonDocument doc(obj);
    setValue("mdToc/collapsedState", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

// === P3-M02 子项2: Markdown 自定义 CSS ===
// 存储键：Markdown/customCss；默认空字符串
// 由 MarkdownMode 加载并叠加在主题预设 CSS 之上（用户 CSS 优先级更高）

QString ConfigManager::markdownCustomCss() const
{
    return getValue("Markdown/customCss", QString()).toString();
}

void ConfigManager::setMarkdownCustomCss(const QString& css)
{
    setValue("Markdown/customCss", css);
}
