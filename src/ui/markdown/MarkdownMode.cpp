#include "ui/markdown/MarkdownMode.h"
#include "ui/editor/MyTextEdit.h"
#include "core/markdown/MaddyParser.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"
#include "ui/markdown/MdTocPanel.h"
#include "ui/markdown/ImageLightBox.h"
#include "core/markdown/MdExporter.h"

#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QTimer>
#include <QScrollBar>
#include <QRegularExpression>
#include <QFileDialog>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTextDocument>
#include "ui/dialog/ModernDialog.h"
#include <QMouseEvent>

// ============================================================
// 构造函数
// ============================================================

MarkdownMode::MarkdownMode(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ===== 主分割器：左侧（编辑器+TOC）| 右侧（预览）=====
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setHandleWidth(2);
    m_mainSplitter->setChildrenCollapsible(false);

    // ===== 左侧分割器：编辑器 | TOC面板 =====
    m_leftSplitter = new QSplitter(Qt::Vertical, m_mainSplitter);
    m_leftSplitter->setHandleWidth(1);
    m_leftSplitter->setChildrenCollapsible(false);

    // 左上侧：源码编辑器
    m_editor = new MyTextEdit(m_leftSplitter);
    m_editor->setObjectName(QStringLiteral("mdEditor"));

    // 左下侧：TOC目录面板
    m_tocPanel = new MdTocPanel(m_leftSplitter);
    m_tocPanel->setMaximumHeight(250);  // 限制TOC面板最大高度
    m_tocPanel->setMinimumHeight(100);  // 最小高度

    // 设置初始比例（编辑器占主要空间）
    m_leftSplitter->setStretchFactor(0, 4);  // 编辑器
    m_leftSplitter->setStretchFactor(1, 1);  // TOC

    // 右侧：预览区
    m_preview = new QTextBrowser(m_mainSplitter);
    m_preview->setObjectName(QStringLiteral("mdPreview"));
    m_preview->setOpenExternalLinks(true);

    // 安装事件过滤器（用于捕获图片点击事件 → 灯箱预览）
    m_preview->installEventFilter(this);

    // 组装主分割器
    m_mainSplitter->addWidget(m_leftSplitter);
    m_mainSplitter->addWidget(m_preview);
    m_mainSplitter->setStretchFactor(0, 1);   // 左侧（编辑器+TOC）
    m_mainSplitter->setStretchFactor(1, 1);   // 右侧（预览）

    // 创建工具栏
    setupToolbar();

    // 组装主布局
    layout->addWidget(m_toolbar);
    layout->addWidget(m_mainSplitter);

    // 默认使用 maddy 三方库解析器
    static MaddyParser defaultParser;
    m_parser = &defaultParser;

    // P1优化: 自适应防抖（根据文档大小动态调整150-500ms）
    auto* debounceTimer = new QTimer(this);
    debounceTimer->setSingleShot(true);
    debounceTimer->setInterval(300); // 默认300ms

    connect(m_editor, &MyTextEdit::textChanged, this, [debounceTimer, this]() {
        // 自适应策略：文档越长，防抖时间越短（避免大文档频繁刷新卡顿）
        int docLength = m_editor->toPlainText().length();
        if (docLength > 10000) {
            debounceTimer->setInterval(500);   // 大文档：500ms
        } else if (docLength > 5000) {
            debounceTimer->setInterval(350);   // 中等文档：350ms
        } else {
            debounceTimer->setInterval(200);   // 小文档：200ms（更流畅）
        }
        debounceTimer->start();
    });

    connect(debounceTimer, &QTimer::timeout, this, [this]() {
        refreshPreview();
        parseToc();  // D5: 刷新时重新解析TOC
    });

    // D4: 滚动同步（编辑器滚动 → 预览区跟随）
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MarkdownMode::onEditorScrolled);

    // 双向滚动同步（预览区滚动 → 编辑器跟随）
    connect(m_preview->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MarkdownMode::onPreviewScrolled);

    // TOC面板点击跳转
    connect(m_tocPanel, &MdTocPanel::tocItemClicked,
            this, &MarkdownMode::onTocItemClicked);

    // 主题切换时完整刷新：预览内容 + 工具栏样式 + TOC面板样式
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MarkdownMode::onThemeChanged);

    // 应用配置中的字体大小（与主编辑器/设置页联动）
    m_editor->setFontSize(ConfigManager::instance().fontSize());

    // 监听配置变更，字体大小变化时同步到 Markdown 编辑器
    // P3-M02 子项2: Markdown 自定义 CSS 变更时刷新预览
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, [this](const QString& key, const QVariant& value) {
        if (key == QStringLiteral("Display/fontSize")) {
            QSignalBlocker blocker(m_editor);
            m_editor->setFontSize(value.toInt());
        } else if (key == QStringLiteral("Markdown/customCss")) {
            // 用户自定义 CSS 变更 → 重新应用 CSS 并刷新预览
            applyPreviewCss();
            refreshPreview();
        }
    });

    // P3-M02 子项2+3: 初始化预览区 CSS（主题预设 + 用户自定义 CSS）
    applyPreviewCss();
}

void MarkdownMode::setupToolbar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setIconSize(QSize(16, 16));

    // 导出按钮
    auto* exportBtn = new QAction(tr("导出"), this);
    exportBtn->setToolTip(tr("导出为 HTML 或 PDF 文档"));
    exportBtn->setStatusTip(tr("Export to HTML/PDF"));
    connect(exportBtn, &QAction::triggered, this, &MarkdownMode::showExportDialog);
    m_toolbar->addAction(exportBtn);

    m_toolbar->addSeparator();

    // 刷新按钮
    auto* refreshBtn = new QAction(tr("刷新"), this);
    refreshBtn->setToolTip(tr("强制刷新预览 (F5)"));
    connect(refreshBtn, &QAction::triggered, this, [this]() {
        refreshPreview();
        parseToc();
    });
    m_toolbar->addAction(refreshBtn);

    m_toolbar->addSeparator();

    // P3-M02 子项4: 复制为富文本按钮
    auto* copyRichBtn = new QAction(tr("复制富文本"), this);
    copyRichBtn->setToolTip(tr("将当前 Markdown 转换为富文本（HTML）并复制到剪贴板，可粘贴到 Word/邮件等"));
    copyRichBtn->setStatusTip(tr("Copy as Rich Text"));
    connect(copyRichBtn, &QAction::triggered, this, &MarkdownMode::copyAsRichText);
    m_toolbar->addAction(copyRichBtn);

    // 添加弹性空间
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    // 标签信息
    auto* infoLabel = new QLabel(tr("Markdown 编辑模式"), m_toolbar);
    infoLabel->setObjectName(QStringLiteral("mdModeLabel"));
    m_toolbar->addWidget(infoLabel);

    // 应用主题样式
    applyToolbarStyle();
}

void MarkdownMode::applyToolbarStyle()
{
    if (!m_toolbar) return;

    const auto& p = ThemeManager::instance().currentPalette();
    QString toolbarBg = p.bgTitleBar.name(QColor::HexRgb);
    QString borderColor = p.borderDefault.name(QColor::HexRgb);
    QString toolFg = p.fgSecondary.name(QColor::HexRgb);
    QString toolHoverBg = p.accentPrimary.name(QColor::HexArgb);
    QString labelColor = p.fgDisabled.name(QColor::HexRgb);

    m_toolbar->setStyleSheet(QStringLiteral(
        "QToolBar {"
        "  background: %1;"
        "  border-bottom: 1px solid %2;"
        "  padding: 4px 8px;"
        "  spacing: 6px;"
        "}"
        "QToolButton {"
        "  border: none; padding: 4px 10px; border-radius: 4px;"
        "  color: %3; font-size: 12px;"
        "}"
        "QToolButton:hover {"
        "  background: %4; color: #fff;"
        "}"
        "QLabel#mdModeLabel {"
        "  color: %5; font-size: 11px; margin-right: 8px;"
        "}"
    ).arg(toolbarBg, borderColor, toolFg, toolHoverBg, labelColor));
}

// ============================================================
// 公共接口实现
// ============================================================

void MarkdownMode::setContent(const QString& text)
{
    m_editor->setPlainText(text);
    refreshPreview();
    parseToc();  // D5: 初始化TOC
}

QString MarkdownMode::content() const
{
    return m_editor->toPlainText();
}

void MarkdownMode::setParser(IMarkdownParser* parser)
{
    if (parser)
        m_parser = parser;
}

void MarkdownMode::refreshPreview()
{
    if (!m_parser) return;
    QString html = m_parser->toHtml(m_editor->toPlainText());

    // 保存刷新前的滚动进度比例，刷新后恢复 (避免 setHtml 重置到顶部导致闪烁/滚动丢失)
    QScrollBar* vScroll = m_preview->verticalScrollBar();
    double ratio = 0.0;
    if (vScroll) {
        int denom = vScroll->maximum() + vScroll->pageStep();
        ratio = (denom > 0) ? static_cast<double>(vScroll->value()) / denom : 0.0;
    }

    // P3-M02 子项3: CSS 由 MarkdownMode 通过 setDefaultStyleSheet 控制
    // （MaddyParser 返回 body-only HTML，不再嵌入 <style>）
    m_preview->setHtml(html);

    // 延迟恢复滚动位置 (等待 QTextDocument 布局更新完成)
    if (vScroll) {
        double r = ratio;
        QTimer::singleShot(0, this, [this, r]() {
            QScrollBar* s = m_preview->verticalScrollBar();
            if (!s) return;
            int denom = s->maximum() + s->pageStep();
            s->setValue(static_cast<int>(r * denom));
        });
    }
}

void MarkdownMode::setFilePath(const QString& filePath)
{
    m_filePath = filePath;
    if (m_tocPanel) {
        m_tocPanel->setFilePath(filePath);
    }
}

void MarkdownMode::onThemeChanged()
{
    // P3-M02 子项3: 主题切换时重新应用 CSS 预设（暗色/浅色）
    applyPreviewCss();

    // 1. 刷新预览内容（CSS由ThemeManager动态生成）
    refreshPreview();

    // 2. 仅刷新工具栏样式（不delete/recreate，避免布局断裂）
    applyToolbarStyle();

    // 3. TOC面板已自行监听 themeChanged 信号，无需手动刷新
}

void MarkdownMode::scrollToHeading(const QString& anchorId)
{
    // 在预览区查找并跳转到指定标题
    QString url = QStringLiteral("#%1").arg(anchorId);
    m_preview->setSource(url);

    // 同时在编辑器中定位到对应行（如果找到）
    for (const auto& item : m_tocItems) {
        if (item.anchorId == anchorId) {
            QTextCursor cursor = m_editor->textCursor();
            cursor.movePosition(QTextCursor::Start);
            cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, item.lineNum);
            m_editor->setTextCursor(cursor);

            // 使用QTextEdit的ensureCursorVisible()将光标位置居中显示
            m_editor->ensureCursorVisible();

            // 手动滚动使光标位于视口中央
            QScrollBar* vScroll = m_editor->verticalScrollBar();
            if (vScroll) {
                int maxVal = vScroll->maximum();
                int pageStep = vScroll->pageStep();
                int targetPos = qBound(0,
                    static_cast<int>(static_cast<double>(item.lineNum) / m_editor->document()->blockCount() * maxVal) - pageStep / 2,
                    maxVal);
                vScroll->setValue(targetPos);
            }
            break;
        }
    }
}

void MarkdownMode::showExportDialog()
{
    // 打开保存对话框，让用户选择格式和路径
    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("导出 Markdown 文档"),
        QStringLiteral(""),
        MdExporter::fileFilter()
    );

    if (filePath.isEmpty()) return;  // 用户取消

    // 创建导出器并执行导出
    MdExporter exporter(this);
    exporter.setParser(m_parser);

    // 配置导出选项
    MdExporter::ExportOptions options;
    options.includeToc = true;

    // 执行导出
    bool success = exporter.exportToFile(m_editor->toPlainText(), filePath, options);

    if (success) {
        ModernDialog::information(
            this,
            tr("导出成功"),
            tr("文档已成功导出到:\n%1").arg(filePath)
        );
    } else {
        ModernDialog::warning(
            this,
            tr("导出失败"),
            tr("无法导出文档，请检查文件路径和权限。")
        );
    }
}

// ============================================================
// 私有槽实现
// ============================================================

void MarkdownMode::onEditorScrolled(int verticalValue)
{
    if (m_syncingScroll) return;  // 防止双向同步死循环

    QScrollBar* editorScroll = m_editor->verticalScrollBar();
    if (!editorScroll) return;

    int denom = editorScroll->maximum() + editorScroll->pageStep();
    if (denom <= 0) return;
    double ratio = static_cast<double>(verticalValue) / denom;

    QScrollBar* previewScroll = m_preview->verticalScrollBar();
    if (!previewScroll) return;

    m_syncingScroll = true;
    int pDenom = previewScroll->maximum() + previewScroll->pageStep();
    previewScroll->setValue(static_cast<int>(ratio * pDenom));
    m_syncingScroll = false;

    // 更新TOC高亮（跟随滚动位置）
    QTextCursor cursor = m_editor->textCursor();
    int currentLine = cursor.blockNumber();
    m_tocPanel->highlightActiveItem(currentLine);
}

void MarkdownMode::onPreviewScrolled(int verticalValue)
{
    if (m_syncingScroll) return;  // 防止双向同步死循环

    QScrollBar* previewScroll = m_preview->verticalScrollBar();
    if (!previewScroll) return;

    int denom = previewScroll->maximum() + previewScroll->pageStep();
    if (denom <= 0) return;
    double ratio = static_cast<double>(verticalValue) / denom;

    QScrollBar* editorScroll = m_editor->verticalScrollBar();
    if (!editorScroll) return;

    m_syncingScroll = true;
    int eDenom = editorScroll->maximum() + editorScroll->pageStep();
    editorScroll->setValue(static_cast<int>(ratio * eDenom));
    m_syncingScroll = false;

    // 更新TOC高亮（跟随滚动位置）
    QTextCursor cursor = m_editor->textCursor();
    int currentLine = cursor.blockNumber();
    m_tocPanel->highlightActiveItem(currentLine);
}

void MarkdownMode::onTocItemClicked(const QString& anchorId, int lineNumber)
{
    Q_UNUSED(lineNumber)
    scrollToHeading(anchorId);
}

// ============================================================
// 事件过滤器：图片点击 → 灯箱预览
// ============================================================

bool MarkdownMode::eventFilter(QObject* obj, QEvent* event)
{
    // 只处理预览区的鼠标点击事件
    if (obj == m_preview && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        if (mouseEvent->button() == Qt::LeftButton) {
            // 获取点击位置的锚点（可能是图片URL）
            QString anchor = m_preview->anchorAt(mouseEvent->pos());

            if (!anchor.isEmpty() && (
                anchor.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) ||
                anchor.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive) ||
                anchor.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive) ||
                anchor.endsWith(QLatin1String(".gif"), Qt::CaseInsensitive) ||
                anchor.endsWith(QLatin1String(".webp"), Qt::CaseInsensitive) ||
                anchor.startsWith(QLatin1String("http://")) ||
                anchor.startsWith(QLatin1String("https://"))
            )) {
                // 是图片链接 → 打开灯箱预览
                ImageLightBox::showImage(this, anchor);
                return true;  // 事件已处理
            }

            // 尝试检测是否点击了<img>标签
            // 通过检查光标位置下的字符格式来判断
            QTextCursor cursor = m_preview->cursorForPosition(mouseEvent->pos());
            QTextCharFormat format = cursor.charFormat();

            if (!format.isImageFormat()) {
                // 可能是点击了图片的父元素，向上查找
                cursor.movePosition(QTextCursor::StartOfBlock);
                while (!cursor.atBlockEnd()) {
                    QTextCharFormat fmt = cursor.charFormat();
                    if (fmt.isImageFormat()) {
                        QString imageName = fmt.toImageFormat().name();
                        if (!imageName.isEmpty()) {
                            ImageLightBox::showImage(this, imageName);
                            return true;
                        }
                    }
                    cursor.movePosition(QTextCursor::NextCharacter);
                }
            } else {
                // 直接是图片格式
                QString imageName = format.toImageFormat().name();
                ImageLightBox::showImage(this, imageName);
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

// ============================================================
// D5: TOC目录导航功能
// ============================================================

void MarkdownMode::parseToc()
{
    m_tocItems.clear();

    QString content = m_editor->toPlainText();
    QStringList lines = content.split('\n');

    static QRegularExpression hRe(QStringLiteral(R"(^(#{1,6})\s+(.+)$)"));

    for (int i = 0; i < lines.size(); ++i) {
        auto match = hRe.match(lines[i]);
        if (match.hasMatch()) {
            TocItem item;
            item.level = match.captured(1).length();
            item.title = match.captured(2);

            // 生成anchor ID（与MarkdownParser保持一致）
            item.anchorId = item.title.toLower()
                .remove(QRegularExpression(QStringLiteral("[^a-z0-9\\x{4e00}-\\x{9fa5}\\-_]")))
                .replace(QRegularExpression(QStringLiteral("_{2,}|-{2,}")), QStringLiteral("-"))
                .replace(QRegularExpression(QStringLiteral("^[-_]|[-_]$")), QString());

            item.lineNum = i;

            m_tocItems.append(item);
        }
    }

    // 更新TOC面板显示
    QList<MdTocPanel::TocEntry> entries;
    for (const auto& item : m_tocItems) {
        MdTocPanel::TocEntry entry;
        entry.level = item.level;
        entry.title = item.title;
        entry.anchorId = item.anchorId;
        entry.lineNumber = item.lineNum;
        entries.append(entry);
    }
    m_tocPanel->updateToc(entries);
}

// ============================================================
// P3-M02 子项2+3: CSS 预设与用户自定义 CSS
// ============================================================

QString MarkdownMode::darkCssPreset()
{
    // 暗色主题 CSS 预设：深色背景 + 浅色文字 + 链接色 + 代码块样式
    // QTextBrowser 兼容的朴素 CSS2.1（不使用 var()/gradient/box-shadow/:hover/transition）
    return QStringLiteral(
        "body { font-family: 'Microsoft YaHei','Segoe UI',sans-serif; font-size: 14px; "
        "line-height: 1.7; color: #e6e6e6; background-color: #1e1e1e; margin: 0; padding: 8px 16px; }"
        "h1,h2,h3,h4,h5,h6 { color: #ffffff; font-weight: 600; line-height: 1.3; "
        "margin-top: 20px; margin-bottom: 8px; }"
        "h1 { font-size: 24px; border-bottom: 2px solid #9B59B6; padding-bottom: 6px; }"
        "h2 { font-size: 20px; border-bottom: 1px solid #444; padding-bottom: 4px; }"
        "h3 { font-size: 17px; }"
        "h4 { font-size: 15px; }"
        "h5,h6 { font-size: 14px; color: #aaa; }"
        "p { margin: 8px 0; }"
        "a { color: #569CD6; text-decoration: none; }"
        "strong { font-weight: 700; color: #ffffff; }"
        "em { font-style: italic; color: #c586c0; }"
        "del { color: #808080; text-decoration: line-through; }"
        "code { background-color: #2d2d30; color: #ce9178; padding: 2px 5px; border-radius: 3px; "
        "font-family: 'Consolas','Courier New',monospace; font-size: 13px; }"
        "pre { background-color: #1e1e1e; color: #d4d4d4; padding: 12px; "
        "border: 1px solid #3c3c3c; border-radius: 6px; margin: 10px 0; }"
        "pre code { background-color: transparent; color: #d4d4d4; padding: 0; border: none; "
        "display: block; white-space: pre; font-size: 13px; line-height: 1.5; }"
        "blockquote { border-left: 4px solid #569CD6; padding: 6px 14px; margin: 10px 0; "
        "color: #aaa; background-color: #252526; }"
        "table { border-collapse: collapse; margin: 10px 0; }"
        "th,td { border: 1px solid #3c3c3c; padding: 6px 12px; }"
        "th { background-color: #2d2d30; color: #569CD6; font-weight: 600; }"
        "hr { border: none; border-top: 1px solid #3c3c3c; margin: 18px 0; }"
        "ul,ol { padding-left: 24px; margin: 6px 0; }"
        "li { margin: 3px 0; }"
        "img { max-width: 100%; }"
        ".hl-kw { color: #569CD6; font-weight: 600; }"
        ".hl-str { color: #CE9178; }"
        ".hl-num { color: #B5CEA8; }"
        ".hl-cmt { color: #6A9955; font-style: italic; }"
        ".hl-pp { color: #C586C0; }"
        ".hl-type { color: #4EC9B0; }"
        ".hl-fn { color: #DCDCAA; }"
    );
}

QString MarkdownMode::lightCssPreset()
{
    // 浅色主题 CSS 预设：浅色背景 + 深色文字 + 链接色 + 代码块样式
    return QStringLiteral(
        "body { font-family: 'Microsoft YaHei','Segoe UI',sans-serif; font-size: 14px; "
        "line-height: 1.7; color: #2c3e50; background-color: #ffffff; margin: 0; padding: 8px 16px; }"
        "h1,h2,h3,h4,h5,h6 { color: #2c3e50; font-weight: 600; line-height: 1.3; "
        "margin-top: 20px; margin-bottom: 8px; }"
        "h1 { font-size: 24px; border-bottom: 2px solid #9B59B6; padding-bottom: 6px; color: #8e44ad; }"
        "h2 { font-size: 20px; border-bottom: 1px solid #e0d0e8; padding-bottom: 4px; color: #9B59B6; }"
        "h3 { font-size: 17px; color: #7d3c98; }"
        "h4 { font-size: 15px; color: #666; }"
        "h5,h6 { font-size: 14px; color: #888; }"
        "p { margin: 8px 0; }"
        "a { color: #9B59B6; text-decoration: none; }"
        "strong { font-weight: 700; color: #2c3e50; }"
        "em { font-style: italic; color: #9B59B6; }"
        "del { color: #999; text-decoration: line-through; }"
        "code { background-color: #f0eef0; color: #c7254e; padding: 2px 5px; border-radius: 3px; "
        "font-family: 'Consolas','Courier New',monospace; font-size: 13px; }"
        "pre { background-color: #f6f8fa; color: #24292e; padding: 12px; "
        "border: 1px solid #e1e4e8; border-radius: 6px; margin: 10px 0; }"
        "pre code { background-color: transparent; color: #24292e; padding: 0; border: none; "
        "display: block; white-space: pre; font-size: 13px; line-height: 1.5; }"
        "blockquote { border-left: 4px solid #9B59B6; padding: 6px 14px; margin: 10px 0; "
        "color: #666; background-color: #faf8fa; }"
        "table { border-collapse: collapse; margin: 10px 0; }"
        "th,td { border: 1px solid #e1e4e8; padding: 6px 12px; }"
        "th { background-color: #f6f8fa; color: #9B59B6; font-weight: 600; }"
        "hr { border: none; border-top: 1px solid #e1e4e8; margin: 18px 0; }"
        "ul,ol { padding-left: 24px; margin: 6px 0; }"
        "li { margin: 3px 0; }"
        "img { max-width: 100%; }"
        ".hl-kw { color: #0000FF; font-weight: 600; }"
        ".hl-str { color: #A31515; }"
        ".hl-num { color: #098658; }"
        ".hl-cmt { color: #008000; font-style: italic; }"
        ".hl-pp { color: #AF00DB; }"
        ".hl-type { color: #267F99; }"
        ".hl-fn { color: #795E26; }"
    );
}

QString MarkdownMode::buildPreviewCss() const
{
    // P3-M02 子项3: 根据当前主题选择预设 CSS
    // 亮/暗主题判定 (与 ThemeManager/MaddyParser 一致: bgEditor.lightness() > 128)
    const auto& p = ThemeManager::instance().currentPalette();
    bool isLight = p.bgEditor.lightness() > 128;
    QString preset = isLight ? lightCssPreset() : darkCssPreset();

    // P3-M02 子项2: 用户自定义 CSS 叠加在主题预设之上（用户 CSS 优先级更高）
    QString userCss = ConfigManager::instance().markdownCustomCss();
    if (!userCss.isEmpty()) {
        return preset + QStringLiteral("\n/* === 用户自定义 CSS === */\n") + userCss;
    }
    return preset;
}

void MarkdownMode::applyPreviewCss()
{
    if (!m_preview || !m_preview->document()) return;
    // P3-M02 子项2+3: 通过 setDefaultStyleSheet 应用 CSS（主题预设 + 用户自定义）
    m_preview->document()->setDefaultStyleSheet(buildPreviewCss());
}

// ============================================================
// P3-M02 子项4: 复制为富文本
// ============================================================

void MarkdownMode::copyAsRichText()
{
    QString markdown = m_editor->toPlainText();
    if (markdown.isEmpty()) {
        ModernDialog::information(
            this,
            tr("提示"),
            tr("编辑器内容为空，无可复制内容。")
        );
        return;
    }

    // 调用 MdExporter 转换为 HTML 字符串（带完整样式，便于粘贴到 Word/邮件等）
    MdExporter exporter(this);
    exporter.setParser(m_parser);
    QString richText = exporter.copyAsRichText(markdown);

    // 写入剪贴板（富文本 HTML 格式）
    QApplication::clipboard()->setText(richText);
    // 同时设置富文本 MIME（支持 Word/邮件客户端识别 HTML）
    QMimeData* mimeData = new QMimeData;
    mimeData->setHtml(richText);
    mimeData->setText(richText);
    QApplication::clipboard()->setMimeData(mimeData);

    ModernDialog::information(
        this,
        tr("已复制"),
        tr("已复制为富文本，可粘贴到 Word/邮件等支持 HTML 的应用。")
    );
}
