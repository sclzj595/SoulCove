#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include "interfaces/core/IConfigManager.h"
#include "core/base/Subject.h"
#include <QSettings>
#include <QMutex>
#include <QObject>
#include <QList>
#include <QPair>
#include <QHash>
#include <QStringList>

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

    // === C03-5: 导航栈持久化 ===
    // 注：使用 QPair<QString, QPair<int,int>> 而非 NavigationEntry，避免 ConfigManager 依赖 Widget.h
    // 序列化节点：navigation/recentStack（JSON 数组字符串形式存入 QSettings）
    /// 保存导航栈到持久化配置（上限 50 条，调用方负责截断）
    void saveNavigationStack(const QList<QPair<QString, QPair<int,int>>>& stack);
    /// 加载持久化的导航栈（返回空列表表示无历史）
    QList<QPair<QString, QPair<int,int>>> loadNavigationStack() const;

    // === C02-4 性能监控面板（调试模式）===
    bool performanceMonitorEnabled() const;  // 性能监控是否启用（默认 false，生产环境关闭）
    void setPerformanceMonitorEnabled(bool enable);

    // === P1 C05-2: 构建配置（CMake 构建设置页持久化）===
    QString qtPrefixPath() const;    // Qt 安装前缀路径（对应 CMAKE_PREFIX_PATH）
    void setQtPrefixPath(const QString& path);
    QString compilerPath() const;    // 编译器路径（MinGW/MSVC）
    void setCompilerPath(const QString& path);
    QString buildType() const;       // 构建类型：Debug / Release / RelWithDebInfo（默认 Debug）
    void setBuildType(const QString& type);
    QString cmakeExtraArgs() const;  // CMake 额外参数（如 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON）
    void setCmakeExtraArgs(const QString& args);
    QString opensslPath() const;     // OpenSSL 安装路径（对应 OPENSSL_ROOT_DIR）
    void setOpensslPath(const QString& path);

    // === P2-H04: 最近工作区列表持久化 ===
    // 存储最近打开的工作区文件路径（.scnb-workspace），上限 10 个
    /// 获取最近工作区文件路径列表（最近在前）
    QStringList recentWorkspaces() const;
    /// 添加一个工作区文件路径到最近列表（去重并提升到顶部，超过 10 个截断）
    void addRecentWorkspace(const QString& filePath);

    // === P3-M04 子项3: 调试器配置 ===
    /// 获取调试器路径（默认 "gdb"，留空使用系统 PATH 中的 gdb）
    QString debuggerPath() const;
    /// 设置调试器路径（gdb / lldb-mi 等可执行文件路径）
    void setDebuggerPath(const QString& path);

    // === P3-M05: 国际化与本地化 ===
    /// 界面语言代码：可选 "zh_CN" / "en_US" / "system"（默认 "zh_CN"）
    QString language() const;
    void setLanguage(const QString& language);
    /// UI 缩放因子：可选 100 / 125 / 150（默认 100，单位 %）
    int uiScaleFactor() const;
    void setUiScaleFactor(int factor);

    // === P3-M03 子项1: 默认行尾（EOL）配置 ===
    /// 获取默认行尾类型（"LF" / "CRLF" / "CR"，默认 "LF"）
    QString defaultEol() const;
    /// 设置默认行尾类型（新建文件时使用）
    void setDefaultEol(const QString& eol);

    // === P3-M03 子项5: 拼写检查开关 ===
    bool spellCheckEnabled() const;
    void setSpellCheckEnabled(bool enabled);

    // === P3-M02 子项1: Markdown TOC 折叠状态持久化（按文件路径记忆，上限 20 个文件）===
    // 序列化格式：mdToc/collapsedState = JSON 对象字符串
    // 每个键为文件路径，值为折叠项行号数组：{ "/path/to/file.md": [10, 25, 40] }
    /// 获取所有文件的 TOC 折叠状态映射（文件路径 → 折叠项行号列表）
    QHash<QString, QList<int>> mdTocCollapsedState() const;
    /// 保存 TOC 折叠状态映射（仅持久化最近 20 个文件，按写入顺序淘汰）
    void setMdTocCollapsedState(const QHash<QString, QList<int>>& state);

    // === P3-M02 子项2: Markdown 自定义 CSS ===
    /// 获取用户自定义 Markdown 预览 CSS（默认空字符串，叠加在主题预设之上）
    QString markdownCustomCss() const;
    /// 设置用户自定义 Markdown 预览 CSS
    void setMarkdownCustomCss(const QString& css);

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
