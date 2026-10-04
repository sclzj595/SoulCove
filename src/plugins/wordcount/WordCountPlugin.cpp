#include "WordCountPlugin.h"

#include <QFile>
#include <QRegularExpression>

QString WordCountPlugin::name() const
{
    return QStringLiteral("com.soulcove.wordcount");
}

QString WordCountPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString WordCountPlugin::description() const
{
    return QStringLiteral("字数统计：统计当前文档的字符/单词/行数，支持导出到插件数据目录（M9 市场安装流程案例）");
}

// ============================================================
// 统计核心（命令与事件回调共用）
// ============================================================

WordCountPlugin::Stats WordCountPlugin::collectStats() const
{
    Stats s;
    if (!m_api) return s;
    const auto doc = m_api->currentDocument();
    if (!doc.valid) return s;

    // 行数：按 \n 切分（最后一行无换行符也计 1）
    s.lines = doc.text.isEmpty() ? 0 : static_cast<int>(doc.text.count(QLatin1Char('\n')) + 1);

    // 非空白字符数
    for (const QChar& c : doc.text) {
        if (!c.isSpace()) ++s.chars;
    }

    // 单词数：连续拉丁/数字串计 1，每个 CJK 字符计 1（中英文混排惯例）
    static const QRegularExpression latinWord(QStringLiteral("[A-Za-z0-9_]+"));
    QRegularExpressionMatchIterator it = latinWord.globalMatch(doc.text);
    while (it.hasNext()) { it.next(); ++s.words; }
    for (const QChar& c : doc.text) {
        if (c.script() == QChar::Script_Han) ++s.words;
    }

    s.valid = true;
    return s;
}

// ============================================================
// 生命周期
// ============================================================

bool WordCountPlugin::initialize(IPluginAPI* api)
{
    m_api = api;
    if (!api) return false;

    api->log(QStringLiteral("[com.soulcove.wordcount] 初始化完成，宿主版本: ")
             + api->applicationVersion());

    // 命令 1：统计当前文档（命令面板 Ctrl+Shift+P 搜 "wordcount"）
    api->registerCommand(QStringLiteral("plugin.wordcount.stats"),
                         QStringLiteral("字数统计：统计当前文档（字符/单词/行数）"),
                         [this]() {
                             const Stats s = collectStats();
                             if (!s.valid) {
                                 m_api->log(QStringLiteral("[com.soulcove.wordcount] 当前没有打开的文档"));
                                 return;
                             }
                             m_api->log(QStringLiteral("[com.soulcove.wordcount] 统计 → 字符: %1 ｜ 单词: %2 ｜ 行: %3")
                                            .arg(s.chars).arg(s.words).arg(s.lines));
                         });

    // 命令 2：导出统计到插件私有数据目录（演示 pluginDataDir）
    api->registerCommand(QStringLiteral("plugin.wordcount.export"),
                         QStringLiteral("字数统计：导出统计结果到插件数据目录"),
                         [this]() {
                             const Stats s = collectStats();
                             if (!s.valid) {
                                 m_api->log(QStringLiteral("[com.soulcove.wordcount] 当前没有打开的文档"));
                                 return;
                             }
                             const QString dir = m_api->pluginDataDir(name());
                             const QString file = dir + QStringLiteral("/stats.txt");
                             QFile f(file);
                             if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                                 m_api->log(QStringLiteral("[com.soulcove.wordcount] 导出失败: ") + file);
                                 return;
                             }
                             f.write(QStringLiteral("chars=%1\nwords=%2\nlines=%3\n")
                                         .arg(s.chars).arg(s.words).arg(s.lines).toUtf8());
                             m_api->log(QStringLiteral("[com.soulcove.wordcount] 已导出 → ") + file);
                         });

    // 事件订阅：保存文件时自动统计（shutdown 时宿主自动退订，无需手动清理）
    api->subscribeEvent(QStringLiteral("fileSaved"), [this](const QVariant&) {
        const Stats s = collectStats();
        if (s.valid) {
            m_api->log(QStringLiteral("[com.soulcove.wordcount] 文件已保存，当前统计 → 字符: %1 ｜ 单词: %2 ｜ 行: %3")
                           .arg(s.chars).arg(s.words).arg(s.lines));
        }
    });

    return true;
}

void WordCountPlugin::shutdown()
{
    if (m_api) {
        m_api->log(QStringLiteral("[com.soulcove.wordcount] 关闭"));
    }
    m_api = nullptr;   // 宿主 API 即将随宿主卸载，释放引用
}
