#ifndef COMPLETIONICONS_H
#define COMPLETIONICONS_H

#include <QIcon>
#include <QHash>
#include <QString>

/// @file CompletionIcons.h
/// @brief C04-10: 补全项分类标注（图标）
///
/// 单例类，根据 LSP CompletionItemKind / 本地词典 / snippet 类型
/// 返回 16x16 QIcon。图标用 QPixmap 在代码中绘制（不依赖外部资源），
/// 颜色跟随 ThemeManager::instance().currentPalette()。
///
/// 主题切换时调用 refresh() 清缓存，下次访问按新主题重建图标。
class CompletionIcons
{
public:
    /// 单例访问
    static CompletionIcons& instance();

    /// @brief 根据 LSP CompletionItemKind 字符串返回图标
    /// @param kind LSP kind 字符串（Function/Method/Variable/Class/...）
    ///             与 LspCompletionItem.kind 一致
    QIcon iconForLspKind(const QString& kind) const;

    /// @brief 本地词典项图标（单词图标）
    QIcon iconForLocalWord() const;

    /// @brief snippet 图标
    QIcon iconForSnippet() const;

    /// @brief 清缓存（主题切换时调用）
    /// 缓存项下次访问时按新主题色板重建
    void refresh();

private:
    CompletionIcons();
    Q_DISABLE_COPY(CompletionIcons)

    /// 重建所有缓存图标（按当前主题色板）
    void rebuildCache() const;

    /// 按类型绘制单个图标到 QPixmap（16x16）
    QPixmap drawIcon(const QColor& color, const QString& shape) const;

    /// 取当前主题对应的强调色 / 前景色（不同主题自动适配）
    QColor themeColor(const QString& role) const;

    // 缓存（mutable 以便 const 接口内懒构建）
    mutable QHash<QString, QIcon> m_lspKindCache;   // kind → QIcon
    mutable QIcon m_localWordIcon;
    mutable QIcon m_snippetIcon;
    mutable bool m_cacheBuilt = false;
    mutable QString m_cachedThemeKey;               // 用于检测主题切换
};

#endif // COMPLETIONICONS_H
