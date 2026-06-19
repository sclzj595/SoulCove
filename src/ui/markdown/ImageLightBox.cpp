#include "ui/markdown/ImageLightBox.h"
#include "core/config/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QScreen>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTimer>
#include <QDebug>

// ============================================================
// 构造函数
// ============================================================

ImageLightBox::ImageLightBox(QWidget* parent, const QString& imagePath, const QPixmap& pixmap)
    : QDialog(parent)
    , m_imagePath(imagePath)
    , m_originalPixmap(pixmap)
{
    // 窗口设置
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setMinimumSize(400, 300);

    // 如果没有传入pixmap，尝试从路径加载
    if (m_originalPixmap.isNull() && !imagePath.isEmpty()) {
        m_originalPixmap.load(imagePath);
    }

    setupUI();

    // 默认适应窗口
    fitToWindow();
}

void ImageLightBox::setupUI()
{
    const auto& p = ThemeManager::instance().currentPalette();
    bool isLight = p.bgEditor.lightness() > 128;

    // 主布局（垂直）
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ===== 背景遮罩 =====
    // 灯箱始终使用半透明暗色遮罩（这是灯箱的设计本质）
    // 但透明度根据主题明暗微调
    int alpha = isLight ? 70 : 85;
    auto* bgWidget = new QWidget(this);
    bgWidget->setObjectName(QStringLiteral("lightbox_bg"));
    bgWidget->setStyleSheet(QStringLiteral(
        "#lightbox_bg {"
        "  background-color: rgba(0, 0, 0, %1);"
        "}"
    ).arg(QString::number(alpha)));
    bgWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* bgLayout = new QVBoxLayout(bgWidget);
    bgLayout->setContentsMargins(20, 20, 20, 60);

    // ===== 图片显示区域 =====
    m_imageLabel = new QLabel(bgWidget);
    m_imageLabel->setObjectName(QStringLiteral("lightbox_image"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(false);
    m_imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_imageLabel->setCursor(Qt::OpenHandCursor);
    m_imageLabel->setStyleSheet(QStringLiteral(
        "#lightbox_image { background: transparent; border: none; }"
    ));

    // 图片阴影效果
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(30);
    shadow->setColor(QColor(0, 0, 0, isLight ? 100 : 160));
    shadow->setOffset(0, 8);
    m_imageLabel->setGraphicsEffect(shadow);

    bgLayout->addWidget(m_imageLabel);

    // ===== 底部工具栏（跟随主题强调色）=====
    QString accent = p.accentPrimary.name(QColor::HexArgb);
    QString accentHover = p.accentHover.name(QColor::HexArgb);
    QString toolbarBg = isLight
        ? QStringLiteral("rgba(245, 245, 245, 0.95)")
        : QStringLiteral("rgba(30, 30, 30, 0.95)");
    QString toolbarBorder = isLight
        ? QStringLiteral("rgba(200, 200, 200, 0.5)")
        : QStringLiteral("rgba(255, 255, 255, 0.1)");
    QString labelColor = isLight ? QStringLiteral("#333333") : QStringLiteral("#e0e0e0");
    QString errorColor = isLight ? QStringLiteral("rgba(231, 76, 60, 0.85)")
                                : QStringLiteral("rgba(231, 76, 60, 0.7)");

    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("lightbox_toolbar"));
    toolbar->setFixedHeight(50);
    toolbar->setStyleSheet(QStringLiteral(
        "#lightbox_toolbar {"
        "  background-color: %1;"
        "  border-top: 1px solid %2;"
        "}"
    ).arg(toolbarBg, toolbarBorder));

    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(15, 5, 15, 5);
    toolbarLayout->setSpacing(10);

    // 缩放比例标签
    m_zoomLabel = new QLabel(tr("100%"), toolbar);
    m_zoomLabel->setObjectName(QStringLiteral("zoom_label"));
    m_zoomLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 13px; font-weight: bold; min-width: 50px;"
    ).arg(labelColor));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    toolbarLayout->addWidget(m_zoomLabel);

    toolbarLayout->addSpacing(20);

    // 放大按钮（使用主题强调色）
    m_btnZoomIn = new QPushButton(tr("放大 (+)"), toolbar);
    m_btnZoomIn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1; color: #fff; border: 1px solid %2;"
        "  padding: 6px 14px; border-radius: 4px; font-size: 12px; min-width: 70px;"
        "}"
        "QPushButton:hover { background: %3; }"
    ).arg(accent, accent, accentHover));
    connect(m_btnZoomIn, &QPushButton::clicked, this, &ImageLightBox::zoomIn);
    toolbarLayout->addWidget(m_btnZoomIn);

    // 缩小按钮
    m_btnZoomOut = new QPushButton(tr("缩小 (-)"), toolbar);
    m_btnZoomOut->setStyleSheet(m_btnZoomIn->styleSheet());
    connect(m_btnZoomOut, &QPushButton::clicked, this, &ImageLightBox::zoomOut);
    toolbarLayout->addWidget(m_btnZoomOut);

    toolbarLayout->addSpacing(10);

    // 适应窗口 / 原始大小按钮（中性灰色）→ 跟随主题边框色
    QString neutralBg = isLight
        ? QStringLiteral("rgba(120, 120, 120, 0.15)")
        : QStringLiteral("rgba(100, 100, 100, 0.3)");
    QString neutralBorder = isLight
        ? QStringLiteral("rgba(120, 120, 120, 0.4)")
        : QStringLiteral("rgba(150, 150, 150, 0.6)");
    QString neutralHover = isLight
        ? QStringLiteral("rgba(120, 120, 120, 0.3)")
        : QStringLiteral("rgba(100, 100, 100, 0.5)");

    m_btnFit = new QPushButton(tr("适应窗口"), toolbar);
    m_btnFit->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1; color: #fff; border: 1px solid %2;"
        "  padding: 6px 14px; border-radius: 4px; font-size: 12px;"
        "}"
        "QPushButton:hover { background: %3; }"
    ).arg(neutralBg, neutralBorder, neutralHover));
    connect(m_btnFit, &QPushButton::clicked, this, &ImageLightBox::fitToWindow);
    toolbarLayout->addWidget(m_btnFit);

    // 原始大小按钮
    m_btnOriginal = new QPushButton(tr("原始大小"), toolbar);
    m_btnOriginal->setStyleSheet(m_btnFit->styleSheet());
    connect(m_btnOriginal, &QPushButton::clicked, this, &ImageLightBox::originalSize);
    toolbarLayout->addWidget(m_btnOriginal);

    toolbarLayout->addStretch();

    // 关闭按钮（始终红色，但浅色主题下稍作调整）
    m_btnClose = new QPushButton(tr("关闭 (Esc)"), toolbar);
    m_btnClose->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1; color: #fff; border: 1px solid rgba(231, 76, 60, 0.9);"
        "  padding: 6px 18px; border-radius: 4px; font-size: 12px; font-weight: bold;"
        "}"
        "QPushButton:hover { background: rgba(231, 76, 60, 0.9); }"
    ).arg(errorColor));
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::close);
    toolbarLayout->addWidget(m_btnClose);

    // 组装主布局
    mainLayout->addWidget(bgWidget, 1);
    mainLayout->addWidget(toolbar);
}

// ============================================================
// 静态工厂方法
// ============================================================

void ImageLightBox::showImage(QWidget* parent, const QString& imageUrl)
{
    auto* lightbox = new ImageLightBox(parent, imageUrl);
    lightbox->exec();
    lightbox->deleteLater();
}

// ============================================================
// 公共槽：缩放控制
// ============================================================

void ImageLightBox::zoomIn()
{
    double newScale = m_scaleFactor * ZOOM_STEP;
    if (newScale <= MAX_SCALE) {
        m_scaleFactor = newScale;
        updateImage();
    }
}

void ImageLightBox::zoomOut()
{
    double newScale = m_scaleFactor / ZOOM_STEP;
    if (newScale >= MIN_SCALE) {
        m_scaleFactor = newScale;
        updateImage();
    }
}

void ImageLightBox::fitToWindow()
{
    if (m_originalPixmap.isNull()) return;

    QSize availableSize = size() - QSize(40, 120);  // 减去边距和工具栏
    if (availableSize.width() <= 0 || availableSize.height() <= 0) return;

    double scaleX = static_cast<double>(availableSize.width()) / m_originalPixmap.width();
    double scaleY = static_cast<double>(availableSize.height()) / m_originalPixmap.height();
    m_scaleFactor = qMin(scaleX, scaleY);

    // 限制在合理范围内
    m_scaleFactor = qBound(MIN_SCALE, m_scaleFactor, MAX_SCALE);

    updateImage();
}

void ImageLightBox::originalSize()
{
    m_scaleFactor = 1.0;
    updateImage();
}

void ImageLightBox::resetZoom()
{
    m_scaleFactor = 1.0;
    centerImage();
    updateImage();
}

// ============================================================
// 事件处理
// ============================================================

void ImageLightBox::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // 检查是否点击在图片上
        if (m_imageLabel->geometry().contains(event->pos())) {
            m_isDragging = true;
            m_lastPos = event->pos();
            m_imageLabel->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }

        // 点击背景关闭
        close();
    }

    QDialog::mousePressEvent(event);
}

void ImageLightBox::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->pos() - m_lastPos;
        m_lastPos = event->pos();

        // 移动图片位置（通过移动label实现）
        QPoint currentPos = m_imageLabel->pos();
        m_imageLabel->move(currentPos + delta);

        event->accept();
        return;
    }

    QDialog::mouseMoveEvent(event);
}

void ImageLightBox::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        m_imageLabel->setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    QDialog::mouseReleaseEvent(event);
}

void ImageLightBox::wheelEvent(QWheelEvent* event)
{
    // 鼠标滚轮缩放（以鼠标位置为中心）
    double delta = event->angleDelta().y() > 0 ? ZOOM_STEP : 1.0 / ZOOM_STEP;
    double newScale = m_scaleFactor * delta;

    if (newScale >= MIN_SCALE && newScale <= MAX_SCALE) {
        m_scaleFactor = newScale;
        updateImage();
    }

    event->accept();
}

void ImageLightBox::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        break;
    case Qt::Key_Minus:
        zoomOut();
        break;
    case Qt::Key_0:
        resetZoom();
        break;
    case Qt::Key_F:
        fitToWindow();
        break;
    default:
        QDialog::keyPressEvent(event);
        break;
    }
}

void ImageLightBox::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);

    // 如果当前是"适应窗口"模式，重新计算
    if (!m_isDragging) {
        // 可选：自动重新适应窗口
        // fitToWindow();
    }
}

void ImageLightBox::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // 居中显示在父窗口或屏幕中心
    if (parentWidget()) {
        QRect parentGeo = parentWidget()->geometry();
        move(parentGeo.x(), parentGeo.y());
        resize(parentGeo.size());
    } else {
        // 使用主屏幕居中
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeo = screen->availableGeometry();
            move(screenGeo.topLeft());
            resize(screenGeo.size());
        }
    }

    // 初始适应窗口
    QTimer::singleShot(100, this, &ImageLightBox::fitToWindow);
}

// ============================================================
// 私有方法
// ============================================================

void ImageLightBox::updateImage()
{
    if (m_originalPixmap.isNull()) return;

    // 计算缩放后的尺寸
    int newWidth = static_cast<int>(m_originalPixmap.width() * m_scaleFactor);
    int newHeight = static_cast<int>(m_originalPixmap.height() * m_scaleFactor);

    // 平滑缩放
    QPixmap scaled = m_originalPixmap.scaled(
        newWidth, newHeight,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );

    m_imageLabel->setPixmap(scaled);
    m_imageLabel->adjustSize();  // 根据pixmap调整label大小

    // 更新缩放标签
    updateZoomLabel();

    // 如果不是拖拽状态，居中显示
    if (!m_isDragging) {
        centerImage();
    }
}

void ImageLightBox::updateZoomLabel()
{
    int percent = static_cast<int>(m_scaleFactor * 100);
    m_zoomLabel->setText(QStringLiteral("%1%").arg(percent));
}

void ImageLightBox::centerImage()
{
    if (!m_imageLabel) return;

    // 获取当前pixmap（返回值，非指针）
    QPixmap currentPix = m_imageLabel->pixmap();
    if (currentPix.isNull()) return;

    // 将图片居中显示在可用区域内
    QSize parentSize = parentWidget() ? parentWidget()->size() : size();
    QSize imageSize = currentPix.size();

    int x = (parentSize.width() - imageSize.width()) / 2;
    int y = (parentSize.height() - imageSize.height()) / 2 - 25;  // 工具栏偏移

    m_imageLabel->move(qMax(0, x), qMax(0, y));
}
