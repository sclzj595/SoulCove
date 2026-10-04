#include "MyPlugin.h"

QString MyPlugin::name() const
{
    return QStringLiteral("com.example.myplugin");   // 反向域名，保证全局唯一
}

QString MyPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString MyPlugin::description() const
{
    return QStringLiteral("我的第一个 SoulCove 插件");
}

bool MyPlugin::initialize(IPluginAPI* api)
{
    m_api = api;
    if (!api) return false;

    api->log(QStringLiteral("[com.example.myplugin] 初始化，宿主版本: ")
             + api->applicationVersion());

    // 注册命令：命令面板 Ctrl+Shift+P 搜索 "myplugin" 触发
    api->registerCommand(QStringLiteral("plugin.myplugin.hello"),
                         QStringLiteral("我的插件：打个招呼"),
                         [this]() {
                             if (m_api) m_api->log(QStringLiteral("Hello from my plugin!"));
                         });

    // 可选：订阅宿主事件（fileOpened / fileSaved / fileClosed / encodingChanged）
    api->subscribeEvent(QStringLiteral("fileSaved"), [this](const QVariant&) {
        if (!m_api) return;
        const auto doc = m_api->currentDocument();
        if (doc.valid) {
            m_api->log(QStringLiteral("已保存: ") + doc.path
                       + QStringLiteral("（") + QString::number(doc.text.length())
                       + QStringLiteral(" 字符）"));
        }
    });

    return true;
}

void MyPlugin::shutdown()
{
    m_api = nullptr;   // 宿主即将卸载本插件，不得再持有宿主资源
}
