#include "HelloPlugin.h"

QString HelloPlugin::name() const
{
    return QStringLiteral("com.soulcove.hello");
}

QString HelloPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString HelloPlugin::description() const
{
    return QStringLiteral("示例插件：注册 plugin.hello.greet 命令，演示插件生命周期");
}

bool HelloPlugin::initialize(IPluginAPI* api)
{
    m_api = api;
    if (!api) return false;

    api->log(QStringLiteral("[com.soulcove.hello] 初始化完成，宿主版本: ")
             + api->applicationVersion());

    // 注册示例命令（可通过命令面板 Ctrl+Shift+P 触发）
    api->registerCommand(QStringLiteral("plugin.hello.greet"),
                         QStringLiteral("示例插件：输出问候日志"),
                         [this]() {
                             if (m_api) {
                                 m_api->log(QStringLiteral("[com.soulcove.hello] Hello, SoulCove!"));
                             }
                         });
    return true;
}

void HelloPlugin::shutdown()
{
    if (m_api) {
        m_api->log(QStringLiteral("[com.soulcove.hello] 关闭"));
    }
    m_api = nullptr;   // 宿主 API 即将随宿主卸载，释放引用
}
