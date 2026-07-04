#include "ui/markdown/HtmlPreviewMode.h"
#include "ui/editor/MyTextEdit.h"
#include "core/config/ThemeManager.h"
#include "core/markdown/HtmlCssResolver.h"
#include "Logger.hpp"

#include <QTextBrowser>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTimer>

HtmlPreviewMode::HtmlPreviewMode(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    setupToolbar();
    applyThemeStyle();
    updateViewVisibility();

    // 防抖定时器：编辑器内容变更后 300ms 刷新预览
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(300);
    connect(&m_debounceTimer, &QTimer::timeout, this, &HtmlPreviewMode::refreshPreview);

    // 主题切换刷新
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HtmlPreviewMode::onThemeChanged);
}

void HtmlPreviewMode::setupUi()
{
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);

    // 左侧：HTML 源码编辑器
    m_editor = new MyTextEdit(m_mainSplitter);
    m_editor->enableSyntaxHighlighting(QStringLiteral("html"));

    // 右侧：HTML 预览（QTextBrowser）
    m_preview = new QTextBrowser(m_mainSplitter);
    m_preview->setObjectName(QStringLiteral("htmlPreview"));
    m_preview->setOpenExternalLinks(true);

    m_mainSplitter->addWidget(m_editor);
    m_mainSplitter->addWidget(m_preview);
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({500, 500});

    // 主布局：工具栏 + 分屏
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_toolbar = new QToolBar(this));
    mainLayout->addWidget(m_mainSplitter);

    // 编辑器内容变更 → 防抖刷新预览
    connect(m_editor, &MyTextEdit::textChanged, this, [this]() {
        m_debounceTimer.start();
        emit contentChanged();
    });
}

void HtmlPreviewMode::setupToolbar()
{
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));

    // 视图模式切换组（互斥）
    QActionGroup* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);

    m_actSource = new QAction(tr("源码"), this);
    m_actSource->setCheckable(true);
    m_actSource->setToolTip(tr("仅显示 HTML 源码"));
    modeGroup->addAction(m_actSource);

    m_actSplit = new QAction(tr("分屏"), this);
    m_actSplit->setCheckable(true);
    m_actSplit->setToolTip(tr("源码 + 预览分屏"));
    m_actSplit->setChecked(true);  // 默认分屏
    modeGroup->addAction(m_actSplit);

    m_actPreview = new QAction(tr("预览"), this);
    m_actPreview->setCheckable(true);
    m_actPreview->setToolTip(tr("仅显示渲染预览"));
    modeGroup->addAction(m_actPreview);

    m_toolbar->addAction(m_actSource);
    m_toolbar->addAction(m_actSplit);
    m_toolbar->addAction(m_actPreview);

    m_toolbar->addSeparator();

    // 手动刷新
    m_actRefresh = new QAction(tr("刷新"), this);
    m_actRefresh->setToolTip(tr("刷新预览 (F5)"));
    m_toolbar->addAction(m_actRefresh);

    // 连接信号
    connect(m_actSource, &QAction::triggered, this, [this]() {
        setViewMode(ViewMode::SourceOnly);
    });
    connect(m_actSplit, &QAction::triggered, this, [this]() {
        setViewMode(ViewMode::SplitView);
    });
    connect(m_actPreview, &QAction::triggered, this, [this]() {
        setViewMode(ViewMode::PreviewOnly);
    });
    connect(m_actRefresh, &QAction::triggered, this, &HtmlPreviewMode::refreshPreview);
}

void HtmlPreviewMode::applyThemeStyle()
{
    const auto& palette = ThemeManager::instance().currentPalette();

    // 工具栏样式：紧凑、现代
    m_toolbar->setStyleSheet(QStringLiteral(
        "QToolBar { background: %1; border: none; border-bottom: 1px solid %2; "
        "padding: 2px 4px; spacing: 2px; }"
        "QToolBar QToolButton { background: transparent; border: none; "
        "padding: 4px 10px; border-radius: 4px; color: %3; font-size: 12px; }"
        "QToolBar QToolButton:hover { background: %4; }"
        "QToolBar QToolButton:checked { background: %5; color: %6; }"
    ).arg(palette.bgEditor.name(),
          palette.borderDefault.name(),
          palette.fgPrimary.name(),
          palette.bgHover.name(),
          palette.accentPrimary.name(),
          palette.fgOnAccent.name()));

    // 预览区背景跟随主题
    m_preview->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: %1; border: none; border-left: 1px solid %2; }"
    ).arg(palette.bgEditor.name(), palette.borderDefault.name()));
}

void HtmlPreviewMode::updateViewVisibility()
{
    switch (m_viewMode) {
    case ViewMode::SourceOnly:
        m_editor->show();
        m_preview->hide();
        break;
    case ViewMode::PreviewOnly:
        m_editor->hide();
        m_preview->show();
        break;
    case ViewMode::SplitView:
        m_editor->show();
        m_preview->show();
        break;
    }
}

void HtmlPreviewMode::setContent(const QString& text)
{
    m_editor->setPlainText(text);
    refreshPreview();
}

QString HtmlPreviewMode::content() const
{
    return m_editor->toPlainText();
}

void HtmlPreviewMode::refreshPreview()
{
    QString html = m_editor->toPlainText();
    if (html.isEmpty()) {
        m_preview->setHtml(QString());
        return;
    }

    // 解析 <link> 引用的本地 CSS 文件并内联注入
    html = HtmlCssResolver::resolveExternalCss(html, m_filePath, m_workDir);

    // 保存滚动比例（避免 setHtml 重置到顶部）
    QScrollBar* vScroll = m_preview->verticalScrollBar();
    double ratio = 0.0;
    if (vScroll) {
        int denom = vScroll->maximum() + vScroll->pageStep();
        ratio = (denom > 0) ? static_cast<double>(vScroll->value()) / denom : 0.0;
    }

    m_preview->setHtml(html);

    // 延迟恢复滚动位置
    if (vScroll) {
        QTimer::singleShot(0, this, [this, ratio]() {
            QScrollBar* s = m_preview->verticalScrollBar();
            if (!s) return;
            int denom = s->maximum() + s->pageStep();
            s->setValue(static_cast<int>(ratio * denom));
        });
    }
}

void HtmlPreviewMode::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    updateViewVisibility();

    // 切换到预览模式时立即刷新
    if (mode == ViewMode::PreviewOnly || mode == ViewMode::SplitView) {
        refreshPreview();
    }
}

void HtmlPreviewMode::onThemeChanged()
{
    applyThemeStyle();
    refreshPreview();
}
