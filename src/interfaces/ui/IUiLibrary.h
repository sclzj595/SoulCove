#ifndef IUILIBRARY_H
#define IUILIBRARY_H

#include <QString>

class QWidget;

/// @brief UI三方库抽象接口
/// 定义可插拔现代化UI库的核心能力
/// 预留 QCustomUi / Qt-Material 等三方库集成入口
/// 上层代码只依赖此接口，不依赖具体实现
class IUiLibrary
{
public:
    virtual ~IUiLibrary() = default;

    /// 库名称
    virtual QString name() const = 0;

    /// 初始化UI库（注册类型、加载资源等）
    virtual bool initialize() = 0;

    /// 应用全局样式主题
    virtual void applyTheme(const QString& themeKey) = 0;

    /// 创建无边框窗口容器
    virtual QWidget* createFramelessWindow(QWidget* parent = nullptr) = 0;

    /// 创建现代化按钮
    virtual QWidget* createModernButton(const QString& text, QWidget* parent = nullptr) = 0;
};

#endif // IUILIBRARY_H
