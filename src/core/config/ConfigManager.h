#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include "interfaces/core/IConfigManager.h"
#include "core/base/Subject.h"
#include <QSettings>
#include <QMutex>
#include <QObject>

/// @brief 配置管理器 - 线程安全单例
/// 基于IConfigManager接口实现，全局唯一实例，统一管理所有配置参数
/// 首次启动从资源文件加载默认值，后续读写使用用户可写目录
/// 配置变更时发射 configChanged 信号，UI组件可监听自动刷新
class ConfigManager : public QObject, public IConfigManager
{
    Q_OBJECT

public:
    /// 获取全局单例实例
    static ConfigManager& instance();

    // 禁止拷贝和赋值
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // === IConfigManager 接口实现 ===
    QVariant getValue(const QString& key, const QVariant& defaultValue = QVariant()) const override;
    void setValue(const QString& key, const QVariant& value) override;
    void remove(const QString& key);
    void sync() override;
    void loadAll() override;
    void saveAll() override;
    bool contains(const QString& key) const override;

    // === 便捷访问方法（消除硬编码魔法值）===
    bool showLineNumbers() const;
    int fontSize() const;
    QString theme() const;
    bool autoSave() const;
    QString windowGeometry() const;
    bool showCompletion() const;

    void setShowLineNumbers(bool show);
    void setFontSize(int size);
    void setTheme(const QString& theme);
    void setAutoSave(bool enable);
    void setWindowGeometry(const QString& geometry);
    void setShowCompletion(bool show);

    // === 窗口最大化状态 ===
    bool windowMaximized() const;
    void setWindowMaximized(bool maximized);

    // === LSP 语言服务器配置 ===
    QString lspPythonPath() const;   // Python 语言服务器路径 (pylsp)
    QString lspCppPath() const;      // C++ 语言服务器路径 (clangd)
    QString lspJsPath() const;       // JS/TS 语言服务器路径 (typescript-language-server)
    bool lspAutoStart() const;       // 打开文件时是否自动启动对应语言服务器
    void setLspPythonPath(const QString& path);
    void setLspCppPath(const QString& path);
    void setLspJsPath(const QString& path);
    void setLspAutoStart(bool enable);

signals:
    /// 配置项变更信号（观察者模式，Qt信号槽落地）
    void configChanged(const QString& key, const QVariant& value);

private:
    ConfigManager();
    ~ConfigManager() override = default;

    /// 从资源文件（默认配置模板）拷贝到可写路径
    void ensureWritableConfig();

    QSettings* m_settings;
    mutable QMutex m_mutex;
};

#endif // CONFIGMANAGER_H
