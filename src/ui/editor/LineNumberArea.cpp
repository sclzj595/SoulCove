#include "ui/editor/LineNumberArea.h"

#include <QPainter>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>

LineNumberArea::LineNumberArea(MyTextEdit *editor)
    : QWidget(editor), m_textEdit(editor)
{
    // 样式由全局QSS和MyTextEdit::lineNumberAreaPaintEvent控制，不设内联样式
}

int LineNumberArea::areaWidth() const
{
    return m_textEdit->lineNumberAreaWidth();
}

void LineNumberArea::updateGeometry(const QRect& editorRect, int width)
{
    setGeometry(QRect(editorRect.left(), editorRect.top(), width, editorRect.height()));
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_textEdit->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event)
{
    m_textEdit->lineNumberAreaPaintEvent(event);
}

void LineNumberArea::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_textEdit->lineNumberAreaClicked(event->pos(), width());
    }
    QWidget::mousePressEvent(event);
}
