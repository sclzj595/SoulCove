#include "core/editor/MinimapRenderer.h"
#include "core/config/ThemeManager.h"

#include <QTextEdit>
#include <QWidget>
#include <QPainter>
#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QFont>

// ========== 构造 / 析构 ==========

MinimapRenderer::MinimapRenderer(QTextEdit* editor, QObject* parent)
    : QObject(parent)
    , m_editor(editor)
{
    // 创建 minimap 子控件（parent 为 QTextEdit，渲染在编辑器右侧）
    m_minimapWidget = new QWidget(m_editor);
    m_minimapWidget->setObjectName(QStringLiteral("minimap"));
    m_minimapWidget->setFixedWidth(80);
    m_minimapWidget->setCursor(Qt::PointingHandCursor);

    // 安装事件过滤器：拦截 minimap widget 的鼠标点击和绘制事件
    // 由 MinimapRenderer::eventFilter 统一处理，MyTextEdit 不再直接处理 minimap 事件
    m_minimapWidget->installEventFilter(this);

    // 延迟更新定时器（文本变更时延迟 200ms 再更新，避免频繁重绘）
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(200);
    connect(&m_updateTimer, &QTimer::timeout, this, &MinimapRenderer::updateNow);
}

MinimapRenderer::~MinimapRenderer()
{
    delete m_minimapImage;
}

// === 显隐控制 ===

void MinimapRenderer::setVisible(bool visible)
{
    m_visible = visible;
    if (m_minimapWidget) {
        m_minimapWidget->setVisible(visible);
        if (visible) updateNow();
    }
}

int MinimapRenderer::width() const
{
    return (m_visible && m_minimapWidget) ? m_minimapWidget->width() : 0;
}

// === 事件处理 ===

bool MinimapRenderer::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_minimapWidget) {
        return QObject::eventFilter(obj, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
        handleMouseClick(static_cast<QMouseEvent*>(event));
        return true;  // 事件已处理，阻止默认传递
    case QEvent::Paint:
        paint(static_cast<QPaintEvent*>(event));
        return true;  // 自绘 minimap，阻止 QWidget 默认空绘制
    default:
        return QObject::eventFilter(obj, event);
    }
}

void MinimapRenderer::handleMouseClick(QMouseEvent* event)
{
    if (!m_minimapWidget) return;

    // 点击位置 → 文档滚动位置（比例换算）
    QScrollBar* vBar = m_editor->verticalScrollBar();
    int scrollMax = vBar->maximum();
    int minimapHeight = m_minimapWidget->height();

    if (scrollMax <= 0 || minimapHeight <= 0) return;

    double ratio = static_cast<double>(event->pos().y()) / minimapHeight;
    int targetScroll = static_cast<int>(ratio * scrollMax);

    vBar->setValue(targetScroll);
}

void MinimapRenderer::paint(QPaintEvent* event)
{
    if (!m_minimapImage || m_minimapImage->isNull()) return;

    QPainter painter(m_minimapWidget);
    painter.drawImage(event->rect(), *m_minimapImage, event->rect());
}

void MinimapRenderer::handleResize(int parentWidth, int parentHeight)
{
    if (!m_minimapWidget || !m_visible) return;

    // minimapWidget 是 editor 的子控件，定位到右侧
    int mapW = m_minimapWidget->width();
    m_minimapWidget->setGeometry(parentWidth - mapW, 0, mapW, parentHeight);
    m_minimapWidget->show();
    m_updateTimer.start(100);
}

void MinimapRenderer::scheduleUpdate()
{
    if (m_visible) m_updateTimer.start();
}

// === 渲染 ===

void MinimapRenderer::updateNow()
{
    if (!m_minimapWidget || !m_visible) return;

    int w = m_minimapWidget->width();
    int h = m_minimapWidget->height();

    if (w <= 0 || h <= 0) return;

    // 计算文档总高度和可见区域比例
    QAbstractTextDocumentLayout* layout = m_editor->document()->documentLayout();
    qreal docHeight = layout->documentSize().height();
    qreal visibleHeight = m_editor->viewport()->height();

    if (docHeight <= 0) {
        m_minimapWidget->update();
        return;
    }

    // 缩放比例
    double scaleY = static_cast<double>(h) / docHeight;

    // 创建缩略图
    delete m_minimapImage;
    m_minimapImage = new QImage(w, h, QImage::Format_RGB32);

    // 使用主题编辑器背景色（适配亮/暗模式）
    const auto& themePalette = ThemeManager::instance().currentPalette();
    m_minimapImage->fill(themePalette.bgEditor);

    QPainter painter(m_minimapImage);

    // 使用缩小的字体渲染文本
    QFont miniFont = m_editor->font();
    miniFont.setPointSize(1);  // 极小字体
    painter.setFont(miniFont);

    // 渲染每一行（简化：只画文字颜色，保留语法高亮色相）
    QTextBlock block = m_editor->document()->firstBlock();
    while (block.isValid()) {
        QRectF blockRect = layout->blockBoundingRect(block);
        int y = static_cast<int>(blockRect.top() * scaleY);
        int lineH = qMax(1, static_cast<int>(blockRect.height() * scaleY));

        QString text = block.text();
        if (!text.isEmpty()) {
            QColor fgColor = themePalette.fgPrimary;
            painter.setPen(fgColor);
            QString displayText = text.left(w / 2);
            painter.drawText(1, y + lineH - 1, displayText);
        }

        block = block.next();
    }

    // 绘制可见区域指示器（半透明矩形）
    drawViewportIndicator(painter, w, h, scaleY, visibleHeight);

    painter.end();

    // 触发重绘
    m_minimapWidget->update();
}

void MinimapRenderer::drawViewportIndicator(QPainter& painter, int w, int h,
                                            double scaleY, qreal visibleHeight)
{
    QScrollBar* vBar = m_editor->verticalScrollBar();
    int scrollMax = vBar->maximum();
    int viewH = qMax(4, static_cast<int>(visibleHeight * scaleY));
    int viewY;
    if (scrollMax > 0) {
        viewY = static_cast<int>(static_cast<double>(vBar->value()) / scrollMax * (h - viewH));
    } else {
        viewY = 0;
    }

    // 根据主题明暗选择指示器颜色
    const auto& palette = ThemeManager::instance().currentPalette();
    bool isLight = palette.bgEditor.lightness() > 128;
    QColor fillColor = isLight ? QColor(0, 0, 0, 20) : QColor(255, 255, 255, 25);
    QColor borderColor = isLight ? QColor(0, 0, 0, 50) : QColor(255, 255, 255, 80);

    painter.fillRect(0, viewY, w, viewH, fillColor);
    painter.setPen(borderColor);
    painter.drawRect(0, viewY, w - 1, viewH - 1);
}
