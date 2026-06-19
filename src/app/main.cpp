#include "ui/shell/Widget.h"

#include <QApplication>
#include <QFile>
#include <QDebug>
#include <QIcon>
#include <QTranslator>
#include <QStandardPaths>
#include "core/config/ConfigManager.h"
#include "Logger.hpp"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ====== 初始化日志系统 ======
    Logger::instance().setLogLevel(LogLevel::Debug);
    Logger::instance().enableFileLogging(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/scNotebook/scNotebook.log")
    );
    LOG_INFO_S("Main", "init", "scNotebook 启动");

    // 设置应用信息
    a.setApplicationName(QStringLiteral("scNotebook"));
    a.setApplicationVersion(QStringLiteral("2.0"));
    a.setOrganizationName(QStringLiteral("scStudio"));

    // 设置应用图标（窗口图标 + 任务栏图标）
    a.setWindowIcon(QIcon(QStringLiteral(":/app_icon")));

    // ====== 加载国际化翻译 ======
    // 从 ConfigManager 读取语言偏好，默认中文
    QTranslator translator;
    QString lang = ConfigManager::instance().getValue(
        QStringLiteral("app/language"),
        QStringLiteral("zh_CN")
    ).toString();
    if (translator.load(QStringLiteral("scNotebook_") + lang, QStringLiteral(":/i18n"))) {
        a.installTranslator(&translator);
    }

    // 注意：不再加载 modern.qss 静态样式表
    // 所有统一样式由 ThemeManager::generateQSS() 动态生成，支持多主题切换
    // 样式表将在 Widget 构造函数中通过 ThemeManager::switchTheme() 应用

    Widget w;
    w.show();

    return a.exec();
}
