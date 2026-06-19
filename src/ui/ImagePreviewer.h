#ifndef IMAGEPREVIEWER_H
#define IMAGEPREVIEWER_H

#include <QWidget>
#include <QPixmap>
#include <QPointF>

/// @brief 图片预览器
/// 在编辑器中打开图片文件时显示图片预览（替代乱码文本）
/// 支持缩放、拖拽平移、滚轮缩放等功能
class ImagePreviewer : public QWidget
{
    Q_OBJECT

public:
    explicit ImagePreviewer(QWidget* parent = nullptr);

    /// 加载图片
    bool loadImage(const QString& imagePath);

    /// 缩放模式
    enum ZoomMode { FitWindow, ActualSize, CustomZoom };
    void setZoomMode(ZoomMode mode);
    void setZoomFactor(double factor);  // 0.1 ~ 10.0

    /// 操作
    void zoomIn();
    void zoomOut();
    void resetZoom();
    bool canZoomIn() const;
    bool canZoomOut() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;   // 滚轮缩放
    void mousePressEvent(QMouseEvent* event) override; // 拖拽平移
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPixmap m_originalPixmap;
    QPixmap m_scaledPixmap;
    double m_zoomFactor = 1.0;
    ZoomMode m_zoomMode = FitWindow;
    QPoint m_dragStartPos;
    bool m_isDragging = false;
    QPointF m_panOffset;

    void updateScaledPixmap();

    /// 绘制棋盘格透明背景图案（像 PS/GIMP）
    static QPixmap createCheckerboardPattern(int size = 16);
};

#endif // IMAGEPREVIEWER_H
