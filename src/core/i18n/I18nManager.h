#ifndef I18NMANAGER_H
#define I18NMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QLocale>
#include <QTranslator>
#include <QScopedPointer>

/// @brief 国际化与本地化管理器（单例）
///
/// 职责：
/// - 启动时根据 ConfigManager::language() 加载对应 .qm 翻译文件
/// - 运行时切换界面语言（QTranslator 热插拔）
/// - 应用对应 QLocale（影响日期/时间/数字格式）
/// - 扫描可用 .qm 文件列表，供 UI 菜单列出可选项
///
/// 设计要点：
/// - 单例 + QObject，便于信号槽联动
/// - 持有 QTranslator 指针，切换语言时先 removeTranslator 再 installTranslator
/// - .qm 文件查找顺序：Qt 资源(:/i18n/) → 可执行文件目录/i18n/
class I18nManager : public QObject
{
    Q_OBJECT

public:
    /// 获取全局单例实例
    static I18nManager& instance();

    // 禁止拷贝和赋值
    I18nManager(const I18nManager&) = delete;
    I18nManager& operator=(const I18nManager&) = delete;

    /// @brief 启动时调用 — 读取配置语言并加载翻译、应用 QLocale
    /// 应在 QApplication 创建后、主窗口构造前调用
    void initialize();

    /// @brief 切换界面语言
    /// @param langCode 语言代码（"zh_CN" / "en_US" / "system"）
    /// 切换会立即重新安装 QTranslator 并应用 QLocale；
    /// 已构造的 UI 需要重启或手动 retranslate 才能完全生效
    void switchLanguage(const QString& langCode);

    /// @brief 当前实际生效的语言代码（已解析 "system" → 具体代码）
    QString currentLanguage() const;

    /// @brief 扫描可用语言代码列表
    /// 优先扫描 Qt 资源 :/i18n/，回退到可执行文件目录的 i18n/ 子目录
    /// @return 语言代码列表（如 ["zh_CN", "en_US"]），始终包含当前语言
    QStringList availableLanguages() const;

    /// @brief 根据当前语言返回对应的 QLocale
    /// "zh_CN" → QLocale::Chinese, "en_US" → QLocale::English
    QLocale currentLocale() const;

    /// @brief 应用当前 QLocale 到 Qt 全局默认
    /// 调用 QLocale::setDefault(currentLocale())
    /// 影响所有使用 QLocale::currentLocale() 的格式化输出
    void applyLocale();

signals:
    /// 语言切换完成信号（UI 可监听后做 retranslate 或提示重启）
    void languageChanged(const QString& langCode);

private:
    I18nManager() = default;
    ~I18nManager() override = default;

    /// 解析 "system" → 系统语言代码，并校验 .qm 是否存在
    QString resolveLanguage(const QString& langCode) const;

    /// 在给定目录中查找 SoulCove_<lang>.qm 文件
    bool loadTranslator(const QString& langCode);

    /// 扫描指定目录下的 SoulCove_*.qm 文件，返回语言代码列表
    static QStringList scanQmFiles(const QString& dirPath);

    QScopedPointer<QTranslator> m_translator;
    QString m_currentLanguage;  ///< 当前实际生效的语言代码（不含 "system"）
    bool m_initialized = false;
};

#endif // I18NMANAGER_H
