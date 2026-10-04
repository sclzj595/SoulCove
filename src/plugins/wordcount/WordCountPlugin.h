#ifndef WORDCOUNTPLUGIN_H
#define WORDCOUNTPLUGIN_H

#include "interfaces/plugin/IPlugin.h"
#include "interfaces/plugin/IPluginAPI.h"

#include <QObject>

/// @brief WordCount 字数统计插件 —— M9 市场安装流程完整案例
///
/// 一个「真实有用」的最小插件，覆盖 IPluginAPI 全部能力：
/// - 命令 plugin.wordcount.stats：统计当前文档（字符/单词/行数）并写日志
/// - 命令 plugin.wordcount.export：把统计结果导出到插件私有数据目录 stats.txt
/// - 订阅 fileSaved 事件：保存时自动统计
///
/// 作为市场案例的意义：本插件的 DLL 构建产物 + 一条 marketplace.json
/// 条目，即构成「扩展市场 → 下载安装 → 重启加载 → 命令面板使用」全链路演示。
class WordCountPlugin : public QObject, public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SoulCovePluginIID FILE "wordcount.json")
    Q_INTERFACES(IPlugin)

public:
    QString name() const override;      // "com.soulcove.wordcount"
    QString version() const override;   // "1.0.0"
    QString description() const override;

    bool initialize(IPluginAPI* api) override;
    void shutdown() override;

private:
    /// 统计快照（供命令与事件回调共用）
    struct Stats {
        int chars = 0;        // 非空白字符数
        int words = 0;        // 单词数（拉丁词 + CJK 单字计 1）
        int lines = 0;        // 行数
        bool valid = false;   // 是否有当前文档
    };
    Stats collectStats() const;

    IPluginAPI* m_api = nullptr;   ///< 宿主 API（shutdown 后置空，不得持有）
};

#endif // WORDCOUNTPLUGIN_H
