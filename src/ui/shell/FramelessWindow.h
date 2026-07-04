#ifndef FRAMELESSWINDOW_H
#define FRAMELESSWINDOW_H

#include <QWidget>
#include <QPoint>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#include "interfaces/ui/IFramelessWindow.h"

/// @brief 无边框窗口基类
///
/// 架构设计：
///   - 边缘缩放：通过 Windows 原生 WM_NCHITTEST 实现
///     （在 Qt 事件之前触发，子控件无法拦截，8 个方向均可缩放）
///   - 标题栏拖拽：通过 Qt mousePressEvent/mouseMoveEvent 实现
///     （精确控制标题栏区域，避开按钮）
///   - 双击最大化：通过 Qt mouseDoubleClickEvent 实现
///
/// DWM 特效：
///   - Win11 MICA 磨砂背景
///   - Win10 BlurBehind 兼容
///   - 暗色模式标题栏
///   - Win11 圆角窗口
class FramelessWindow : public QWidget, public IFramelessWindow
{
    Q_OBJECT

public:
    explicit FramelessWindow(QWidget* parent = nullptr);
    ~FramelessWindow() override = default;

    void setTitleBarWidget(QWidget* titleBar) override;

protected:
    // === Qt 鼠标事件（标题栏拖拽 + 双击最大化）===
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

    // === Windows 原生事件（边缘缩放检测）===
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    /// 初始化窗口属性（无边框 + Hover）
    void initWindowAttributes();
    /// 应用 DWM 磨砂/暗色/圆角特效
    void applyAcrylicEffect();
    /// 启用 Windows DWM 亚克力磨砂效果（Win10/Win11）
    void enableAcrylicEffect();

    /// 检查全局坐标是否在标题栏非按钮区域
    bool isOnTitleBarNonButton(const QPoint& globalPos);

    // === 窗口状态 ===
    QWidget* m_titleBarWidget = nullptr;  ///< 标题栏控件
    QPoint   m_dragPos;                   ///< 拖拽起始偏移
    bool     m_isDragging = false;        ///< 正在拖拽移动

    static const int s_edgeMargin = 8;    ///< 边缘检测宽度（像素）
};

#endif // FRAMELESSWINDOW_H
