#include "core/i18n/I18nManager.h"
#include "core/config/ConfigManager.h"
#include "Logger.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QLocale>

// ========== 单例实现 ==========
I18nManager& I18nManager::instance()
{
    static I18nManager s_instance;
    return s_instance;
}

// ========== 公共方法 ==========

void I18nManager::initialize()
{
    if (m_initialized) return;

    QString lang = ConfigManager::instance().language();
    QString resolved = resolveLanguage(lang);

    // 加载翻译文件（失败不阻断启动，回退到源码中的中文）
    if (loadTranslator(resolved)) {
        m_currentLanguage = resolved;
        LOG_INFO_S("I18nManager", "initialize",
                   "已加载翻译: " << resolved.toStdString());
    } else {
        // .qm 加载失败时，仍记录"语言代码"，便于 UI 显示当前选择
        m_currentLanguage = resolved;
        LOG_INFO_S("I18nManager", "initialize",
                   "翻译加载失败，使用源码默认语言: " << resolved.toStdString());
    }

    applyLocale();
    m_initialized = true;
}

void I18nManager::switchLanguage(const QString& langCode)
{
    QString resolved = resolveLanguage(langCode);
    if (resolved == m_currentLanguage && m_translator) {
        // 同语言无需重复切换，但仍发出信号便于 UI 反馈
        emit languageChanged(resolved);
        return;
    }

    // 先卸载旧翻译器（避免多翻译器叠加造成 key 冲突）
    if (m_translator) {
        qApp->removeTranslator(m_translator.data());
        m_translator.reset();
    }

    if (loadTranslator(resolved)) {
        m_currentLanguage = resolved;
        LOG_INFO_S("I18nManager", "switchLanguage",
                   "切换语言成功: " << resolved.toStdString());
    } else {
        // 加载失败也记录语言代码（保持配置一致性）
        m_currentLanguage = resolved;
        LOG_INFO_S("I18nManager", "switchLanguage",
                   "翻译文件加载失败，回退源码默认: " << resolved.toStdString());
    }

    applyLocale();
    emit languageChanged(resolved);
}

QString I18nManager::currentLanguage() const
{
    return m_currentLanguage;
}

QStringList I18nManager::availableLanguages() const
{
    QStringList langs;

    // 优先扫描 Qt 资源 :/i18n/（SoulCove IDE/SoulCove Notebook/SoulCove Notebook Lite 内嵌）
    langs += scanQmFiles(QStringLiteral(":/i18n"));

    // 回退扫描可执行文件目录的 i18n/ 子目录（开发/调试部署用）
    QString exeDir = QCoreApplication::applicationDirPath() + QStringLiteral("/i18n");
    langs += scanQmFiles(exeDir);

    // 去重并保证当前语言始终在列表中（仅在已初始化时）
    langs.removeDuplicates();
    if (!m_currentLanguage.isEmpty() && !langs.contains(m_currentLanguage)) {
        langs.prepend(m_currentLanguage);
    }

    // 保证 zh_CN / en_US 始终可选（即使 .qm 缺失，源码默认中文也能工作）
    if (!langs.contains(QStringLiteral("zh_CN"))) {
        langs.prepend(QStringLiteral("zh_CN"));
    }
    if (!langs.contains(QStringLiteral("en_US"))) {
        langs.append(QStringLiteral("en_US"));
    }

    return langs;
}

QLocale I18nManager::currentLocale() const
{
    // 直接用语言代码构造 QLocale（Qt 内置识别 "zh_CN" / "en_US" 等 BCP47 风格）
    QLocale loc(m_currentLanguage);
    if (loc.language() == QLocale::C) {
        // 未识别回退到系统默认
        loc = QLocale::system();
    }
    return loc;
}

void I18nManager::applyLocale()
{
    QLocale::setDefault(currentLocale());
    LOG_DEBUG_S("I18nManager", "applyLocale",
                "QLocale 已设置为: " << m_currentLanguage.toStdString());
}

// ========== 私有方法 ==========

QString I18nManager::resolveLanguage(const QString& langCode) const
{
    // "system" / 空 → 跟随系统语言；其他值原样返回
    if (langCode.isEmpty() || langCode == QStringLiteral("system")) {
        QLocale sys = QLocale::system();
        // 仅在 zh_CN / en_US 中匹配，其他系统语言回退到 zh_CN
        QString sysName = sys.name();  // 如 "zh_CN" / "en_US"
        if (sysName == QStringLiteral("zh_CN") || sysName == QStringLiteral("en_US")) {
            return sysName;
        }
        // 系统 locale 不在支持列表中，按语言族匹配
        if (sys.language() == QLocale::Chinese) {
            return QStringLiteral("zh_CN");
        }
        if (sys.language() == QLocale::English) {
            return QStringLiteral("en_US");
        }
        return QStringLiteral("zh_CN");
    }
    return langCode;
}

bool I18nManager::loadTranslator(const QString& langCode)
{
    if (langCode.isEmpty()) return false;

    auto* translator = new QTranslator(this);
    QString qmBase = QStringLiteral("SoulCove_") + langCode;

    // 1. 优先从 Qt 资源 :/i18n/ 加载（已部署的内嵌 .qm）
    bool ok = translator->load(qmBase, QStringLiteral(":/i18n"));
    if (!ok) {
        // 2. 回退到可执行文件目录的 i18n/ 子目录（开发部署用）
        QString exeDir = QCoreApplication::applicationDirPath() + QStringLiteral("/i18n");
        ok = translator->load(qmBase, exeDir);
    }

    if (!ok) {
        // zh_CN 是源码默认语言，缺失 .qm 视为正常（不报错）
        if (langCode != QStringLiteral("zh_CN")) {
            LOG_DEBUG_S("I18nManager", "loadTranslator",
                        "未找到 .qm 文件: " << qmBase.toStdString());
        }
        delete translator;
        return false;
    }

    if (!qApp->installTranslator(translator)) {
        LOG_DEBUG_S("I18nManager", "loadTranslator",
                    "installTranslator 失败: " << qmBase.toStdString());
        delete translator;
        return false;
    }

    m_translator.reset(translator);
    return true;
}

QStringList I18nManager::scanQmFiles(const QString& dirPath)
{
    QStringList langs;
    QDir dir(dirPath);
    if (!dir.exists()) return langs;

    // 匹配 SoulCove_<lang>.qm
    const QString pattern = QStringLiteral("SoulCove_*.qm");
    QDirIterator it(dirPath, QStringList() << pattern, QDir::Files);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi(it.fileInfo());
        // 文件名格式：SoulCove_zh_CN.qm → 提取 "zh_CN"
        QString base = fi.completeBaseName();  // "SoulCove_zh_CN"
        QString lang = base.mid(QStringLiteral("SoulCove_").length());
        if (!lang.isEmpty()) {
            langs.append(lang);
        }
    }
    return langs;
}
