#include "ui/shell/FramelessWindow.h"
#include "core/config/ThemeManager.h"

#include <QMouseEvent>
#include <QShowEvent>
#include <QGuiApplication>
#include <QAbstractButton>
#include <QPainter>
#include <QPaintEvent>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>

// P1: Win11 DWM 背景类型 API 兼容性定义
// MinGW 11.2 的 dwmapi.h 不包含 Win11 的 DWM_SYSTEMBACKDROP_TYPE 常量，
// 手动定义以支持在旧 SDK 上编译（值来自 Windows SDK）
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWM_SYSTEMBACKDROP_TYPE
typedef enum DWM_SYSTEMBACKDROP_TYPE {
    DWMSBT_AUTO = 0,
    DWMSBT_NONE = 1,
    DWMSBT_MAINWINDOW = 2,
    DWMSBT_TRANSIENTWINDOW = 3,
    DWMSBT_TABBEDWINDOW = 4
} DWM_SYSTEMBACKDROP_TYPE;
#endif
#endif

// ========== 构造 / 初始化 ==========

FramelessWindow::FramelessWindow(QWidget* parent)
    : QWidget(parent)
{
    initWindowAttributes();
    setMouseTracking(true);
}

void FramelessWindow::initWindowAttributes()
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_Hover, true);
    // 修复 DWM 闪烁主因：启用透明背景，让 MICA/BlurBehind 正确透过客户区，
    // 避免 Windows 用系统默认画刷擦背景造成拖拽时浅色闪烁
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    applyAcrylicEffect();

#ifdef Q_OS_WIN
    // 关键修复：Qt::FramelessWindowHint 创建的是 WS_POPUP 窗口，没有 WS_THICKFRAME 样式。
    // 没有 WS_THICKFRAME，即使 WM_NCHITTEST 返回 HTLEFT/HTRIGHT 等，
    // Windows 也不会启动缩放循环（DefWindowProc 直接忽略 hit-test 结果）。
    // 解决方案：手动添加 WS_THICKFRAME，然后在 WM_NCCALCSIZE 中把边框裁掉，
    // 既保留缩放能力，又保持无边框外观。
    HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_THICKFRAME);
    // 重新应用 DWM 特效（改样式后可能丢失）
    applyAcrylicEffect();
#endif
}

// ========== DWM 磨砂特效 ==========

void FramelessWindow::applyAcrylicEffect()
{
#ifdef Q_OS_WIN
    HWND hwnd = (HWND)winId();

    // 修复 DWM 闪烁：将 DWM 合成层扩展到整个客户区，与 WA_TranslucentBackground 配合，
    // 消除拖拽/缩放时 DWM 合成层与 Qt 客户区各自独立重绘造成的撕裂
    MARGINS margins = { -1, -1, -1, -1 };  // -1 表示整窗扩展（毛玻璃）
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    // 修复主题割裂：禁用 Win11 MICA 背景效果。
    // MICA 使用系统壁纸色调，与自定义紫色暗黑主题不匹配，
    // 拖拽/缩放时 DWM 合成层先于 Qt paintEvent 绘制，导致系统色闪烁。
    // 改为 DWMSBT_NONE（无系统背景），完全依赖 paintEvent 的纯色主题填充，
    // 确保窗口背景与编辑器主题视觉统一。
    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // Win10 BlurBehind 磨砂效果（兼容）— 同样禁用，避免系统色透过
    DWM_BLURBEHIND bb = {};
    bb.dwFlags = DWM_BB_ENABLE;
    bb.fEnable = FALSE;
    bb.hRgnBlur = nullptr;
    DwmEnableBlurBehindWindow(hwnd, &bb);

    // 暗色模式标题栏（Win10 1809+ / Win11）
    // 修复：原硬编码 TRUE 导致亮色主题下标题栏仍为暗色，
    // 改为根据当前主题色板亮度动态判断，与 ThemeManager 保持一致
    bool isDark = ThemeManager::instance().currentPalette().bgEditor.lightness() <= 128;
    BOOL darkMode = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));

    // 扩展窗口边框到客户端区域
    BOOL ncRendering = TRUE;
    DwmSetWindowAttribute(hwnd, 2, &ncRendering, sizeof(ncRendering));

    // Win11 圆角窗口
    DWORD cornerPreference = 2;
    DwmSetWindowAttribute(hwnd, 33, &cornerPreference, sizeof(cornerPreference));
#endif
}

void FramelessWindow::enableAcrylicEffect()
{
#ifdef Q_OS_WIN
    applyAcrylicEffect();
#endif
}

void FramelessWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    enableAcrylicEffect();
}

// ========== 兜底背景绘制 ==========
// WA_TranslucentBackground 后窗口不再自动填充背景，需手动绘制主题色，
// 否则 QSS 尚未刷新或子控件未覆盖的区域会透出桌面。
void FramelessWindow::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    p.fillRect(rect(), ThemeManager::instance().currentPalette().bgWindow);
    QWidget::paintEvent(event);
}

// ========== 标题栏设置 ==========

void FramelessWindow::setTitleBarWidget(QWidget* titleBar)
{
    m_titleBarWidget = titleBar;
    m_titleBarWidget->setMouseTracking(true);
    for (auto* child : m_titleBarWidget->findChildren<QWidget*>()) {
        child->setMouseTracking(true);
    }
}

// ========== Windows 原生事件：WM_NCHITTEST 边缘缩放 + WM_NCCALCSIZE 边框裁剪 ==========
//
// 核心原理：
//   WM_NCHITTEST 是 Windows 在任何 Qt 事件之前发送的消息，
//   用于确定鼠标点击的区域（客户区/标题栏/边缘等）。
//   通过在此返回 HTLEFT/HTRIGHT/HTTOP/HTBOTTOM 等，
//   Windows 会原生处理缩放，子控件无法拦截。
//
// 为什么不用 Qt mouseMoveEvent 检测边缘：
//   Qt 事件会被子控件（SideBar/编辑器/状态栏等）拦截，
//   导致只有子控件缝隙（如左下角）能触发缩放。
//   WM_NCHITTEST 在 Qt 事件之前触发，子控件无法拦截。
//
// 为什么需要 WS_THICKFRAME + WM_NCCALCSIZE：
//   Qt::FramelessWindowHint 创建的 WS_POPUP 窗口没有 WS_THICKFRAME，
//   Windows 不会为 HTLEFT 等 hit-test 结果启动缩放循环。
//   手动添加 WS_THICKFRAME 后，Windows 会绘制默认边框，
//   需要在 WM_NCCALCSIZE 中返回 0 把边框裁掉，保持无边框外观。
//
// 光标样式：
//   Windows 根据 WM_NCHITTEST 返回值自动设置光标
//   （HTLEFT → 水平箭头，HTTOP → 垂直箭头等），无需手动处理。
//
bool FramelessWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);

        // WM_NCCALCSIZE：裁剪 WS_THICKFRAME 添加的边框，保持无边框外观
        // 返回 0 表示使用建议的客户区（即整个窗口），不绘制非客户区边框
        if (msg->message == WM_NCCALCSIZE) {
            if (msg->wParam == TRUE) {
                *result = 0;  // 边框裁剪
                return true;
            }
        }

        if (msg->message == WM_NCHITTEST) {
            // 最大化/全屏时禁用边缘缩放 — 全部作为客户区处理
            // （避免多屏环境下最大化窗口边缘可见时误触发缩放）
            if (isMaximized() || isFullScreen()) {
                *result = HTCLIENT;
                return true;
            }

            // 获取鼠标相对于窗口客户区的坐标
            POINT winPos = msg->pt;
            ScreenToClient(msg->hwnd, &winPos);

            RECT winRect;
            GetClientRect(msg->hwnd, &winRect);

            const int m = s_edgeMargin;
            bool left   = winPos.x < m;
            bool right  = winPos.x > winRect.right - m;
            bool top    = winPos.y < m;
            bool bottom = winPos.y > winRect.bottom - m;

            // 优先检测角点（4 个对角）— 角点优先级高于边线
            if (left && top)     { *result = HTTOPLEFT;     return true; }
            if (right && top)    { *result = HTTOPRIGHT;    return true; }
            if (left && bottom)  { *result = HTBOTTOMLEFT;  return true; }
            if (right && bottom) { *result = HTBOTTOMRIGHT; return true; }

            // 再检测边线（4 个方向）
            if (left)   { *result = HTLEFT;   return true; }
            if (right)  { *result = HTRIGHT;  return true; }
            if (top)    { *result = HTTOP;    return true; }
            if (bottom) { *result = HTBOTTOM; return true; }

            // 非边缘区域：返回 HTCLIENT，交给 Qt 处理
            // （标题栏拖拽、双击最大化等通过 Qt 鼠标事件实现）
            *result = HTCLIENT;
            return true;
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

// ========== 标题栏区域检测 ==========
//
// 判断全局坐标是否落在标题栏的非按钮区域。
// 用于：mousePressEvent 启动拖拽、mouseDoubleClickEvent 触发最大化。
//
// 注意：TitleBar 不重写 mousePressEvent，事件会 propagate 到 FramelessWindow。
//       按钮点击会被 QPushButton 消费，不会 propagate，因此不会误触发拖拽。
//
bool FramelessWindow::isOnTitleBarNonButton(const QPoint& globalPos)
{
    if (!m_titleBarWidget) return false;

    QPoint localPos = m_titleBarWidget->mapFromGlobal(globalPos);
    if (!m_titleBarWidget->rect().contains(localPos))
        return false;

    // 递归检查是否点击在按钮上（按钮点击不应触发拖拽）
    QWidget* child = m_titleBarWidget->childAt(localPos);
    while (child && child != m_titleBarWidget) {
        if (qobject_cast<QAbstractButton*>(child))
            return false;
        child = child->parentWidget();
    }

    return true;
}

// ========== Qt 鼠标事件（标题栏拖拽）==========

void FramelessWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    QPoint globalPos = event->globalPosition().toPoint();

    // 标题栏非按钮区域 → 启动拖拽
    if (isOnTitleBarNonButton(globalPos)) {
        // 最大化状态下拖拽标题栏 → 自动还原窗口
        // （VSCode/Chrome 标准行为：拖拽时窗口"撕下"最大化状态）
        // 还原后保持鼠标在标题栏水平方向的相对比例，避免窗口跳变
        if (isMaximized()) {
            QRect normal = normalGeometry();
            qreal ratio = static_cast<qreal>(event->position().x()) / width();
            int newX = globalPos.x() - static_cast<int>(normal.width() * ratio);
            int newY = globalPos.y() - event->position().y();
            showNormal();
            move(newX, newY);
        }

        m_isDragging = true;
        m_dragPos = globalPos - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void FramelessWindow::mouseMoveEvent(QMouseEvent* event)
{
    // 拖拽移动
    if (m_isDragging) {
        QPoint globalPos = event->globalPosition().toPoint();
        move(globalPos - m_dragPos);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void FramelessWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

// ========== 双击最大化/还原 ==========

void FramelessWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QPoint globalPos = event->globalPosition().toPoint();
        if (isOnTitleBarNonButton(globalPos)) {
            if (isMaximized()) showNormal();
            else showMaximized();
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}
