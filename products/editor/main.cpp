#include "ui/shell/Widget.h"
#include "factory/ProductConfig.h"

#include <QApplication>
#include <QIcon>
#include <QStandardPaths>
#include "core/config/ConfigManager.h"
#include "core/i18n/I18nManager.h"  // P3-M05: 国际化管理器
#include "Logger.hpp"

/// @brief SoulCove Notebook 产品入口 — 代码编辑器
///
/// 产品线定位：中级代码编辑工具，支持文件树、搜索、格式化。
/// 无LSP、无终端、无Git、无任务系统。
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
    LOG_INFO_S("Main", "init", "SoulCove Notebook 启动");

    // 设置应用信息
    a.setApplicationName(QStringLiteral("SoulCove Notebook"));
    a.setApplicationVersion(QStringLiteral("1.0"));
    a.setOrganizationName(QStringLiteral("SoulCove"));
    a.setWindowIcon(QIcon(QStringLiteral(":/app_icon")));

    // ====== 加载国际化翻译（P3-M05：通过 I18nManager 统一管理）======
    // 必须在 QApplication 创建后、主窗口构造前调用，确保 tr() 能正确翻译 UI
    I18nManager::instance().initialize();

    // 使用 Code Editor 配置创建主窗口
    Widget w(ProductConfig::codeEditor());
    w.show();

    return a.exec();
}
