#include "ui/editor/CompletionIcons.h"
#include "core/config/ThemeManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPen>
#include <QBrush>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// CompletionIcons — 补全项分类图标（C04-10）
// 图标 16x16，代码绘制，颜色跟随 ThemeManager
// ============================================================

CompletionIcons& CompletionIcons::instance()
{
    static CompletionIcons s_instance;
    return s_instance;
}

CompletionIcons::CompletionIcons()
{
}

void CompletionIcons::refresh()
{
    m_lspKindCache.clear();
    m_localWordIcon = QIcon();
    m_snippetIcon = QIcon();
    m_cacheBuilt = false;
    m_cachedThemeKey.clear();
}

QColor CompletionIcons::themeColor(const QString& role) const
{
    const auto& p = ThemeManager::instance().currentPalette();
    // 不同主题色板取色 — 直接使用主题已有色，避免硬编码
    if (role == QStringLiteral("function"))    return p.syntax.function.isValid()    ? p.syntax.function    : QColor(180, 120, 220);
    if (role == QStringLiteral("variable"))   return p.syntax.localVar.isValid()    ? p.syntax.localVar    : QColor(86, 156, 214);
    if (role == QStringLiteral("type"))       return p.syntax.type.isValid()        ? p.syntax.type        : QColor(78, 201, 176);
    if (role == QStringLiteral("keyword"))    return p.syntax.keyword.isValid()     ? p.syntax.keyword     : QColor(215, 186, 125);
    if (role == QStringLiteral("module"))     return p.fgSecondary.isValid()        ? p.fgSecondary        : QColor(150, 150, 150);
    if (role == QStringLiteral("snippet"))    return p.syntax.number.isValid()      ? p.syntax.number      : QColor(220, 200, 80);
    if (role == QStringLiteral("constant"))   return p.syntax.constant.isValid()    ? p.syntax.constant    : QColor(220, 90, 90);
    if (role == QStringLiteral("local"))      return p.fgPrimary.isValid()          ? p.fgPrimary          : QColor(120, 160, 200);
    if (role == QStringLiteral("default"))    return p.fgSecondary.isValid()        ? p.fgSecondary        : QColor(150, 150, 150);
    return p.fgSecondary.isValid() ? p.fgSecondary : QColor(150, 150, 150);
}

QPixmap CompletionIcons::drawIcon(const QColor& color, const QString& shape) const
{
    const int sz = 16;
    QPixmap pix(sz, sz);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(color.darker(140), 1.0);
    QBrush brush(color);
    painter.setPen(pen);
    painter.setBrush(brush);

    // 边距，避免图形贴边
    const qreal m = 2.0;

    if (shape == QStringLiteral("cube")) {
        // 紫色立方体（fn 图标）：带顶面/正面的等距立方体感
        QRectF rect(m, m + 1, sz - 2 * m - 1, sz - 2 * m - 2);
        painter.drawRect(rect);
        // 顶面斜线（伪 3D）
        QPen linePen(color.lighter(130), 1.0);
        painter.setPen(linePen);
        painter.drawLine(rect.topLeft(), rect.topLeft() + QPointF(2, -2));
        painter.drawLine(rect.topRight(), rect.topRight() + QPointF(2, -2));
        painter.drawLine(rect.topLeft() + QPointF(2, -2), rect.topRight() + QPointF(2, -2));
    } else if (shape == QStringLiteral("rect")) {
        // 蓝色矩形（变量）
        QRectF rect(m, m + 2, sz - 2 * m, sz - 2 * m - 4);
        painter.drawRect(rect);
    } else if (shape == QStringLiteral("hexagon")) {
        // 绿色六边形（类型）
        QPainterPath path;
        qreal cx = sz / 2.0, cy = sz / 2.0, r = (sz / 2.0) - m;
        for (int i = 0; i < 6; ++i) {
            qreal angle = M_PI / 3.0 * i - M_PI / 2.0;
            QPointF pt(cx + r * std::cos(angle), cy + r * std::sin(angle));
            if (i == 0) path.moveTo(pt);
            else        path.lineTo(pt);
        }
        path.closeSubpath();
        painter.drawPath(path);
    } else if (shape == QStringLiteral("diamond")) {
        // 橙色菱形（关键字）
        QPainterPath path;
        qreal cx = sz / 2.0, cy = sz / 2.0, r = (sz / 2.0) - m;
        path.moveTo(cx, cy - r);
        path.lineTo(cx + r, cy);
        path.lineTo(cx, cy + r);
        path.lineTo(cx - r, cy);
        path.closeSubpath();
        painter.drawPath(path);
    } else if (shape == QStringLiteral("circle")) {
        // 灰色圆形（模块）
        qreal r = (sz / 2.0) - m;
        painter.drawEllipse(QPointF(sz / 2.0, sz / 2.0), r, r);
    } else if (shape == QStringLiteral("lightning")) {
        // 黄色闪电（snippet）
        QPainterPath path;
        path.moveTo(9.0, 1.5);
        path.lineTo(4.0, 9.0);
        path.lineTo(7.0, 9.0);
        path.lineTo(6.0, 14.5);
        path.lineTo(12.0, 6.5);
        path.lineTo(9.0, 6.5);
        path.closeSubpath();
        painter.drawPath(path);
    } else if (shape == QStringLiteral("triangle")) {
        // 红色三角（常量）
        QPainterPath path;
        qreal cx = sz / 2.0, r = (sz / 2.0) - m;
        path.moveTo(cx, m);
        path.lineTo(cx + r, sz - m);
        path.lineTo(cx - r, sz - m);
        path.closeSubpath();
        painter.drawPath(path);
    } else if (shape == QStringLiteral("word")) {
        // 本地单词图标：两条横线 + 一根小斜线，类似"abc"占位
        QPen textPen(color, 1.6);
        painter.setPen(textPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(m, 5.0),  QPointF(sz - m, 5.0));
        painter.drawLine(QPointF(m, 9.0),  QPointF(sz - m - 2.0, 9.0));
        painter.drawLine(QPointF(m, 13.0), QPointF(sz - m, 13.0));
    } else {
        // 默认：灰色圆点
        qreal r = (sz / 2.0) - m - 1.0;
        painter.drawEllipse(QPointF(sz / 2.0, sz / 2.0), r, r);
    }

    return pix;
}

void CompletionIcons::rebuildCache() const
{
    m_lspKindCache.clear();

    // 函数：Function / Method / Constructor → 紫色立方体
    QIcon fnIcon(drawIcon(themeColor(QStringLiteral("function")), QStringLiteral("cube")));
    m_lspKindCache.insert(QStringLiteral("Function"), fnIcon);
    m_lspKindCache.insert(QStringLiteral("Method"), fnIcon);
    m_lspKindCache.insert(QStringLiteral("Constructor"), fnIcon);

    // 变量：Variable / Field / Property → 蓝色矩形
    QIcon varIcon(drawIcon(themeColor(QStringLiteral("variable")), QStringLiteral("rect")));
    m_lspKindCache.insert(QStringLiteral("Variable"), varIcon);
    m_lspKindCache.insert(QStringLiteral("Field"), varIcon);
    m_lspKindCache.insert(QStringLiteral("Property"), varIcon);

    // 类型：Class / Struct / Interface / Enum / EnumMember / TypeParameter → 绿色六边形
    QIcon typeIcon(drawIcon(themeColor(QStringLiteral("type")), QStringLiteral("hexagon")));
    m_lspKindCache.insert(QStringLiteral("Class"), typeIcon);
    m_lspKindCache.insert(QStringLiteral("Struct"), typeIcon);
    m_lspKindCache.insert(QStringLiteral("Interface"), typeIcon);
    m_lspKindCache.insert(QStringLiteral("Enum"), typeIcon);
    m_lspKindCache.insert(QStringLiteral("EnumMember"), typeIcon);
    m_lspKindCache.insert(QStringLiteral("TypeParameter"), typeIcon);

    // 关键字：Keyword → 橙色菱形
    QIcon kwIcon(drawIcon(themeColor(QStringLiteral("keyword")), QStringLiteral("diamond")));
    m_lspKindCache.insert(QStringLiteral("Keyword"), kwIcon);

    // 模块：Module → 灰色圆形
    QIcon modIcon(drawIcon(themeColor(QStringLiteral("module")), QStringLiteral("circle")));
    m_lspKindCache.insert(QStringLiteral("Module"), modIcon);
    m_lspKindCache.insert(QStringLiteral("Namespace"), modIcon);

    // 片段：Snippet → 黄色闪电
    QIcon snipIcon(drawIcon(themeColor(QStringLiteral("snippet")), QStringLiteral("lightning")));
    m_lspKindCache.insert(QStringLiteral("Snippet"), snipIcon);

    // 常量：Constant → 红色三角
    QIcon constIcon(drawIcon(themeColor(QStringLiteral("constant")), QStringLiteral("triangle")));
    m_lspKindCache.insert(QStringLiteral("Constant"), constIcon);

    // 本地词典 / snippet 缓存
    m_localWordIcon = QIcon(drawIcon(themeColor(QStringLiteral("local")), QStringLiteral("word")));
    m_snippetIcon   = snipIcon;

    m_cacheBuilt = true;
    m_cachedThemeKey = ThemeManager::instance().currentTheme();
}

QIcon CompletionIcons::iconForLspKind(const QString& kind) const
{
    // 主题切换检测：缓存的主题 key 与当前不一致则重建
    if (!m_cacheBuilt || m_cachedThemeKey != ThemeManager::instance().currentTheme()) {
        rebuildCache();
    }
    auto it = m_lspKindCache.constFind(kind);
    if (it != m_lspKindCache.constEnd()) {
        return it.value();
    }
    // 其他/未知类型 → 默认灰色圆点
    return QIcon(drawIcon(themeColor(QStringLiteral("default")), QStringLiteral("default")));
}

QIcon CompletionIcons::iconForLocalWord() const
{
    if (!m_cacheBuilt || m_cachedThemeKey != ThemeManager::instance().currentTheme()) {
        rebuildCache();
    }
    return m_localWordIcon;
}

QIcon CompletionIcons::iconForSnippet() const
{
    if (!m_cacheBuilt || m_cachedThemeKey != ThemeManager::instance().currentTheme()) {
        rebuildCache();
    }
    return m_snippetIcon;
}
