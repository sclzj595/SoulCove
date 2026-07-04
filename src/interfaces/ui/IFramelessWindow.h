#ifndef IFRAMELESSWINDOW_H
#define IFRAMELESSWINDOW_H

#include <QWidget>

/// @brief 无边框窗口抽象接口
/// 定义无边框窗口的核心能力：拖拽、缩放、窗口状态控制
/// 上层代码只依赖此接口，不依赖 FramelessWindow 具体实现
class IFramelessWindow
{
public:
    virtual ~IFramelessWindow() = default;

    /// 设置标题栏控件（用于拖拽判定）
    virtual void setTitleBarWidget(QWidget* titleBar) = 0;
};

#endif // IFRAMELESSWINDOW_H
