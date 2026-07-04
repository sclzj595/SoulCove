#include "core/config/DefaultUiLibrary.h"

#include "ui/shell/FramelessWindow.h"
#include "core/config/ThemeManager.h"

#include <QPushButton>

bool DefaultUiLibrary::initialize()
{
    // 预留初始化逻辑（注册类型、加载资源等）
    // 当前实现无需额外初始化，直接返回成功
    return true;
}

void DefaultUiLibrary::applyTheme(const QString& themeKey)
{
    // 通过 ThemeManager 应用主题
    ThemeManager::instance().switchTheme(themeKey);
}

QWidget* DefaultUiLibrary::createFramelessWindow(QWidget* parent)
{
    return new FramelessWindow(parent);
}

QWidget* DefaultUiLibrary::createModernButton(const QString& text, QWidget* parent)
{
    auto* btn = new QPushButton(text, parent);
    btn->setObjectName(QStringLiteral("modernButton"));
    btn->setCursor(Qt::PointingHandCursor);

    // 按钮颜色跟随主题强调色
    const auto& p = ThemeManager::instance().currentPalette();
    QString accent = p.accentPrimary.name(QColor::HexRgb);
    QString hover = p.accentHover.name(QColor::HexRgb);
    QString pressed = p.selectionBg.name(QColor::HexRgb);

    btn->setStyleSheet(
        QStringLiteral("QPushButton#modernButton {"
                       "  background-color: %1;"
                       "  color: #ffffff;"
                       "  border: none;"
                       "  border-radius: 4px;"
                       "  padding: 6px 16px;"
                       "  font-size: 13px;"
                       "}"
                       "QPushButton#modernButton:hover {"
                       "  background-color: %2;"
                       "}"
                       "QPushButton#modernButton:pressed {"
                       "  background-color: %3;"
                       "}")
            .arg(accent, hover, pressed)
    );
    return btn;
}
