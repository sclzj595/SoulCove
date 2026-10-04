#ifndef MYPLUGIN_H
#define MYPLUGIN_H

#include "soulcove/IPlugin.h"
#include "soulcove/IPluginAPI.h"

#include <QObject>

/// @brief 第三方插件骨架 —— 全文替换 MyPlugin 为你的插件名即可
///
/// 三要素：
/// 1. 继承 QObject + IPlugin，声明 Q_PLUGIN_METADATA（IID 固定，json 文件名可自定）
/// 2. 实现 5 个纯虚函数（name/version/description/initialize/shutdown）
/// 3. initialize() 里通过 IPluginAPI 注册命令/订阅事件，shutdown() 里置空 m_api
class MyPlugin : public QObject, public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SoulCovePluginIID FILE "myplugin.json")
    Q_INTERFACES(IPlugin)

public:
    QString name() const override;      // 唯一标识，反向域名风格
    QString version() const override;   // 语义化版本
    QString description() const override;

    bool initialize(IPluginAPI* api) override;
    void shutdown() override;

private:
    IPluginAPI* m_api = nullptr;   // shutdown 后必须置空
};

#endif // MYPLUGIN_H
