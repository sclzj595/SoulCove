#include "ui/editor/EditorSplitView.h"
#include "ui/editor/MyTextEdit.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextDocument>

EditorSplitView::EditorSplitView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部工具栏（标题 + 关闭按钮）
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("splitViewToolbar"));
    toolbar->setFixedHeight(26);
    auto* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(6, 0, 4, 0);
    tbLayout->setSpacing(4);

    m_titleLabel = new QLabel(tr("分栏视图"), toolbar);
    m_titleLabel->setObjectName(QStringLiteral("panelTitle"));
    tbLayout->addWidget(m_titleLabel);
    tbLayout->addStretch();

    m_btnClose = new QPushButton(QStringLiteral("\u2715"), toolbar);  // ✕
    m_btnClose->setFixedSize(20, 20);
    m_btnClose->setToolTip(tr("关闭分栏 (Ctrl+Shift+\\)"));
    m_btnClose->setObjectName(QStringLiteral("iconButton"));
    m_btnClose->setCursor(Qt::PointingHandCursor);
    tbLayout->addWidget(m_btnClose);

    layout->addWidget(toolbar);

    // 分栏编辑器实例
    m_editor = new MyTextEdit(this);
    layout->addWidget(m_editor);

    // 关闭按钮 → 发射 closeRequested
    connect(m_btnClose, &QPushButton::clicked, this, &EditorSplitView::closeRequested);
}

void EditorSplitView::setSourceEditor(MyTextEdit* source)
{
    if (m_sourceEditor == source) return;

    m_sourceEditor = source;

    if (!source) {
        // 清空：断开文档关联
        m_editor->setDocument(new QTextDocument());  // 给一个空文档避免空指针
        m_titleLabel->setText(tr("分栏视图"));
        return;
    }

    // 共享源编辑器的 QTextDocument
    // 注意：QSyntaxHighlighter 绑定在 document 上，共享后高亮自动生效
    m_editor->setDocument(source->document());

    // 同步字体
    m_editor->setFont(source->font());

    // 更新标题
    m_titleLabel->setText(tr("分栏: ") + source->windowTitle());
}
