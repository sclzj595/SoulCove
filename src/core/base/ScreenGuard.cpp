#include "core/base/ScreenGuard.h"
#include "Logger.hpp"

#include <QWidget>
#include <QResizeEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
#include <QTimer>

/// GDI 位图安全上限（最后防线，正常流程不应触发）
static constexpr int kMaxDevicePixels = 16384;
static constexpr qreal kMaxDpr = 3.0;

/// 窗口占屏幕的最大比例（90% — 留出任务栏和边距空间）
static constexpr qreal kMaxScreenRatio = 0.9;

ScreenGuard::ScreenGuard(QObject* parent)
    : QObject(parent)
{
}

void ScreenGuard::installOn(QWidget* widget)
{
    if (!widget) return;
    widget->installEventFilter(this);
    // 不设置 maximumSize — 允许用户自由拖拽调整大小
    // 跨屏适配由 handleScreenChange 一次性处理
    LOG_INFO_S("ScreenGuard", "installOn", "已安装屏幕守卫");
}

QSize ScreenGuard::safeMaxSize()
{
    if (auto* screen = qApp->primaryScreen()) {
        QRect avail = screen->availableGeometry();
        return QSize(
            static_cast<int>(avail.width() * kMaxScreenRatio),
            static_cast<int>(avail.height() * kMaxScreenRatio)
        );
    }
    return QSize(1920, 1080);
}

qreal ScreenGuard::safeMaxDevicePixelRatio()
{
    return kMaxDpr;
}

bool ScreenGuard::isSizeSafe(const QSize& logicalSize, qreal dpr)
{
    if (dpr <= 0.0) dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    qint64 dw = static_cast<qint64>(logicalSize.width()) * qRound(dpr * 100) / 100;
    qint64 dh = static_cast<qint64>(logicalSize.height()) * qRound(dpr * 100) / 100;
    // GDI 位图上限：256MB / 4字节 = 64M 像素
    return dw <= kMaxDevicePixels && dh <= kMaxDevicePixels && dw * dh <= 64LL * 1024 * 1024;
}

QSize ScreenGuard::clampSize(const QSize& size, qreal dpr)
{
    if (dpr <= 0.0) dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    if (dpr > kMaxDpr) dpr = kMaxDpr;
    int maxW = static_cast<int>(kMaxDevicePixels / dpr);
    int maxH = static_cast<int>(kMaxDevicePixels / dpr);
    return QSize(qMin(size.width(), maxW), qMin(size.height(), maxH));
}

bool ScreenGuard::eventFilter(QObject* watched, QEvent* event)
{
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget) return QObject::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Resize:
        return handleResize(widget, static_cast<QResizeEvent*>(event));
    case QEvent::ScreenChangeInternal:
        handleScreenChange(widget);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

bool ScreenGuard::handleResize(QWidget* widget, QResizeEvent* event)
{
    QSize newSize = event->size();
    qreal dpr = widget->devicePixelRatioF();

    // 最后防线：GDI 位图安全检查
    if (!isSizeSafe(newSize, dpr)) {
        QSize clamped = clampSize(newSize, dpr);
        LOG_WARN_S("ScreenGuard", "handleResize",
                   "GDI安全拦截:" << newSize.width() << "x" << newSize.height()
                   << " → " << clamped.width() << "x" << clamped.height()
                   << " (DPR=" << dpr << ")");
        QMetaObject::invokeMethod(widget, [widget, clamped]() {
            widget->resize(clamped);
        }, Qt::QueuedConnection);
        return true;
    }

    return false;
}

bool ScreenGuard::handleScreenChange(QWidget* widget)
{
    if (!widget) return false;

    QScreen* screen = widget->screen();
    if (!screen) return false;

    QRect available = screen->availableGeometry();
    QSize currentSize = widget->size();
    QPoint currentPos = widget->pos();

    // 1. 如果窗口最大化/全屏，不干预（系统会自动适配）
    if (widget->isMaximized() || widget->isFullScreen()) {
        LOG_DEBUG_S("ScreenGuard", "handleScreenChange",
                    "窗口最大化, 跳过尺寸调整, 屏幕:" << screen->name()
                    << "可用区:" << available.width() << "x" << available.height());
        return false;
    }

    // 2. 窗口尺寸超出新屏幕 90% → 一次性缩放到 90% 以内
    //    （不设置 maximumSize，用户之后仍可自由拖拽调整）
    int maxW = static_cast<int>(available.width() * kMaxScreenRatio);
    int maxH = static_cast<int>(available.height() * kMaxScreenRatio);
    bool needResize = false;
    int newW = currentSize.width();
    int newH = currentSize.height();

    if (currentSize.width() > maxW) {
        newW = maxW;
        needResize = true;
    }
    if (currentSize.height() > maxH) {
        newH = maxH;
        needResize = true;
    }

    // 4. 窗口位置超出新屏幕可见区域 → 移入屏幕内
    bool needMove = false;
    int newX = currentPos.x();
    int newY = currentPos.y();

    if (newX + newW < available.left() + 50) {
        newX = available.left() + 50;
        needMove = true;
    } else if (newX > available.right() - 50) {
        newX = available.left() + 50;
        needMove = true;
    }
    if (newY + newH < available.top() + 50) {
        newY = available.top() + 50;
        needMove = true;
    } else if (newY > available.bottom() - 50) {
        newY = available.top() + 50;
        needMove = true;
    }

    // 5. 延迟执行调整（等屏幕切换事件处理完再改尺寸，避免递归）
    if (needResize || needMove) {
        LOG_INFO_S("ScreenGuard", "handleScreenChange",
                   "跨屏适配: 屏幕" << screen->name()
                   << " 可用" << available.width() << "x" << available.height()
                   << " 窗口" << currentSize.width() << "x" << currentSize.height()
                   << " → " << newW << "x" << newH
                   << (needMove ? " (重定位)" : ""));

        QTimer::singleShot(0, widget, [widget, newW, newH, newX, newY, needMove]() {
            widget->resize(newW, newH);
            if (needMove) widget->move(newX, newY);
        });
    }

    return false;
}
