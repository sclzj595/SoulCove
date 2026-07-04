#include "ui/shell/Widget.h"
#include "factory/ProductConfig.h"

#include <QApplication>
#include <QFile>
#include <QDebug>
#include <QIcon>
#include <QStandardPaths>
#include "core/config/ConfigManager.h"
#include "core/i18n/I18nManager.h"  // P3-M05: 国际化管理器
#include "Logger.hpp"

/// @brief SoulCove IDE 产品入口 — 全功能集成开发环境
///
/// 产品线定位：完整IDE，支持LSP、终端、Git、任务系统、大纲导航。
/// 这是产品线的最终形态，从 Notebook Lite → Notebook → IDE 的迭代终点。
int main(int argc, char *argv[])
{
    // 安装 Qt 消息过滤器（屏蔽 Windows COM/SHELL 系统警告噪音）
    Logger::installQtMessageFilter();

    QApplication a(argc, argv);

    // 强制注册静态库 scCore 中的 Qt 资源（qrc 在静态库中时，链接器可能剥离未引用的初始化代码）
    Q_INIT_RESOURCE(resources);

    // ====== 初始化日志系统 ======
    Logger::instance().setLogLevel(LogLevel::Debug);
    Logger::instance().enableFileLogging(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/SoulCove/SoulCove.log")
    );
    LOG_INFO_S("Main", "init", "SoulCove IDE 启动");

    // 设置应用信息
    a.setApplicationName(QStringLiteral("SoulCove IDE"));
    a.setApplicationVersion(QStringLiteral("3.0"));
    a.setOrganizationName(QStringLiteral("SoulCove"));
    a.setWindowIcon(QIcon(QStringLiteral(":/app_icon")));

    // ====== 加载国际化翻译（P3-M05：通过 I18nManager 统一管理）======
    // 必须在 QApplication 创建后、主窗口构造前调用，确保 tr() 能正确翻译 UI
    I18nManager::instance().initialize();

    // 使用 IDE 配置创建主窗口（全功能）
    Widget w(ProductConfig::ide());
    w.show();

    return a.exec();
}
