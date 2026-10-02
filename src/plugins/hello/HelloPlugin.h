#ifndef HELLOPLUGIN_H
#define HELLOPLUGIN_H

#include "interfaces/plugin/IPlugin.h"
#include "interfaces/plugin/IPluginAPI.h"

#include <QObject>

/// @brief 示例插件（M7 插件系统联调用）
///
/// 演示最小合规插件的全部要素：
/// - 继承 QObject + IPlugin，Q_PLUGIN_METADATA 声明 IID 与元数据文件
/// - initialize() 通过 IPluginAPI 写日志、注册命令 "plugin.hello.greet"
/// - shutdown() 释放对宿主 API 的引用
class HelloPlugin : public QObject, public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SoulCovePluginIID FILE "hello.json")
    Q_INTERFACES(IPlugin)

public:
    QString name() const override;
    QString version() const override;
    QString description() const override;

    bool initialize(IPluginAPI* api) override;
    void shutdown() override;

private:
    IPluginAPI* m_api = nullptr;   ///< 宿主 API（shutdown 后置空，不得持有）
    int m_subId = -1;              ///< fileSaved 事件订阅ID（v1.1 演示）
};

#endif // HELLOPLUGIN_H
