#ifndef SCREENGUARD_H
#define SCREENGUARD_H

#include <QObject>
#include <QSize>

class QResizeEvent;
class QPaintEvent;

/// @brief 屏幕安全守卫 — 防止窗口跨屏/高 DPI 时尺寸爆炸导致 GDI 崩溃
///
/// 问题根因：
///   Windows GDI 的 CreateDIBSection 对位图尺寸有隐性上限（~32767px）。
///   当窗口从低 DPI 屏拖到高 DPI 外接屏时，Qt 按设备像素比放大
///   backing store，导致实际位图尺寸 = 窗口逻辑尺寸 × DPI倍率。
///   极端情况下位图超过 2GB → CreateDIBSection 失败 → 崩溃。
///
/// 解决方案：
///   1. 限制窗口最大逻辑尺寸（resizeEvent 拦截）
///   2. 限制设备像素比上限（paintEvent 前检查）
///   3. 超限时跳过绘制而非崩溃
class ScreenGuard : public QObject
{
    Q_OBJECT
public:
    explicit ScreenGuard(QObject* parent = nullptr);

    /// 安装到目标窗口作为事件过滤器
    void installOn(QWidget* widget);

    /// 获取安全最大逻辑尺寸
    static QSize safeMaxSize();

    /// 获取安全最大设备像素比
    static qreal safeMaxDevicePixelRatio();

    /// 检查给定尺寸是否安全（考虑 DPI 后的实际位图大小）
    static bool isSizeSafe(const QSize& logicalSize, qreal dpr = 0.0);

    /// 将尺寸钳制到安全范围
    static QSize clampSize(const QSize& size, qreal dpr = 0.0);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// 处理 resize 事件 — 钳制窗口尺寸
    bool handleResize(QWidget* widget, QResizeEvent* event);
    /// 处理屏幕切换 — 检查 DPI 变化
    bool handleScreenChange(QWidget* widget);
};

#endif // SCREENGUARD_H
