#ifndef IMAGELIGHTBOX_H
#define IMAGELIGHTBOX_H

#include <QDialog>
#include <QLabel>
#include <QPixmap>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPushButton>

/// @brief 图片灯箱预览窗口（模态对话框）
///
/// 功能：
/// - 全屏/居中显示放大的图片
/// - 支持鼠标滚轮缩放（20%-500%）
/// - 支持鼠标拖拽平移
/// - 点击背景关闭
/// - 显示当前缩放比例
/// - 工具栏：放大/缩小/适应窗口/原始大小/关闭
///
/// 使用场景：Markdown预览中点击图片时弹出
class ImageLightBox : public QDialog
{
    Q_OBJECT

public:
    /// @brief 构造函数
    /// @param parent 父窗口
    /// @param imagePath 图片路径或URL
    /// @param pixmap 图片数据（优先使用）
    explicit ImageLightBox(QWidget* parent = nullptr,
                           const QString& imagePath = QString(),
                           const QPixmap& pixmap = QPixmap());

    /// @brief 显示图片（静态工厂方法）
    static void showImage(QWidget* parent, const QString& imageUrl);

public slots:
    /// @brief 缩放图片
    void zoomIn();     // 放大 (+25%)
    void zoomOut();    // 缩小 (-25%)
    void fitToWindow();// 适应窗口
    void originalSize();// 原始大小
    void resetZoom();  // 重置为100%

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupUI();
    void updateImage();
    void updateZoomLabel();
    void centerImage();

    QLabel* m_imageLabel = nullptr;      ///< 图片显示标签
    QLabel* m_zoomLabel = nullptr;        ///< 缩放比例显示
    QPushButton* m_btnZoomIn = nullptr;
    QPushButton* m_btnZoomOut = nullptr;
    QPushButton* m_btnFit = nullptr;
    QPushButton* m_btnOriginal = nullptr;
    QPushButton* m_btnClose = nullptr;

    QPixmap m_originalPixmap;            ///< 原始图片
    QString m_imagePath;                 ///< 图片路径

    double m_scaleFactor = 1.0;          ///< 当前缩放比例 (0.2 ~ 5.0)
    QPoint m_lastPos;                    ///< 鼠标拖拽位置
    bool m_isDragging = false;           ///< 是否正在拖拽
    bool m_isPanning = false;            ///< 是否正在平移

    static constexpr double MIN_SCALE = 0.2;   ///< 最小缩放 (20%)
    static constexpr double MAX_SCALE = 5.0;   ///< 最大缩放 (500%)
    static constexpr double ZOOM_STEP = 1.25;  ///< 每次缩放步进 (25%)
};

#endif // IMAGELIGHTBOX_H
