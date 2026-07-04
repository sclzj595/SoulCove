#include "ui/tools/ImagePreviewer.h"
#include "Logger.hpp"
#include "core/config/ThemeManager.h"

#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QFileInfo>
#include <QScrollBar>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolTip>

// ========== 构造函数 ==========

ImagePreviewer::ImagePreviewer(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("imagePreviewer"));
    // 接受焦点以支持键盘/鼠标事件
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // 背景色跟随主题
    QString bgColor = ThemeManager::instance().currentPalette().bgEditor.name(QColor::HexRgb);
    setStyleSheet(QStringLiteral("background-color:%1;").arg(bgColor));
}

// ========== 加载图片 ==========

bool ImagePreviewer::loadImage(const QString& imagePath)
{
    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        LOG_DEBUG("[ImagePreviewer] 无法加载图片:" << imagePath);
        return false;
    }

    m_originalPixmap = pixmap;
    m_panOffset = QPointF(0, 0);

    if (m_zoomMode == FitWindow)
        setZoomMode(FitWindow);  // 触发重新计算缩放比例
    else
        updateScaledPixmap();

    update();
    return true;
}

// ========== 缩放控制 ==========

void ImagePreviewer::setZoomMode(ZoomMode mode)
{
    m_zoomMode = mode;

    switch (mode) {
    case FitWindow: {
        if (m_originalPixmap.isNull()) {
            m_zoomFactor = 1.0;
            break;
        }
        // 等比缩放适应窗口，留少量边距
        int margin = 20;
        double scaleX = static_cast<double>(width() - margin * 2) / m_originalPixmap.width();
        double scaleY = static_cast<double>(height() - margin * 2) / m_originalPixmap.height();
        m_zoomFactor = qMin(scaleX, scaleY);
        if (m_zoomFactor <= 0) m_zoomFactor = 1.0;
        break;
    }
    case ActualSize:
        m_zoomFactor = 1.0;
        break;
    case CustomZoom:
        // 保持当前 zoomFactor 不变
        break;
    }

    m_panOffset = QPointF(0, 0);
    updateScaledPixmap();
    update();
}

void ImagePreviewer::setZoomFactor(double factor)
{
    m_zoomFactor = qBound(0.1, factor, 10.0);
    m_zoomMode = CustomZoom;
    updateScaledPixmap();
    update();
}

void ImagePreviewer::zoomIn()
{
    setZoomFactor(m_zoomFactor * 1.25);
}

void ImagePreviewer::zoomOut()
{
    setZoomFactor(m_zoomFactor / 1.25);
}

void ImagePreviewer::resetZoom()
{
    setZoomMode(FitWindow);
}

bool ImagePreviewer::canZoomIn() const
{
    return m_zoomFactor < 10.0;
}

bool ImagePreviewer::canZoomOut() const
{
    return m_zoomFactor > 0.1;
}

// ========== 内部方法 ==========

void ImagePreviewer::updateScaledPixmap()
{
    if (m_originalPixmap.isNull()) {
        m_scaledPixmap = QPixmap();
        return;
    }

    int newWidth = qMax(1, static_cast<int>(m_originalPixmap.width() * m_zoomFactor));
    int newHeight = qMax(1, static_cast<int>(m_originalPixmap.height() * m_zoomFactor));
    m_scaledPixmap = m_originalPixmap.scaled(
        newWidth, newHeight,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
}

QPixmap ImagePreviewer::createCheckerboardPattern(int size)
{
    QPixmap pattern(size * 2, size * 2);
    pattern.fill(QColor(204, 204, 204));

    QPainter p(&pattern);
    p.fillRect(0, 0, size, size, QColor(255, 255, 255));
    p.fillRect(size, size, size, size, QColor(255, 255, 255));
    p.end();

    return pattern;
}

// ========== 绘制事件 ==========

void ImagePreviewer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // 填充背景
    painter.fillRect(rect(), QColor(30, 30, 30));

    if (m_scaledPixmap.isNull()) {
        // 无图片时显示提示文字
        painter.setPen(QColor(128, 128, 128));
        QFont font = painter.font();
        font.setPointSize(14);
        painter.setFont(font);
        painter.drawText(rect(), Qt::AlignCenter, tr("无图片"));
        return;
    }

    // 计算绘制位置（居中 + 平移偏移）
    int x = (width() - m_scaledPixmap.width()) / 2 + static_cast<int>(m_panOffset.x());
    int y = (height() - m_scaledPixmap.height()) / 2 + static_cast<int>(m_panOffset.y());

    // 如果支持透明通道，绘制棋盘格背景
    if (m_originalPixmap.hasAlphaChannel()) {
        QPixmap checkerboard = createCheckerboardPattern(16);
        painter.setBrushOrigin(x, y);
        painter.setBrush(checkerboard);
        painter.setBrushOrigin(x, y);
        // 用平铺模式绘制棋盘格背景
        painter.save();
        painter.translate(x, y);
        painter.setClipRect(0, 0, m_scaledPixmap.width(), m_scaledPixmap.height());
        // 手动绘制棋盘格
        for (int cy = 0; cy < m_scaledPixmap.height(); cy += 16) {
            for (int cx = 0; cx < m_scaledPixmap.width(); cx += 16) {
                bool isLight = ((cx / 16) % 2) == ((cy / 16) % 2);
                painter.fillRect(cx, cy, 16, 16,
                                 isLight ? QColor(240, 240, 240) : QColor(200, 200, 200));
            }
        }
        painter.restore();
    }

    // 绘制图片
    painter.drawPixmap(x, y, m_scaledPixmap);

    // 显示缩放百分比信息
    QString info = QStringLiteral("%1%").arg(static_cast<int>(m_zoomFactor * 100));
    if (!m_originalPixmap.isNull()) {
        info += QStringLiteral("  (%1 x %2)")
                    .arg(m_originalPixmap.width())
                    .arg(m_originalPixmap.height());
    }

    painter.setPen(QColor(180, 180, 180));
    QFont infoFont = painter.font();
    infoFont.setPointSize(9);
    painter.setFont(infoFont);
    painter.drawText(rect().adjusted(8, 4, -8, -8),
                      Qt::AlignRight | Qt::AlignTop, info);
}

// ========== 鼠标事件（拖拽平移）==========

void ImagePreviewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_zoomMode != FitWindow) {
        m_isDragging = true;
        m_dragStartPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void ImagePreviewer::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        QPointF delta = event->pos() - m_dragStartPos;
        m_panOffset += delta;
        m_dragStartPos = event->pos();
        update();
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void ImagePreviewer::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

// ========== 滚轮事件（Ctrl+滚轮缩放）==========

void ImagePreviewer::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        // Ctrl+滚轮：缩放
        double delta = event->angleDelta().y();
        if (delta > 0)
            zoomIn();
        else if (delta < 0)
            zoomOut();

        // 显示缩放提示
        QToolTip::showText(event->globalPosition().toPoint(),
                            QStringLiteral("%1%").arg(static_cast<int>(m_zoomFactor * 100)),
                            this);

        event->accept();
    } else {
        // 普通滚轮：垂直滚动（当图片超出窗口时）
        QWidget::wheelEvent(event);
    }
}
