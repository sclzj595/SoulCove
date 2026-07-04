#ifndef LINENUMBERAREA_H
#define LINENUMBERAREA_H

#include "interfaces/editor/ILineNumber.h"
#include "ui/editor/MyTextEdit.h"

#include <QWidget>
#include <QPaintEvent>

/// @brief 行号区域显示组件
/// 实现ILineNumber接口，独立绘制行号、跟随编辑区滚动同步
class LineNumberArea : public QWidget, public ILineNumber
{
    Q_OBJECT

public:
    explicit LineNumberArea(MyTextEdit* editor);

    // ========== ILineNumber 接口实现 ==========
    void updateLineNumber() override { update(); }
    void setVisible(bool visible) override { QWidget::setVisible(visible); }
    bool isVisible() const override { return QWidget::isVisible(); }
    int areaWidth() const override;
    void updateGeometry(const QRect& editorRect, int width) override;
    QWidget* asWidget() override { return this; }

    // 基类方法（保留兼容）
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    MyTextEdit* m_textEdit;
};

#endif 	// LINENUMBERAREA_H
