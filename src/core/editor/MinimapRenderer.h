#ifndef MINIMAPRENDERER_H
#define MINIMAPRENDERER_H

#include <QObject>
#include <QImage>
#include <QTimer>

class QTextEdit;
class QWidget;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

/// @brief 迷你地图渲染器
///
/// 职责：渲染文档缩略图、处理点击跳转、管理视口指示器。
///
/// 设计说明：
/// - 渲染器模式：从 MyTextEdit 抽取 minimap 相关状态与逻辑
/// - 拥有 m_minimapWidget（子控件，parent 为 QTextEdit）、m_minimapImage、m_minimapUpdateTimer
/// - 通过 QTextEdit* 访问 document()/viewport()/verticalScrollBar()/font()
/// - MyTextEdit 在 eventFilter/resizeEvent/handleTextChanged 中委托给渲染器
class MinimapRenderer : public QObject
{
    Q_OBJECT

public:
    explicit MinimapRenderer(QTextEdit* editor, QObject* parent = nullptr);
    ~MinimapRenderer() override;

    // === 显隐控制 ===
    void setVisible(bool visible);
    bool isVisible() const { return m_visible; }

    /// 获取 minimap 控件宽度（供 MyTextEdit 计算 viewport 右边距）
    int width() const;

    // === 事件处理（由 MyTextEdit 委托） ===

    /// 处理 minimap 鼠标点击（点击位置 → 滚动条比例换算）
    void handleMouseClick(QMouseEvent* event);

    /// 绘制 minimap（由 eventFilter 的 Paint 事件触发）
    void paint(QPaintEvent* event);

    /// 父控件 resize 时更新 minimap 几何位置
    void handleResize(int parentWidth, int parentHeight);

    /// 触发延迟更新（文本变更/滚动时调用）
    void scheduleUpdate();

    /// 立即重建 minimap 图像
    void updateNow();

protected:
    /// 事件过滤器：拦截 minimap widget 的鼠标点击和绘制事件
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    /// 绘制可见区域指示器（半透明矩形）
    void drawViewportIndicator(QPainter& painter, int w, int h,
                               double scaleY, qreal visibleHeight);

    QTextEdit*  m_editor;
    QWidget*    m_minimapWidget = nullptr;
    QImage*     m_minimapImage = nullptr;
    QTimer      m_updateTimer;
    bool        m_visible = true;
};

#endif // MINIMAPRENDERER_H
