#include "core/shortcut/ShortcutManager.h"
#include "core/config/ConfigManager.h"
#include "Logger.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QStandardPaths>

// ============================================================
// ShortcutItem 序列化
// ============================================================

QJsonObject ShortcutItem::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("displayName")] = displayName;
    obj[QStringLiteral("category")] = category;
    obj[QStringLiteral("defaultKey")] = defaultKey.toString();
    obj[QStringLiteral("currentKey")] = currentKey.toString();
    obj[QStringLiteral("description")] = description;
    return obj;
}

ShortcutItem ShortcutItem::fromJson(const QJsonObject& json)
{
    ShortcutItem item;
    item.id = json[QStringLiteral("id")].toString();
    item.displayName = json[QStringLiteral("displayName")].toString();
    item.category = json[QStringLiteral("category")].toString();
    item.defaultKey = QKeySequence(json[QStringLiteral("defaultKey")].toString());
    item.currentKey = QKeySequence(json[QStringLiteral("currentKey")].toString(item.defaultKey.toString()));
    item.description = json[QStringLiteral("description")].toString();
    return item;
}

// ============================================================
// 单例实现 — RAII 资源管理
// ============================================================

ShortcutManager& ShortcutManager::instance()
{
    static ShortcutManager inst;  // 线程安全（C++11起）
    return inst;
}

ShortcutManager::ShortcutManager()
    : QObject(nullptr)
{
}

ShortcutManager::~ShortcutManager()
{
    // RAII：析构时自动保存配置
    if (m_initialized) {
        saveConfig();
    }
}

void ShortcutManager::initialize()
{
    if (m_initialized) return;

    // 注册所有默认快捷键
    registerDefaults();

    // 加载用户自定义配置（覆盖默认值）
    loadConfig();

    m_initialized = true;
    LOG_DEBUG_S("ShortcutManager", "initialize", "初始化完成，已注册" << m_shortcuts.size() << "个快捷键");
}

void ShortcutManager::registerDefaults()
{
    // ===== 文件操作 =====
    m_shortcuts[QStringLiteral("file.open")] = {
        QStringLiteral("file.open"),
        tr("打开文件"),
        QStringLiteral("文件"),
        QKeySequence(Qt::CTRL | Qt::Key_O),
        QKeySequence(Qt::CTRL | Qt::Key_O),
        tr("打开文件对话框")
    };

    m_shortcuts[QStringLiteral("file.save")] = {
        QStringLiteral("file.save"),
        tr("保存文件"),
        QStringLiteral("文件"),
        QKeySequence(Qt::CTRL | Qt::Key_S),
        QKeySequence(Qt::CTRL | Qt::Key_S),
        tr("保存当前文件")
    };

    m_shortcuts[QStringLiteral("file.saveAs")] = {
        QStringLiteral("file.saveAs"),
        tr("另存为..."),
        QStringLiteral("文件"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
        tr("另存为新文件")
    };

    m_shortcuts[QStringLiteral("file.new")] = {
        QStringLiteral("file.new"),
        tr("新建文件"),
        QStringLiteral("文件"),
        QKeySequence(Qt::CTRL | Qt::Key_N),
        QKeySequence(Qt::CTRL | Qt::Key_N),
        tr("创建新文件")
    };

    m_shortcuts[QStringLiteral("file.closeTab")] = {
        QStringLiteral("file.closeTab"),
        tr("关闭标签页"),
        QStringLiteral("文件"),
        QKeySequence(Qt::CTRL | Qt::Key_W),
        QKeySequence(Qt::CTRL | Qt::Key_W),
        tr("关闭当前标签页")
    };

    // ===== 编辑操作 =====
    m_shortcuts[QStringLiteral("edit.undo")] = {
        QStringLiteral("edit.undo"),
        tr("撤销"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_Z),
        QKeySequence(Qt::CTRL | Qt::Key_Z),
        tr("撤销上一步操作")
    };

    m_shortcuts[QStringLiteral("edit.redo")] = {
        QStringLiteral("edit.redo"),
        tr("重做"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_Y),
        QKeySequence(Qt::CTRL | Qt::Key_Y),
        tr("重做撤销的操作")
    };

    m_shortcuts[QStringLiteral("edit.find")] = {
        QStringLiteral("edit.find"),
        tr("查找"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_F),
        QKeySequence(Qt::CTRL | Qt::Key_F),
        tr("打开查找对话框")
    };

    m_shortcuts[QStringLiteral("edit.replace")] = {
        QStringLiteral("edit.replace"),
        tr("替换"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_H),
        QKeySequence(Qt::CTRL | Qt::Key_H),
        tr("打开替换对话框")
    };

    m_shortcuts[QStringLiteral("edit.format")] = {
        QStringLiteral("edit.format"),
        tr("格式化文档"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
        tr("格式化当前文档")
    };

    // ===== 视图操作 =====
    m_shortcuts[QStringLiteral("view.zoomIn")] = {
        QStringLiteral("view.zoomIn"),
        tr("放大字体"),
        QStringLiteral("视图"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal),
        tr("增大编辑器字体")
    };

    m_shortcuts[QStringLiteral("view.zoomOut")] = {
        QStringLiteral("view.zoomOut"),
        tr("缩小字体"),
        QStringLiteral("视图"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus),
        tr("减小编辑器字体")
    };

    // ===== 终端操作 =====
    m_shortcuts[QStringLiteral("terminal.toggle")] = {
        QStringLiteral("terminal.toggle"),
        tr("切换终端面板"),
        QStringLiteral("终端"),
        QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft),
        QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft),
        tr("显示/隐藏终端面板")
    };

    m_shortcuts[QStringLiteral("terminal.newTab")] = {
        QStringLiteral("terminal.newTab"),
        tr("新建终端标签"),
        QStringLiteral("终端"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft),
        tr("创建新的终端会话")
    };

    // ===== 全局命令 =====
    m_shortcuts[QStringLiteral("command.palette")] = {
        QStringLiteral("command.palette"),
        tr("命令面板"),
        QStringLiteral("全局"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),
        tr("打开全局命令搜索框")
    };

    // ===== P2-H05 子项1: VSCode 预设方案补充命令 =====
    // 这些命令在原项目中尚无默认绑定，注册为独立的快捷键条目，
    // 配合 applyPreset(VSCode) 使用。默认键值即采用 VSCode 兼容值，
    // 这样切换到 VSCode 预设不会因冲突导致 setShortcut 失败。
    m_shortcuts[QStringLiteral("editor.copy")] = {
        QStringLiteral("editor.copy"),
        tr("复制"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_C),
        QKeySequence(Qt::CTRL | Qt::Key_C),
        tr("复制选中内容到剪贴板")
    };

    m_shortcuts[QStringLiteral("editor.paste")] = {
        QStringLiteral("editor.paste"),
        tr("粘贴"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_V),
        QKeySequence(Qt::CTRL | Qt::Key_V),
        tr("粘贴剪贴板内容")
    };

    m_shortcuts[QStringLiteral("editor.cut")] = {
        QStringLiteral("editor.cut"),
        tr("剪切"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_X),
        QKeySequence(Qt::CTRL | Qt::Key_X),
        tr("剪切选中内容到剪贴板")
    };

    m_shortcuts[QStringLiteral("editor.gotoLine")] = {
        QStringLiteral("editor.gotoLine"),
        tr("跳转到行"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_G),
        QKeySequence(Qt::CTRL | Qt::Key_G),
        tr("跳转到指定行号")
    };

    m_shortcuts[QStringLiteral("editor.commentLine")] = {
        QStringLiteral("editor.commentLine"),
        tr("切换行注释"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_Slash),
        QKeySequence(Qt::CTRL | Qt::Key_Slash),
        tr("注释/取消注释当前行")
    };

    m_shortcuts[QStringLiteral("editor.formatDocument")] = {
        QStringLiteral("editor.formatDocument"),
        tr("格式化文档 (VSCode)"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_F),
        QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_F),
        tr("VSCode 风格格式化文档（Shift+Alt+F）")
    };

    m_shortcuts[QStringLiteral("editor.gotoDefinition")] = {
        QStringLiteral("editor.gotoDefinition"),
        tr("跳转到定义"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::Key_F12),
        QKeySequence(Qt::Key_F12),
        tr("跳转到符号定义处")
    };

    m_shortcuts[QStringLiteral("editor.gotoImplementation")] = {
        QStringLiteral("editor.gotoImplementation"),
        tr("跳转到实现"),
        QStringLiteral("编辑"),
        QKeySequence(Qt::CTRL | Qt::Key_F12),
        QKeySequence(Qt::CTRL | Qt::Key_F12),
        tr("跳转到符号实现处")
    };

    m_shortcuts[QStringLiteral("editor.quickOpen")] = {
        QStringLiteral("editor.quickOpen"),
        tr("快速打开文件"),
        QStringLiteral("全局"),
        QKeySequence(Qt::CTRL | Qt::Key_P),
        QKeySequence(Qt::CTRL | Qt::Key_P),
        tr("按文件名快速打开")
    };

    m_shortcuts[QStringLiteral("view.toggleSidebar")] = {
        QStringLiteral("view.toggleSidebar"),
        tr("切换侧边栏"),
        QStringLiteral("视图"),
        QKeySequence(Qt::CTRL | Qt::Key_B),
        QKeySequence(Qt::CTRL | Qt::Key_B),
        tr("显示/隐藏侧边栏")
    };

    m_shortcuts[QStringLiteral("view.toggleFullScreen")] = {
        QStringLiteral("view.toggleFullScreen"),
        tr("切换全屏"),
        QStringLiteral("视图"),
        QKeySequence(Qt::Key_F11),
        QKeySequence(Qt::Key_F11),
        tr("切换全屏显示")
    };

    m_shortcuts[QStringLiteral("navigation.goBack")] = {
        QStringLiteral("navigation.goBack"),
        tr("后退"),
        QStringLiteral("导航"),
        QKeySequence(Qt::ALT | Qt::Key_Left),
        QKeySequence(Qt::ALT | Qt::Key_Left),
        tr("后退到上一个光标位置")
    };

    m_shortcuts[QStringLiteral("navigation.goForward")] = {
        QStringLiteral("navigation.goForward"),
        tr("前进"),
        QStringLiteral("导航"),
        QKeySequence(Qt::ALT | Qt::Key_Right),
        QKeySequence(Qt::ALT | Qt::Key_Right),
        tr("前进到下一个光标位置")
    };
}

// ============================================================
// 公共接口
// ============================================================

QList<ShortcutItem> ShortcutManager::allShortcuts() const
{
    return m_shortcuts.values();
}

QList<ShortcutItem> ShortcutManager::shortcutsByCategory(const QString& category) const
{
    QList<ShortcutItem> result;
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        if (it.value().category == category) {
            result.append(it.value());
        }
    }
    return result;
}

QStringList ShortcutManager::categories() const
{
    QSet<QString> cats;
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        cats.insert(it.value().category);
    }
    return cats.values();
}

ShortcutItem ShortcutManager::shortcut(const QString& id) const
{
    return m_shortcuts.value(id);
}

bool ShortcutManager::setShortcut(const QString& id, const QKeySequence& newKey)
{
    if (!m_shortcuts.contains(id)) {
        LOG_WARN_S("ShortcutManager", "setShortcut", "未知的快捷键ID:" << id);
        return false;
    }

    // 检查冲突
    QStringList conflicts = checkConflict(newKey, id);
    if (!conflicts.isEmpty()) {
        LOG_DEBUG_S("ShortcutManager", "setShortcut", "快捷键冲突:" << newKey.toString() << "→" << conflicts);
        return false;  // 存在冲突
    }

    // 更新快捷键
    QKeySequence oldKey = m_shortcuts[id].currentKey;
    m_shortcuts[id].currentKey = newKey;

    emit shortcutChanged(id, oldKey, newKey);
    saveConfig();  // 自动持久化

    LOG_DEBUG_S("ShortcutManager", "setShortcut", "快捷键已修改:" << id << oldKey.toString() << "→" << newKey.toString());
    return true;
}

void ShortcutManager::resetToDefault(const QString& id)
{
    if (!m_shortcuts.contains(id)) return;

    QKeySequence oldKey = m_shortcuts[id].currentKey;
    m_shortcuts[id].currentKey = m_shortcuts[id].defaultKey;

    emit shortcutReset(id);
    emit shortcutChanged(id, oldKey, m_shortcuts[id].defaultKey);

    saveConfig();
}

void ShortcutManager::resetAllToDefault()
{
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        it->currentKey = it->defaultKey;
        emit shortcutReset(it.key());
    }

    saveConfig();
}

QStringList ShortcutManager::checkConflict(const QKeySequence& key, const QString& excludeId) const
{
    QStringList conflicts;

    if (key.isEmpty()) return conflicts;  // 空快捷键不冲突

    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        if (it.key() == excludeId) continue;  // 排除自身

        if (it.value().currentKey == key) {
            conflicts.append(it.key());
        }
    }

    return conflicts;
}

// ============================================================
// 持久化存储
// ============================================================

QString ShortcutManager::configFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);  // RAII：确保目录存在
    return dir + QStringLiteral("/shortcuts.json");
}

void ShortcutManager::saveConfig()
{
    if (!m_initialized) return;

    try {
        QJsonArray arr;
        for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
            // 只保存自定义过的快捷键（非默认值）
            if (it.value().currentKey != it.value().defaultKey) {
                arr.append(it.value().toJson());
            }
        }

        QJsonObject root;
        root[QStringLiteral("version")] = 1;
        root[QStringLiteral("customShortcuts")] = arr;

        QFile file(configFilePath());
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QJsonDocument doc(root);
            file.write(doc.toJson(QJsonDocument::Indented));
            file.close();
            LOG_DEBUG_S("ShortcutManager", "saveConfig", "配置已保存");
        } else {
            LOG_WARN_S("ShortcutManager", "saveConfig", "无法打开配置文件写入:" << file.errorString());
        }
    } catch (const std::exception& e) {
        LOG_ERROR_S("ShortcutManager", "saveConfig", "保存配置异常:" << e.what());
    }
}

void ShortcutManager::loadConfig()
{
    try {
        QFile file(configFilePath());
        if (!file.exists()) {
            LOG_DEBUG_S("ShortcutManager", "loadConfig", "配置文件不存在，使用默认值");
            return;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            LOG_WARN_S("ShortcutManager", "loadConfig", "无法打开配置文件读取:" << file.errorString());
            return;
        }

        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();

        QJsonArray customArr = root[QStringLiteral("customShortcuts")].toArray();
        int loadedCount = 0;

        for (const QJsonValue& val : customArr) {
            ShortcutItem item = ShortcutItem::fromJson(val.toObject());

            if (m_shortcuts.contains(item.id)) {
                m_shortcuts[item.id].currentKey = item.currentKey;
                loadedCount++;
            } else {
                LOG_WARN_S("ShortcutManager", "loadConfig", "加载到未注册的快捷键ID:" << item.id);
            }
        }

        LOG_DEBUG_S("ShortcutManager", "loadConfig", "已加载" << loadedCount << "个自定义快捷键");

    } catch (const std::exception& e) {
        LOG_ERROR_S("ShortcutManager", "loadConfig", "加载配置异常:" << e.what());
    }

    // P2-H05 子项1: 加载持久化的预设方案
    int presetVal = ConfigManager::instance().getValue(
        QStringLiteral("Shortcuts/preset"), static_cast<int>(ShortcutPreset::Default)).toInt();
    m_currentPreset = (presetVal == static_cast<int>(ShortcutPreset::VSCode))
                          ? ShortcutPreset::VSCode
                          : ShortcutPreset::Default;
}

void ShortcutManager::reloadConfig()
{
    // 先重置为默认值
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        it->currentKey = it->defaultKey;
    }

    // 重新加载用户配置
    loadConfig();

    LOG_DEBUG_S("ShortcutManager", "reloadConfig", "配置已重新加载");
}

// ============================================================
// P2-H05 子项1: 预设方案切换
// ============================================================

QString ShortcutManager::presetName(ShortcutPreset preset) const
{
    switch (preset) {
    case ShortcutPreset::VSCode:   return tr("VSCode 预设");
    case ShortcutPreset::Default:  [[fallthrough]];
    default:                       return tr("默认预设");
    }
}

QString ShortcutManager::resolvePresetId(const QString& specId) const
{
    // spec 风格 ID（editor.*）映射到项目中实际注册的命令 ID
    // VSCode 预设规范使用 editor.* 命名，而项目原有命令使用 file.*/edit.*/terminal.* 命名
    static const QMap<QString, QString> aliases = {
        {QStringLiteral("editor.undo"),           QStringLiteral("edit.undo")},
        {QStringLiteral("editor.redo"),           QStringLiteral("edit.redo")},
        {QStringLiteral("editor.find"),           QStringLiteral("edit.find")},
        {QStringLiteral("editor.replace"),        QStringLiteral("edit.replace")},
        {QStringLiteral("editor.save"),           QStringLiteral("file.save")},
        {QStringLiteral("editor.newFile"),        QStringLiteral("file.new")},
        {QStringLiteral("editor.openFile"),       QStringLiteral("file.open")},
        {QStringLiteral("editor.commandPalette"), QStringLiteral("command.palette")},
        {QStringLiteral("view.toggleTerminal"),   QStringLiteral("terminal.toggle")},
    };

    if (m_shortcuts.contains(specId)) return specId;
    return aliases.value(specId, specId);
}

void ShortcutManager::applyPreset(ShortcutPreset preset)
{
    // Default 预设：等价于全部重置为项目默认快捷键
    if (preset == ShortcutPreset::Default) {
        resetAllToDefault();
        m_currentPreset = ShortcutPreset::Default;
        ConfigManager::instance().setValue(
            QStringLiteral("Shortcuts/preset"), static_cast<int>(ShortcutPreset::Default));
        emit presetChanged(m_currentPreset);
        LOG_INFO_S("ShortcutManager", "applyPreset", "已切换到默认预设");
        return;
    }

    // VSCode 预设：按规范批量覆盖关键命令
    // 规范中的 editor.* ID 通过 resolvePresetId 解析为实际注册的命令 ID
    struct PresetEntry { const char* specId; Qt::KeyboardModifiers mods; int key; };
    static const PresetEntry vscodeEntries[] = {
        { "editor.copy",               Qt::ControlModifier,                       Qt::Key_C },
        { "editor.paste",              Qt::ControlModifier,                       Qt::Key_V },
        { "editor.cut",                Qt::ControlModifier,                       Qt::Key_X },
        { "editor.undo",               Qt::ControlModifier,                       Qt::Key_Z },
        { "editor.redo",               Qt::ControlModifier,                       Qt::Key_Y },
        { "editor.find",               Qt::ControlModifier,                       Qt::Key_F },
        { "editor.replace",            Qt::ControlModifier,                       Qt::Key_H },
        { "editor.gotoLine",           Qt::ControlModifier,                       Qt::Key_G },
        { "editor.commentLine",        Qt::ControlModifier,                       Qt::Key_Slash },
        { "editor.formatDocument",     Qt::ShiftModifier | Qt::AltModifier,       Qt::Key_F },
        { "editor.gotoDefinition",     Qt::NoModifier,                            Qt::Key_F12 },
        { "editor.gotoImplementation", Qt::ControlModifier,                       Qt::Key_F12 },
        { "editor.quickOpen",          Qt::ControlModifier,                       Qt::Key_P },
        { "editor.commandPalette",     Qt::ControlModifier | Qt::ShiftModifier,   Qt::Key_P },
        { "editor.save",               Qt::ControlModifier,                       Qt::Key_S },
        { "editor.newFile",            Qt::ControlModifier,                       Qt::Key_N },
        { "editor.openFile",           Qt::ControlModifier,                       Qt::Key_O },
        { "view.toggleSidebar",        Qt::ControlModifier,                       Qt::Key_B },
        { "view.toggleTerminal",       Qt::ControlModifier,                       Qt::Key_QuoteLeft },
        { "view.toggleFullScreen",     Qt::NoModifier,                            Qt::Key_F11 },
        { "navigation.goBack",         Qt::AltModifier,                           Qt::Key_Left },
        { "navigation.goForward",      Qt::AltModifier,                           Qt::Key_Right },
    };

    int appliedCount = 0;
    int skippedCount = 0;
    for (const auto& e : vscodeEntries) {
        QString actualId = resolvePresetId(QString::fromLatin1(e.specId));
        QKeySequence targetSeq(e.mods | e.key);

        if (!m_shortcuts.contains(actualId)) {
            LOG_WARN_S("ShortcutManager", "applyPreset",
                "VSCode 预设跳过未注册命令:" << e.specId << "→" << actualId);
            ++skippedCount;
            continue;
        }

        // 直接写入以绕过冲突检测（预设切换属于用户显式操作，冲突由设计保证不存在）
        // 同 spec 多条目可能解析到同一 actualId（如 editor.save→file.save 已覆盖默认），
        // 此时按顺序写入即可，最终值为最后一条
        QKeySequence oldKey = m_shortcuts[actualId].currentKey;
        m_shortcuts[actualId].currentKey = targetSeq;
        emit shortcutChanged(actualId, oldKey, targetSeq);
        ++appliedCount;
    }

    m_currentPreset = ShortcutPreset::VSCode;
    ConfigManager::instance().setValue(
        QStringLiteral("Shortcuts/preset"), static_cast<int>(ShortcutPreset::VSCode));
    saveConfig();
    emit presetChanged(m_currentPreset);

    LOG_INFO_S("ShortcutManager", "applyPreset",
        "已切换到 VSCode 预设，应用" << appliedCount << "项，跳过" << skippedCount << "项");
}

// ============================================================
// P2-H05 子项4: 导出/导入 JSON
// ============================================================

bool ShortcutManager::exportToJson(const QString& filePath) const
{
    try {
        QJsonArray arr;
        for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
            arr.append(it.value().toJson());
        }

        QJsonObject root;
        root[QStringLiteral("version")] = 1;
        root[QStringLiteral("preset")] = static_cast<int>(m_currentPreset);
        root[QStringLiteral("shortcuts")] = arr;

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            LOG_WARN_S("ShortcutManager", "exportToJson", "无法写入文件:" << file.errorString());
            return false;
        }
        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();

        LOG_INFO_S("ShortcutManager", "exportToJson", "已导出" << m_shortcuts.size() << "个快捷键到" << filePath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR_S("ShortcutManager", "exportToJson", "导出异常:" << e.what());
        return false;
    }
}

bool ShortcutManager::importFromJson(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARN_S("ShortcutManager", "importFromJson", "无法读取文件:" << file.errorString());
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        LOG_WARN_S("ShortcutManager", "importFromJson", "JSON 解析失败:" << parseError.errorString());
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray arr = root.value(QStringLiteral("shortcuts")).toArray();
    if (arr.isEmpty()) {
        // 兼容旧格式：直接是数组
        arr = doc.array();
    }

    int appliedCount = 0;
    for (const QJsonValue& val : arr) {
        if (!val.isObject()) continue;
        ShortcutItem item = ShortcutItem::fromJson(val.toObject());
        if (item.id.isEmpty() || !m_shortcuts.contains(item.id)) {
            LOG_WARN_S("ShortcutManager", "importFromJson", "跳过未注册的快捷键ID:" << item.id);
            continue;
        }
        QKeySequence oldKey = m_shortcuts[item.id].currentKey;
        m_shortcuts[item.id].currentKey = item.currentKey;
        emit shortcutChanged(item.id, oldKey, item.currentKey);
        ++appliedCount;
    }

    // 同步预设字段（若存在）
    int presetVal = root.value(QStringLiteral("preset")).toInt(
        static_cast<int>(ShortcutPreset::Default));
    m_currentPreset = (presetVal == static_cast<int>(ShortcutPreset::VSCode))
                          ? ShortcutPreset::VSCode
                          : ShortcutPreset::Default;
    ConfigManager::instance().setValue(
        QStringLiteral("Shortcuts/preset"), static_cast<int>(m_currentPreset));

    saveConfig();
    LOG_INFO_S("ShortcutManager", "importFromJson", "已导入" << appliedCount << "个快捷键");
    return true;
}

QStringList ShortcutManager::checkImportConflicts(const QString& filePath) const
{
    QStringList result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result << QStringLiteral("PARSE_ERROR");
        return result;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        result << QStringLiteral("PARSE_ERROR");
        return result;
    }

    QJsonObject root = doc.object();
    QJsonArray arr = root.value(QStringLiteral("shortcuts")).toArray();
    if (arr.isEmpty()) arr = doc.array();

    // 收集本次导入会修改的 (id → key) 映射，模拟冲突检测
    QMap<QString, QKeySequence> importedKeys;
    for (const QJsonValue& val : arr) {
        if (!val.isObject()) continue;
        ShortcutItem item = ShortcutItem::fromJson(val.toObject());
        if (item.id.isEmpty() || !m_shortcuts.contains(item.id)) continue;
        importedKeys[item.id] = item.currentKey;
    }

    // 对每条导入项，检查与「其它已注册命令（不含自身和其它导入项）」的冲突
    for (auto it = importedKeys.constBegin(); it != importedKeys.constEnd(); ++it) {
        const QString& id = it.key();
        const QKeySequence& key = it.value();
        if (key.isEmpty()) continue;

        for (auto mit = m_shortcuts.constBegin(); mit != m_shortcuts.constEnd(); ++mit) {
            if (mit.key() == id) continue;            // 排除自身
            if (importedKeys.contains(mit.key())) continue;  // 排除其它导入项

            if (mit.value().currentKey == key) {
                result << QStringLiteral("%1 ← %2 (与 %3 冲突)")
                              .arg(id, key.toString(), mit.value().displayName);
                break;
            }
        }
    }

    return result;
}
