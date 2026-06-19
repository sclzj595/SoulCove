#include "core/shortcut/ShortcutManager.h"
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
