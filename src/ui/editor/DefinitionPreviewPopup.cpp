#include "ui/editor/DefinitionPreviewPopup.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextBlock>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QEnterEvent>
#include <QFileInfo>
#include <QScreen>
#include <QGuiApplication>
#include <QFontDatabase>

// ============================================================
// DefinitionPreviewPopup — C03-6 定义预览弹窗
// ============================================================

DefinitionPreviewPopup::DefinitionPreviewPopup(QWidget* parent)
    : QFrame(parent)
{
    // 始终作为独立 ToolTip 顶级窗口：保证 move(globalPos) 使用全局坐标，
    // 且弹窗可超出父窗口边界显示（参考 HoverPopup 实现）。
    // 注：即便有 parent，设置 Qt::ToolTip 后 Qt 会将其视为顶级窗口，
    //     parent 仅用于内存管理（父销毁时一并释放本弹窗）。
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_Hover, true);  // 启用 enterEvent/leaveEvent 追踪
    setFocusPolicy(Qt::ClickFocus);   // 接受焦点以接收 Esc 按键
    setFrameShape(QFrame::NoFrame);

    // 主体布局：顶部标题栏 + 代码区
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部标题栏（文件名:行号）
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("definitionPreviewTitle"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() > 0 ? titleFont.pointSize() : 9);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(m_titleLabel);

    // 代码片段只读编辑器（等宽字体）
    m_codeEdit = new QTextEdit(this);
    m_codeEdit->setReadOnly(true);
    m_codeEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_codeEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_codeEdit->setFocusPolicy(Qt::NoFocus);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_codeEdit->setFont(monoFont);
    m_codeEdit->document()->setDocumentMargin(6);
    layout->addWidget(m_codeEdit);

    // 限制最大尺寸（避免超长片段撑爆屏幕）
    setMaximumWidth(700);
    setMaximumHeight(420);
    setMinimumWidth(280);
    setMinimumHeight(120);

    // 5 秒自动隐藏计时器
    m_autoHideTimer.setSingleShot(true);
    m_autoHideTimer.setInterval(5000);
    connect(&m_autoHideTimer, &QTimer::timeout, this, &DefinitionPreviewPopup::hidePopup);
}

void DefinitionPreviewPopup::showDefinition(const QString& filePath, int line, int col,
                                            const QString& codeSnippet)
{
    Q_UNUSED(col);  // 列号信息已反映在标题中，代码片段以行为单位
    if (m_titleLabel) {
        m_titleLabel->setText(buildTitle(filePath, line));
    }
    if (m_codeEdit) {
        m_codeEdit->setPlainText(codeSnippet);
    }
    adjustSizeToFit();
}

void DefinitionPreviewPopup::showAt(const QPoint& globalPos)
{
    // 取消任何正在进行的隐藏计时（用户主动重新触发了显示）
    m_autoHideTimer.stop();
    move(adjustPosition(globalPos));
    show();
    // 启动 5 秒自动隐藏
    m_autoHideTimer.start();
}

void DefinitionPreviewPopup::hidePopup()
{
    m_autoHideTimer.stop();
    hide();
}

void DefinitionPreviewPopup::keyPressEvent(QKeyEvent* event)
{
    // Esc 关闭弹窗
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void DefinitionPreviewPopup::enterEvent(QEnterEvent* event)
{
    // 鼠标进入 → 取消自动隐藏（允许用户阅读/选择代码）
    m_autoHideTimer.stop();
    QFrame::enterEvent(event);
}

void DefinitionPreviewPopup::leaveEvent(QEvent* event)
{
    // 鼠标离开 → 重启 5 秒计时
    m_autoHideTimer.start();
    QFrame::leaveEvent(event);
}

QString DefinitionPreviewPopup::buildTitle(const QString& filePath, int line) const
{
    // 文件名 + 行号（避免在标题中暴露完整路径造成视觉噪声）
    QString name = QFileInfo(filePath).fileName();
    if (name.isEmpty()) name = filePath;
    return QStringLiteral("%1 : %2").arg(name).arg(line);
}

void DefinitionPreviewPopup::adjustSizeToFit()
{
    // 基于代码片段长度估算合适的宽度/高度
    if (!m_codeEdit) return;
    QTextDocument* doc = m_codeEdit->document();
    if (!doc) return;

    // 计算最长行字符宽度（等宽字体下，字符数 × 字符宽度）
    QFontMetrics fm(m_codeEdit->font());
    int maxChars = 0;
    int lineCount = 0;
    for (QTextBlock blk = doc->firstBlock(); blk.isValid(); blk = blk.next()) {
        int len = blk.text().length();
        if (len > maxChars) maxChars = len;
        ++lineCount;
    }

    // 内容宽度：最长行 + 滚动条预留 + 边距
    int contentWidth = fm.horizontalAdvance(QLatin1Char('M')) * maxChars + 40;
    // 内容高度：行数 × 行高 + 边距
    int contentHeight = fm.lineSpacing() * qMax(lineCount, 1) + 30;

    // 加上标题栏高度
    int titleHeight = m_titleLabel ? m_titleLabel->sizeHint().height() : 24;
    int totalWidth  = qBound(minimumWidth(),  contentWidth,  maximumWidth());
    int totalHeight = qBound(minimumHeight(), contentHeight + titleHeight, maximumHeight());

    resize(totalWidth, totalHeight);
}

QPoint DefinitionPreviewPopup::adjustPosition(const QPoint& globalPos) const
{
    // 校正弹窗位置使其不超出当前屏幕可用区域
    QScreen* screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return globalPos;

    QRect avail = screen->availableGeometry();
    QPoint pos  = globalPos;
    QRect geo   = geometry();  // 注意：geometry().width() 此时为 resize 后的尺寸

    if (pos.x() + geo.width() > avail.right() - 4) {
        pos.setX(avail.right() - geo.width() - 4);
    }
    if (pos.y() + geo.height() > avail.bottom() - 4) {
        pos.setY(avail.bottom() - geo.height() - 4);
    }
    if (pos.x() < avail.left() + 4) pos.setX(avail.left() + 4);
    if (pos.y() < avail.top() + 4)  pos.setY(avail.top() + 4);
    return pos;
}
