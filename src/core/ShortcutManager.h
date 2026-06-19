#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QObject>
#include <QString>
#include <QKeySequence>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <functional>

/// @brief 快捷键条目
struct ShortcutItem {
    QString id;              ///< 唯一标识符（如 "file.open"）
    QString displayName;     ///< 显示名称（如 "打开文件"）
    QString category;        ///< 分类（如 "文件"、"编辑"）
    QKeySequence defaultKey; ///< 默认快捷键
    QKeySequence currentKey; ///< 当前用户自定义的快捷键
    QString description;     ///< 功能描述

    /// @brief 序列化为 JSON
    QJsonObject toJson() const;

    /// @brief 从 JSON 反序列化
    static ShortcutItem fromJson(const QJsonObject& json);
};

/// @brief 快捷键管理器 — 单例模式，RAII资源管理
///
/// 职责：
/// - 管理所有可配置的快捷键
/// - 提供默认快捷键注册表
/// - 冲突检测与解决
/// - 持久化存储（JSON格式）
/// - 运行时动态修改快捷键绑定
///
/// RAII保证：
/// - 析构时自动保存配置
/// - 异常安全的文件操作
class ShortcutManager : public QObject
{
    Q_OBJECT

public:
    /// @brief 获取单例实例
    static ShortcutManager& instance();

    // 禁用拷贝（单例）
    ShortcutManager(const ShortcutManager&) = delete;
    ShortcutManager& operator=(const ShortcutManager&) = delete;

    /// @brief 初始化管理器（加载配置 + 注册默认快捷键）
    void initialize();

    /// @brief 获取所有已注册的快捷键
    QList<ShortcutItem> allShortcuts() const;

    /// @brief 获取指定分类的快捷键
    QList<ShortcutItem> shortcutsByCategory(const QString& category) const;

    /// @brief 获取所有分类列表
    QStringList categories() const;

    /// @brief 根据 ID 获取快捷键
    ShortcutItem shortcut(const QString& id) const;

    /// @brief 修改快捷键
    /// @param id 快捷键ID
    /// @param newKey 新的快捷键序列
    /// @return 是否成功（失败原因：冲突或无效）
    bool setShortcut(const QString& id, const QKeySequence& newKey);

    /// @brief 重置为默认快捷键
    void resetToDefault(const QString& id);

    /// @brief 重置所有快捷键为默认值
    void resetAllToDefault();

    /// @brief 检测快捷键冲突
    /// @param key 要检查的快捷键
    /// @param excludeId 排除的ID（修改自身时不与自己冲突）
    /// @return 冲突的快捷键ID列表（空表示无冲突）
    QStringList checkConflict(const QKeySequence& key, const QString& excludeId = QString()) const;

    /// @brief 保存配置到文件
    void saveConfig();

    /// @brief 从文件重新加载配置
    void reloadConfig();

signals:
    /// @brief 快捷键已修改
    void shortcutChanged(const QString& id, const QKeySequence& oldKey, const QKeySequence& newKey);

    /// @brief 快捷键重置为默认值
    void shortcutReset(const QString& id);

private:
    ShortcutManager();
    ~ShortcutManager() override;

    /// @brief 注册默认快捷键
    void registerDefaults();

    /// @brief 加载用户配置
    void loadConfig();

    /// @brief 获取配置文件路径
    QString configFilePath() const;

    QMap<QString, ShortcutItem> m_shortcuts;  ///< ID → 快捷键映射
    bool m_initialized = false;
};

#endif // SHORTCUTMANAGER_H
