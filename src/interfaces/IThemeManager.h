#ifndef ITHEMEMANAGER_H
#define ITHEMEMANAGER_H

#include <QString>
#include <QStringList>

/// @brief 主题管理器抽象接口
/// 管理多主题色板，支持动态注册、热切换
/// 上层代码只依赖此接口，不依赖 ThemeManager 具体实现
class IThemeManager
{
public:
    virtual ~IThemeManager() = default;

    /// 切换主题
    virtual void switchTheme(const QString& key) = 0;

    /// 当前主题key
    virtual QString currentTheme() const = 0;

    /// 获取所有已注册主题key列表
    virtual QStringList themeKeys() const = 0;

    /// 获取主题显示名
    virtual QString themeDisplayName(const QString& key) const = 0;
};

#endif // ITHEMEMANAGER_H
