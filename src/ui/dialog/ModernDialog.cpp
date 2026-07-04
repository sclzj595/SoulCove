#include "ui/dialog/ModernDialog.h"
#include "core/config/ThemeManager.h"
#include "core/i18n/I18nManager.h"  // P3-M05: 动态宽度按语言调整

#include <QListWidget>
#include <QScrollBar>
#include <QScreen>
#include <QGuiApplication>
#include <QPainter>

// ============================================================
// 构造 / 基础
// ============================================================

ModernDialog::ModernDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
}

void ModernDialog::setupUI()
{
    // 窗口属性：无边框、模态、背景透明（用于圆角裁剪）
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    // P3-M05: 对话框宽度根据当前语言动态调整（英文比中文偏宽，避免按钮文字截断）
    // 中文 420px（紧凑），英文 480px（容纳更长按钮文本如 "Don't Save"）
    int dialogWidth = 420;
    QString curLang = I18nManager::instance().currentLanguage();
    if (curLang == QStringLiteral("en_US")) {
        dialogWidth = 480;
    }
    setFixedSize(dialogWidth, 0);  // 高度由内容撑开，后续 adjustSize
    setMinimumWidth(dialogWidth - 40);

    // 主布局
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // 内容容器（带圆角 + 阴影）
    auto* container = new QWidget(this);
    container->setObjectName(QStringLiteral("modernDialogContainer"));

    auto* innerLayout = new QVBoxLayout(container);
    innerLayout->setContentsMargins(28, 24, 28, 20);
    innerLayout->setSpacing(16);

    // 第一行：图标 + 消息文本
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(16);
    topRow->setContentsMargins(0, 0, 0, 0);

    m_iconLabel = new QLabel(container);
    m_iconLabel->setFixedSize(36, 36);
    m_iconLabel->setAlignment(Qt::AlignCenter);

    m_messageLabel = new QLabel(container);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_messageLabel->setOpenExternalLinks(false);

    topRow->addWidget(m_iconLabel);
    topRow->addWidget(m_messageLabel, 1);
    innerLayout->addLayout(topRow);

    // 输入框区域（默认隐藏）
    m_inputEdit = new QLineEdit(container);
    m_inputEdit->setMinimumHeight(36);
    m_inputEdit->hide();
    innerLayout->addWidget(m_inputEdit);

    // 列表选择区域（默认隐藏）
    m_listWidget = new QListWidget(container);
    m_listWidget->setMaximumHeight(180);
    m_listWidget->setAlternatingRowColors(false);
    m_listWidget->hide();
    innerLayout->addWidget(m_listWidget);

    // 按钮栏
    m_buttonBox = new QDialogButtonBox(Qt::Horizontal, container);
    m_buttonBox->setCenterButtons(false);
    innerLayout->addWidget(m_buttonBox);

    m_layout->addWidget(container);

    applyTheme();

    // 点击按钮 → 记录角色并关闭
    connect(m_buttonBox, &QDialogButtonBox::clicked, this, [this](QAbstractButton* btn) {
        int role = m_buttonBox->buttonRole(btn);
        if (role == QDialogButtonBox::AcceptRole) {
            m_resultRole = ROLE_ACCEPT;
        } else if (role == QDialogButtonBox::DestructiveRole) {
            m_resultRole = ROLE_DESTRUCTIVE;
        } else {
            m_resultRole = ROLE_REJECT;
        }
        accept();
    });
}

void ModernDialog::applyTheme()
{
    const auto& pal = ThemeManager::instance().currentPalette();

    QString bg      = pal.bgDialog.name();
    QString fg      = pal.fgPrimary.name();
    QString accent  = pal.accentPrimary.name();
    QString hover   = pal.accentHover.name();
    QString border  = pal.borderDefault.name();
    QString inputBg = pal.bgInput.name();

    // 容器样式
    QString containerStyle = QStringLiteral(
        "#modernDialogContainer {"
        "  background-color: %1;"
        "  color: %2;"
        "  border-radius: 12px;"
        "  border: 1px solid %3;"
        "}"
    ).arg(bg, fg, border);

    if (auto* container = findChild<QWidget*>(QStringLiteral("modernDialogContainer")))
        container->setStyleSheet(containerStyle);

    // 消息标签样式
    m_messageLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 14px; line-height: 1.5; background: transparent; }"
    ).arg(fg));

    // 输入框样式
    m_inputEdit->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
        "  padding: 8px 12px; font-size: 13px; selection-background-color: %4;"
        "}"
        "QLineEdit:focus { border-color: %5; }"
    ).arg(inputBg, fg, border, accent, accent));

    // 列表样式
    m_listWidget->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
        "  padding: 4px; font-size: 13px; outline: none;"
        "}"
        "QListWidget::item { padding: 6px 10px; border-radius: 4px; }"
        "QListWidget::item:selected { background-color: %4; color: #ffffff; }"
        "QListWidget::item:hover:!selected { background-color: %5; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: %3; border-radius: 3px; min-height: 20px; }"
    ).arg(inputBg, fg, border, accent, inputBg));
}

// ============================================================
// 配置方法链
// ============================================================

ModernDialog& ModernDialog::setTitle(const QString& title)
{
    setWindowTitle(title);
    return *this;
}

ModernDialog& ModernDialog::setMessage(const QString& message)
{
    m_messageLabel->setText(message);
    return *this;
}

ModernDialog& ModernDialog::setIcon(IconType type)
{
    m_iconType = type;
    m_iconLabel->setPixmap(createIconPixmap(type));
    m_iconLabel->setVisible(type != IconType::None);
    return *this;
}

ModernDialog& ModernDialog::addButton(const QString& text, ButtonRole role)
{
    QPushButton* btn = createButton(text, role);

    switch (role) {
    case ButtonRole::Accept:
        m_buttonBox->addButton(btn, QDialogButtonBox::AcceptRole);
        break;
    case ButtonRole::Destructive:
        m_buttonBox->addButton(btn, QDialogButtonBox::DestructiveRole);
        break;
    default:
        m_buttonBox->addButton(btn, QDialogButtonBox::RejectRole);
        break;
    }

    return *this;
}

// ============================================================
// 图标工厂：返回 QPixmap（直接设置给 QLabel）
// ============================================================

QPixmap ModernDialog::createIconPixmap(IconType type) const
{
    const auto& pal = ThemeManager::instance().currentPalette();
    QString accentColor = pal.accentPrimary.name();

    QString symbol;
    QString color;

    switch (type) {
    case IconType::Info:
        symbol     = QChar(0x2139);   // ℹ
        color      = QStringLiteral("#3498db");
        break;
    case IconType::Warning:
        symbol     = QChar(0x26A0);   // ⚠
        color      = QStringLiteral("#f39c12");
        break;
    case IconType::Question:
        symbol     = QChar(0x2753);   // ❓
        color      = accentColor;
        break;
    case IconType::Error:
        symbol     = QChar(0x2717);   // ✗
        color      = QStringLiteral("#e74c3c");
        break;
    default:
        return QPixmap();  // IconType::None → 空图标
    }

    QPixmap pix(36, 36);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    // 绘制圆形底色
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(color));
    p.drawEllipse(QRectF(2, 2, 32, 32));
    // 绘制符号
    p.setPen(QColor(QStringLiteral("#ffffff")));
    QFont f = p.font();
    f.setPixelSize(20);
    p.setFont(f);
    p.drawText(pix.rect(), Qt::AlignCenter, symbol);
    p.end();

    return pix;
}

QPushButton* ModernDialog::createButton(const QString& text, ButtonRole role) const
{
    const auto& pal = ThemeManager::instance().currentPalette();
    QString accent  = pal.accentPrimary.name();
    QString hover   = pal.accentHover.name();
    QString bgInput = pal.bgInput.name();
    QString fg      = pal.fgPrimary.name();
    QString border  = pal.borderDefault.name();

    QString style;

    switch (role) {
    case ButtonRole::Accept:
        style = QStringLiteral(
            "QPushButton {"
            "  background-color: %1; color: #ffffff; border: none; border-radius: 8px;"
            "  padding: 9px 24px; font-size: 13px; font-weight: 500;"
            "}"
            "QPushButton:hover { background-color: %2; }"
            "QPushButton:pressed { background-color: %1; }"
        ).arg(accent, hover);
        break;
    case ButtonRole::Destructive:
        style = QStringLiteral(
            "QPushButton {"
            "  background-color: transparent; color: #e74c3c; border: 1px solid #e74c3c; border-radius: 8px;"
            "  padding: 9px 24px; font-size: 13px;"
            "}"
            "QPushButton:hover { background-color: #fdeaea; }"
            "QPushButton:pressed { background-color: #fadbd8; }"
        );
        break;
    default:  // Reject
        style = QStringLiteral(
            "QPushButton {"
            "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px;"
            "  padding: 9px 24px; font-size: 13px;"
            "}"
            "QPushButton:hover { border-color: %4; background-color: %5; }"
            "QPushButton:pressed { background-color: %1; }"
        ).arg(bgInput, fg, border, accent, bgInput);
        break;
    }

    auto* btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    // P3-M05: 按钮最小宽度从 70 提升到 90，防止 "Don't Save" / "不保存" 切换时宽度跳变
    btn->setMinimumWidth(90);
    btn->setStyleSheet(style);
    btn->setAttribute(Qt::WA_Hover, true);
    return btn;
}

// ============================================================
// 动画
// ============================================================

void ModernDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    animateOpen();
}

void ModernDialog::animateOpen()
{
    // 居中显示
    if (parentWidget()) {
        QRect pr = parentWidget()->geometry();
        move(pr.x() + (pr.width() - width()) / 2,
             pr.y() + (pr.height() - height()) / 2);
    }

    // 调整高度到内容大小
    adjustSize();

    // 淡入动画
    QPropertyAnimation* anim = new QPropertyAnimation(this, "windowOpacity");
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(150);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, anim, &QObject::deleteLater);
    anim->start();
}

// ============================================================
// 静态工厂方法
// ============================================================

int ModernDialog::information(QWidget* parent, const QString& title, const QString& text)
{
    ModernDialog d(parent);
    d.setTitle(title).setMessage(text).setIcon(IconType::Info)
     .addButton(tr("OK"), ButtonRole::Accept);
    d.exec();
    return d.m_resultRole;
}

int ModernDialog::warning(QWidget* parent, const QString& title, const QString& text)
{
    ModernDialog d(parent);
    d.setTitle(title).setMessage(text).setIcon(IconType::Warning)
     .addButton(tr("OK"), ButtonRole::Accept);
    d.exec();
    return d.m_resultRole;
}

int ModernDialog::critical(QWidget* parent, const QString& title, const QString& text)
{
    ModernDialog d(parent);
    d.setTitle(title).setMessage(text).setIcon(IconType::Error)
     .addButton(tr("OK"), ButtonRole::Accept);
    d.exec();
    return d.m_resultRole;
}

int ModernDialog::question(QWidget* parent, const QString& title, const QString& text)
{
    ModernDialog d(parent);
    d.setTitle(title).setMessage(text).setIcon(IconType::Question)
     .addButton(tr("是"), ButtonRole::Accept)
     .addButton(tr("否"), ButtonRole::Reject);
    d.exec();
    return d.m_resultRole;
}

int ModernDialog::confirm(QWidget* parent, const QString& title, const QString& text)
{
    ModernDialog d(parent);
    d.setTitle(title).setMessage(text).setIcon(IconType::Warning)
     .addButton(tr("保存"), ButtonRole::Accept)
     .addButton(tr("不保存"), ButtonRole::Destructive)
     .addButton(tr("取消"), ButtonRole::Reject);
    d.exec();
    return d.m_resultRole;
}

QString ModernDialog::getText(QWidget* parent, const QString& title,
                               const QString& label, const QString& text,
                               bool* ok)
{
    ModernDialog d(parent);
    d.setTitle(title).setMessage(label).setIcon(IconType::None);

    // 显示输入框
    d.m_inputEdit->setText(text);
    d.m_inputEdit->show();
    d.m_inputEdit->setFocus();
    d.addButton(tr("确定"), ButtonRole::Accept);
    d.addButton(tr("取消"), ButtonRole::Reject);

    d.exec();

    if (ok) *ok = (d.m_resultRole == ROLE_ACCEPT);
    return (d.m_resultRole == ROLE_ACCEPT) ? d.m_inputEdit->text() : QString();
}

QString ModernDialog::getItem(QWidget* parent, const QString& title,
                               const QString& label, const QStringList& items,
                               int current, bool* ok)
{
    ModernDialog d(parent);
    d.setTitle(title).setMessage(label).setIcon(IconType::None);

    // 显示列表
    d.m_listWidget->addItems(items);
    if (current >= 0 && current < items.size()) {
        d.m_listWidget->setCurrentRow(current);
    }
    d.m_listWidget->show();
    d.m_listWidget->setFocus();
    d.addButton(tr("确定"), ButtonRole::Accept);
    d.addButton(tr("取消"), ButtonRole::Reject);

    // 双击直接确认
    QObject::connect(d.m_listWidget, &QListWidget::itemDoubleClicked, &d, [&d]() {
        d.m_resultRole = ROLE_ACCEPT;
        d.accept();
    });

    d.exec();

    if (ok) *ok = (d.m_resultRole == ROLE_ACCEPT);
    if (d.m_resultRole == ROLE_ACCEPT && d.m_listWidget->currentItem()) {
        return d.m_listWidget->currentItem()->text();
    }
    return QString();
}
