#include "core/config/ThemeManager.h"
#include "Logger.hpp"
#include <QApplication>
#include <QWidget>
#include <QStyle>
#include <QDebug>
#include <QMutexLocker>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

ThemeManager::ThemeManager()
{
    // 注册预设主题
    registerTheme(QStringLiteral("purple"), createPurpleDark());
    registerTheme(QStringLiteral("blue"),   createBlueDark());
    registerTheme(QStringLiteral("black"),  createBlackDark());
    registerTheme(QStringLiteral("light"),  createLightClassic());
    registerTheme(QStringLiteral("pink"),  createPinkLight());    // 白粉主题

    // 默认紫色主题
    m_currentKey = QStringLiteral("purple");
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager s_instance;
    return s_instance;
}

void ThemeManager::registerTheme(const QString& key, const ThemePalette& palette)
{
    QMutexLocker locker(&m_mutex);
    m_themes[key] = palette;
    LOG_DEBUG_S("ThemeManager", "registerTheme", "注册主题:" << key << palette.displayName);
}

void ThemeManager::switchTheme(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    if (!m_themes.contains(key)) {
        LOG_WARN_S("ThemeManager", "switchTheme", "主题不存在:" << key);
        return;
    }

    bool changed = (m_currentKey != key);
    m_currentKey = key;
    const auto& palette = m_themes[key];
    locker.unlock();

    // 始终应用QSS到全局（修复：即使key相同也重新加载，确保样式生效）
    QString fullQss = palette.generateQSS();

    // 校验：确保QSS无残留 %N 占位符（避免 "Could not parse stylesheet" 警告）
    if (fullQss.contains(QRegularExpression(QStringLiteral("%[0-9]")))) {
        LOG_WARN_S("ThemeManager", "switchTheme", "QSS模板存在未替换的 %N 占位符，样式可能解析失败");
    }

    qApp->setStyleSheet(fullQss);

    // 强制所有控件重绘（解决切换深色/浅色后界面不刷新的问题）
    for (QWidget* w : qApp->allWidgets()) {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }

#ifdef Q_OS_WIN
    // 修复：切换主题时同步 DWM 暗色模式到所有顶层窗口，
    // 避免 DWM 标题栏/边框残留旧主题色（如切到亮色主题时标题栏仍为暗色）
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20（Win10 1809+ / Win11）
    bool isDark = palette.bgEditor.lightness() <= 128;
    BOOL darkMode = isDark ? TRUE : FALSE;
    for (QWidget* w : qApp->topLevelWidgets()) {
        if (HWND hwnd = reinterpret_cast<HWND>(w->winId())) {
            DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
        }
    }
#endif

    // 始终发射信号（MD预览等组件需要每次都刷新样式）
    emit themeChanged(key);

    LOG_DEBUG_S("ThemeManager", "switchTheme", "应用主题:" << palette.displayName);
}

QString ThemeManager::themeDisplayName(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    auto it = m_themes.find(key);
    return it != m_themes.end() ? it.value().displayName : key;
}

// ========== 预设主题工厂 ==========

ThemePalette ThemeManager::createPurpleDark()
{
    ThemePalette p;
    p.name = QStringLiteral("purple");
    p.displayName = QStringLiteral("暗黑紫");

    // 主色调
    p.accentPrimary  = QColor("#9B59B6");
    p.accentHover    = QColor("#B370CF");
    p.accentPressed  = QColor("#7D3C98");

    // 背景色
    p.bgWindow       = QColor("#1e1e1e");
    p.bgTitleBar     = QColor("#323233");
    p.bgSideBar      = QColor("#252526");
    p.bgEditor       = QColor("#1e1e1e");
    p.bgStatusBar    = QColor("#7B2D8E");
    p.bgTabBar       = QColor("#252526");
    p.bgTabActive    = QColor("#1e1e1e");
    p.bgTabInactive  = QColor("#2d2d2d");
    p.bgInput        = QColor("#3c3c3c");
    p.bgActivityBar  = QColor("#333333");
    p.bgHover        = QColor(255, 255, 255, 20);
    p.bgPressed      = QColor(255, 255, 255, 35);
    p.bgMenu         = QColor("#2d2d2d");
    p.bgTooltip      = QColor("#3c3c3c");
    p.bgDialog       = QColor("#2d2d2d");

    // 前景色
    p.fgPrimary      = QColor("#d4d4d4");
    p.fgSecondary    = QColor("#999999");
    p.fgDisabled     = QColor("#5a5a5a");
    p.fgLineNumber   = QColor("#858585");
    p.fgOnAccent     = QColor("#ffffff");
    p.fgOnHover      = QColor("#ffffff");

    // 边框色
    p.borderDefault  = QColor("#3c3c3c");
    p.borderHover    = QColor("#9B59B6");
    p.borderFocus    = QColor("#9B59B6");

    // 特殊色
    p.selectionBg    = QColor(155, 89, 182, 70);
    p.currentLineBg  = QColor("#2a2d2e");
    p.closeBtnHover  = QColor("#e74c3c");
    p.errorColor     = QColor("#e74c3c");
    p.warningColor   = QColor("#f39c12");

    // 滚动条
    p.scrollbarBg         = QColor("#1e1e1e");
    p.scrollbarHandle     = QColor("#424242");
    p.scrollbarHandleHover= QColor("#555555");

    p.initSyntaxDark();
    return p;
}

ThemePalette ThemeManager::createBlueDark()
{
    ThemePalette p;
    p.name = QStringLiteral("blue");
    p.displayName = QStringLiteral("经典蓝");

    p.accentPrimary  = QColor("#0078d4");
    p.accentHover    = QColor("#1a8cff");
    p.accentPressed  = QColor("#005a9e");

    p.bgWindow       = QColor("#1e1e1e");
    p.bgTitleBar     = QColor("#323233");
    p.bgSideBar      = QColor("#252526");
    p.bgEditor       = QColor("#1e1e1e");
    p.bgStatusBar    = QColor("#007acc");
    p.bgTabBar       = QColor("#252526");
    p.bgTabActive    = QColor("#1e1e1e");
    p.bgTabInactive  = QColor("#2d2d2d");
    p.bgInput        = QColor("#3c3c3c");
    p.bgActivityBar  = QColor("#333333");
    p.bgHover        = QColor(255, 255, 255, 20);
    p.bgPressed      = QColor(255, 255, 255, 35);
    p.bgMenu         = QColor("#2d2d2d");
    p.bgTooltip      = QColor("#3c3c3c");
    p.bgDialog       = QColor("#2d2d2d");

    p.fgPrimary      = QColor("#d4d4d4");
    p.fgSecondary    = QColor("#999999");
    p.fgDisabled     = QColor("#5a5a5a");
    p.fgLineNumber   = QColor("#858585");
    p.fgOnAccent     = QColor("#ffffff");
    p.fgOnHover      = QColor("#ffffff");

    p.borderDefault  = QColor("#3c3c3c");
    p.borderHover    = QColor("#0078d4");
    p.borderFocus    = QColor("#0078d4");

    p.selectionBg    = QColor("#264f78");
    p.currentLineBg  = QColor("#2a2d2e");
    p.closeBtnHover  = QColor("#e74c3c");
    p.errorColor     = QColor("#e74c3c");
    p.warningColor   = QColor("#f39c12");

    p.scrollbarBg         = QColor("#1e1e1e");
    p.scrollbarHandle     = QColor("#424242");
    p.scrollbarHandleHover= QColor("#555555");

    p.initSyntaxDark();
    return p;
}

ThemePalette ThemeManager::createBlackDark()
{
    ThemePalette p;
    p.name = QStringLiteral("black");
    p.displayName = QStringLiteral("极致黑");

    p.accentPrimary  = QColor("#666666");
    p.accentHover    = QColor("#888888");
    p.accentPressed  = QColor("#444444");

    p.bgWindow       = QColor("#0d0d0d");
    p.bgTitleBar     = QColor("#1a1a1a");
    p.bgSideBar      = QColor("#141414");
    p.bgEditor       = QColor("#0d0d0d");
    p.bgStatusBar    = QColor("#1a1a1a");
    p.bgTabBar       = QColor("#141414");
    p.bgTabActive    = QColor("#0d0d0d");
    p.bgTabInactive  = QColor("#1a1a1a");
    p.bgInput        = QColor("#1a1a1a");
    p.bgActivityBar  = QColor("#181818");
    p.bgHover        = QColor(255, 255, 255, 15);
    p.bgPressed      = QColor(255, 255, 255, 25);
    p.bgMenu         = QColor("#1a1a1a");
    p.bgTooltip      = QColor("#1a1a1a");
    p.bgDialog       = QColor("#1a1a1a");

    p.fgPrimary      = QColor("#cccccc");
    p.fgSecondary    = QColor("#888888");
    p.fgDisabled     = QColor("#555555");
    p.fgLineNumber   = QColor("#666666");
    p.fgOnAccent     = QColor("#ffffff");
    p.fgOnHover      = QColor("#ffffff");

    p.borderDefault  = QColor("#2a2a2a");
    p.borderHover    = QColor("#555555");
    p.borderFocus    = QColor("#555555");

    p.selectionBg    = QColor("#444444");
    p.currentLineBg  = QColor("#1a1a1a");
    p.closeBtnHover  = QColor("#e74c3c");
    p.errorColor     = QColor("#e74c3c");
    p.warningColor   = QColor("#f39c12");

    p.scrollbarBg         = QColor("#0d0d0d");
    p.scrollbarHandle     = QColor("#333333");
    p.scrollbarHandleHover= QColor("#444444");

    p.initSyntaxDark();
    return p;
}

ThemePalette ThemeManager::createLightClassic()
{
    ThemePalette p;
    p.name = QStringLiteral("light");
    p.displayName = QStringLiteral("亮色经典");

    p.accentPrimary  = QColor("#0078d4");
    p.accentHover    = QColor("#1a8cff");
    p.accentPressed  = QColor("#005a9e");

    p.bgWindow       = QColor("#f3f3f3");
    p.bgTitleBar     = QColor("#dddddd");
    p.bgSideBar      = QColor("#e8e8e8");
    p.bgEditor       = QColor("#ffffff");
    p.bgStatusBar    = QColor("#007acc");
    p.bgTabBar       = QColor("#e8e8e8");
    p.bgTabActive    = QColor("#ffffff");
    p.bgTabInactive  = QColor("#e0e0e0");
    p.bgInput        = QColor("#ffffff");
    p.bgActivityBar  = QColor("#d5d5d5");
    p.bgHover        = QColor(0, 0, 0, 12);
    p.bgPressed      = QColor(0, 0, 0, 20);
    p.bgMenu         = QColor("#f0f0f0");
    p.bgTooltip      = QColor("#f5f5f5");
    p.bgDialog       = QColor("#f0f0f0");

    p.fgPrimary      = QColor("#333333");
    p.fgSecondary    = QColor("#666666");
    p.fgDisabled     = QColor("#aaaaaa");
    p.fgLineNumber   = QColor("#999999");
    p.fgOnAccent     = QColor("#ffffff");
    p.fgOnHover      = QColor("#111111");

    p.borderDefault  = QColor("#cccccc");
    p.borderHover    = QColor("#0078d4");
    p.borderFocus    = QColor("#0078d4");

    p.selectionBg    = QColor("#add6ff");
    p.currentLineBg  = QColor("#f0f4ff");
    p.closeBtnHover  = QColor("#e81123");
    p.errorColor     = QColor("#d32f2f");
    p.warningColor   = QColor("#f57c00");

    p.scrollbarBg         = QColor("#f3f3f3");
    p.scrollbarHandle     = QColor("#c1c1c1");
    p.scrollbarHandleHover= QColor("#a0a0a0");

    p.initSyntaxLight();
    return p;
}

ThemePalette ThemeManager::createPinkLight()
{
    ThemePalette p;
    p.name = QStringLiteral("pink");
    p.displayName = QStringLiteral("白粉");

    // 更粉的强调色（提高饱和度）
    p.accentPrimary  = QColor("#e84a8b");   // 热粉红
    p.accentHover    = QColor("#f26ba3");
    p.accentPressed  = QColor("#c73573");

    // 浅色背景系（偏粉白）
    p.bgWindow       = QColor("#fdf5f8");   // 极浅粉底
    p.bgTitleBar     = QColor("#fceef3");   // 标题栏带粉
    p.bgSideBar      = QColor("#fae9f0");   // 侧边栏更粉
    p.bgEditor       = QColor("#fffafc");   // 编辑器微粉白
    p.bgStatusBar    = QColor("#e84a8b");   // 状态栏跟随强调色
    p.bgTabBar       = QColor("#fceef3");   // 标签栏
    p.bgTabActive    = QColor("#ffffff");   // 活跃标签纯白
    p.bgTabInactive  = QColor("#f5dce6");   // 非活跃标签更粉
    p.bgInput        = QColor("#fffafc");   // 输入框
    p.bgActivityBar  = QColor("#f8d4e3");   // 活动图标栏更粉
    p.bgHover        = QColor(232, 74, 139, 22);     // 粉色半透明悬浮
    p.bgPressed      = QColor(232, 74, 139, 38);
    p.bgMenu         = QColor("#fef0f5");   // 菜单
    p.bgTooltip      = QColor("#fffbfc");   // 提示框
    p.bgDialog       = QColor("#fef0f5");   // 对话框

    // 前景色（深色文字）
    p.fgPrimary      = QColor("#2d1f24");
    p.fgSecondary    = QColor("#5a404d");
    p.fgDisabled     = QColor("#a88e98");
    p.fgLineNumber   = QColor("#b89aa6");
    p.fgOnAccent     = QColor("#ffffff");
    p.fgOnHover      = QColor("#1a1015");

    // 边框（粉色调）
    p.borderDefault  = QColor("#ebc8d6");
    p.borderHover    = QColor("#e84a8b");
    p.borderFocus    = QColor("#e84a8b");

    // 特殊色
    p.selectionBg    = QColor("#fce4ec");   // 粉色选中背景
    p.currentLineBg  = QColor("#fff0f5");   // 当前行极淡粉
    p.closeBtnHover  = QColor("#e81123");
    p.errorColor     = QColor("#c0392b");
    p.warningColor   = QColor("#d68910");

    // 滚动条（暖粉色调）
    p.scrollbarBg         = QColor("#fae9f0");
    p.scrollbarHandle     = QColor("#e0b8cc");
    p.scrollbarHandleHover= QColor("#d49ab5");

    p.initSyntaxLight();
    return p;
}

// ========== P1-2: 语法高亮配色初始化 ==========

void ThemePalette::initSyntaxDark()
{
    // VSCode Dark+ 配色 — 暗色主题标准配色
    syntax.keyword      = QColor("#569CD6"); // 蓝
    syntax.control      = QColor("#C586C0"); // 粉紫（控制流）
    syntax.type         = QColor("#4EC9B0"); // 青
    syntax.string       = QColor("#CE9178"); // 橙
    syntax.number       = QColor("#B5CEA8"); // 浅绿
    syntax.comment      = QColor("#6A9955"); // 绿
    syntax.function     = QColor("#DCDCAA"); // 黄
    syntax.funcDecl     = QColor("#DCDCAA"); // 黄（函数声明，斜体）
    syntax.preprocessor = QColor("#C586C0"); // 粉紫
    syntax.builtin      = QColor("#DCDCAA"); // 黄
    syntax.decorator    = QColor("#DCDCAA"); // 黄
    syntax.constant     = QColor("#4FC1FF"); // 浅蓝
    syntax.tag          = QColor("#569CD6"); // 蓝
    syntax.typeDef      = QColor("#4EC9B0"); // 青（LSP 类型定义）
    syntax.memberVar    = QColor("#9CDCFE"); // 浅蓝（LSP 成员变量）
    syntax.localVar     = QColor("#9CDCFE"); // 浅蓝（LSP 局部变量）
    syntax.yamlKey      = QColor("#4EC9B0"); // 青
    syntax.tomlKey      = QColor("#9CDCFE"); // 浅蓝
    syntax.tomlSection  = QColor("#C586C0"); // 粉紫
    syntax.doxy         = QColor("#FFB86C"); // P3: 橙黄（Doxygen 标签，加粗）
    syntax.todo         = QColor("#FF6B6B"); // P4: 暖红（TODO/FIXME，斜体）
    syntax.headerPath   = QColor("#4FC1FF"); // Bug1: 浅蓝（头文件路径，链接风格，可点击跳转）
}

void ThemePalette::initSyntaxLight()
{
    // VSCode Light+ 配色 — 与 Dark+ 对称，保证主题切换后视觉一致
    syntax.keyword      = QColor("#0000FF"); // 蓝
    syntax.control      = QColor("#AF00DB"); // 紫（控制流，区别于关键字）
    syntax.type         = QColor("#267F99"); // 青
    syntax.string       = QColor("#A31515"); // 红
    syntax.number       = QColor("#098658"); // 绿
    syntax.comment      = QColor("#008000"); // 绿
    syntax.function     = QColor("#795E26"); // 棕
    syntax.funcDecl     = QColor("#795E26"); // 棕（函数声明，斜体）
    syntax.preprocessor = QColor("#AF00DB"); // 紫
    syntax.builtin      = QColor("#795E26"); // 棕
    syntax.decorator    = QColor("#795E26"); // 棕
    syntax.constant     = QColor("#0070C1"); // 蓝
    syntax.tag          = QColor("#800000"); // 褐红
    syntax.typeDef      = QColor("#267F99"); // 青（LSP 类型定义）
    syntax.memberVar    = QColor("#001080"); // 深蓝（LSP 成员变量）
    syntax.localVar     = QColor("#001080"); // 深蓝（LSP 局部变量）
    syntax.yamlKey      = QColor("#267F99"); // 青
    syntax.tomlKey      = QColor("#001080"); // 深蓝
    syntax.tomlSection  = QColor("#AF00DB"); // 紫
    syntax.doxy         = QColor("#B5651D"); // P3: 橙褐（Doxygen 标签，加粗）
    syntax.todo         = QColor("#D1242F"); // P4: 暖红（TODO/FIXME，斜体）
    syntax.headerPath   = QColor("#0550AE"); // Bug1: 深蓝（头文件路径，链接风格，可点击跳转）
}

// ========== QSS 生成（全变量化，零硬编码）==========

QString ThemePalette::generateQSS() const
{
    auto c = [](const QColor& color) -> QString {
        if (color.alpha() == 255) return color.name(QColor::HexRgb);
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };

    // 构建变量表（用 {{key}} 格式避免与 Qt 的 %N 冲突）
    QMap<QString, QString> v;
    v["fgPrimary"]           = c(fgPrimary);
    v["bgWindow"]            = c(bgWindow);
    v["bgTitleBar"]          = c(bgTitleBar);
    v["borderDefault"]       = c(borderDefault);
    v["fgSecondary"]         = c(fgSecondary);
    v["closeBtnHover"]       = c(closeBtnHover);
    v["fgOnHover"]           = c(fgOnHover);
    v["fgDisabled"]          = c(fgDisabled);
    v["accentPrimary"]       = c(accentPrimary);
    v["bgTabBar"]            = c(bgTabBar);
    v["bgTabInactive"]       = c(bgTabInactive);
    v["bgInput"]             = c(bgInput);
    v["bgEditor"]            = c(bgEditor);
    v["bgSideBar"]           = c(bgSideBar);
    v["selectionBg"]         = c(selectionBg);
    v["bgStatusBar"]         = c(bgStatusBar);
    v["fgOnAccent"]          = c(fgOnAccent);
    v["bgActivityBar"]       = c(bgActivityBar);
    v["bgHover"]             = c(bgHover);
    v["bgPressed"]           = c(bgPressed);
    v["bgMenu"]              = c(bgMenu);
    v["bgTooltip"]           = c(bgTooltip);
    v["bgDialog"]            = c(bgDialog);
    v["borderHover"]         = c(borderHover);
    v["borderFocus"]         = c(borderFocus);
    v["currentLineBg"]       = c(currentLineBg);
    v["errorColor"]          = c(errorColor);
    v["scrollbarBg"]         = c(scrollbarBg);
    v["scrollbarHandle"]     = c(scrollbarHandle);
    v["scrollbarHandleHover"]= c(scrollbarHandleHover);
    v["accentHover"]         = c(accentHover);
    v["fgLineNumber"]        = c(fgLineNumber);
    v["warningColor"]        = c(warningColor);

    // ===== MD预览专用变量（动态生成，适配亮/暗主题）=====
    bool isLightTheme = bgEditor.lightness() > 128;
    // 代码块背景：暗色主题用accent的15%透明度，亮色主题用浅灰
    v["mdCodeBg"]     = isLightTheme ? QStringLiteral("rgba(0,0,0,0.06)")
                                     : QStringLiteral("rgba(%1,%2,%3,0.15)")
                                           .arg(accentPrimary.red()).arg(accentPrimary.green()).arg(accentPrimary.blue());
    // 代码文字色：暗色主题用粉红，亮色主题用深红
    v["mdCodeFg"]     = isLightTheme ? QStringLiteral("#c64848") : QStringLiteral("#e06c75");
    // 预格式化块背景：暗色主题用深灰，亮色主题用浅灰
    v["mdPreBg"]      = isLightTheme ? QStringLiteral("#f5f5f5") : QStringLiteral("#282c34");
    // strong 文字色：暗色主题用白色，亮色主题用主文字色
    v["mdStrong"]     = isLightTheme ? c(fgPrimary) : QStringLiteral("#ffffff");
    // 引用块渐变背景
    v["mdQuoteBg"]    = isLightTheme ? QStringLiteral("rgba(0,0,0,0.03)")
                                     : QStringLiteral("rgba(%1,%2,%3,0.08)")
                                           .arg(accentPrimary.red()).arg(accentPrimary.green()).arg(accentPrimary.blue());
    // 表格悬浮色
    v["mdRowHover"]   = isLightTheme ? QStringLiteral("rgba(0,0,0,0.04)")
                                     : QStringLiteral("rgba(%1,%2,%3,0.08)")
                                           .arg(accentPrimary.red()).arg(accentPrimary.green()).arg(accentPrimary.blue());
    // 表格偶数行
    v["mdRowEven"]    = isLightTheme ? QStringLiteral("rgba(0,0,0,0.02)")
                                     : QStringLiteral("rgba(255,255,255,0.02)");

    // 变量替换辅助：{{key}} → value
    auto repl = [&v](QString tpl) -> QString {
        for (auto it = v.constBegin(); it != v.constEnd(); ++it)
            tpl.replace(QStringLiteral("{{") + it.key() + QStringLiteral("}}"), it.value());
        return tpl;
    };

    // QSS 模板使用 {{key}} 占位符
    QString qss = QStringLiteral(R"(
/* ============================================================
 * SoulCove 全局样式表（由 ThemeManager 动态生成）
 * ============================================================ */

/* ========== 全局基础 ========== */
* {
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 13px;
}

QWidget {
    color: {{fgPrimary}};
    background-color: transparent;
}

#mainWindow {
    background-color: {{bgWindow}};
}

/* ============================================================
 * 标题栏
 * ============================================================ */
#titleBar {
    background-color: {{bgTitleBar}};
    border-bottom: 1px solid {{borderDefault}};
}

#titleBar > QLabel#titleLabel {
    color: {{fgSecondary}};
    font-size: 12px;
    background: transparent;
    border: none;
    padding: 0 4px;
}

#titleBar #titleSeparator {
    color: {{fgDisabled}};
    font-size: 13px;
    background: transparent;
    border: none;
    padding: 0 2px;
}

#titleBar QPushButton {
    color: {{fgSecondary}};
    border: none;
    background: transparent;
    padding: 4px 6px;
    border-radius: 3px;
}

#titleBar QPushButton:hover {
    background-color: {{bgHover}};
    color: {{fgOnHover}};
}

#titleBar QPushButton:pressed {
    background-color: {{bgPressed}};
}

/* VSCode 风格文本菜单按钮 */
#titleBar QPushButton#menuBarItem {
    color: {{fgSecondary}};
    font-size: 12px;
    padding: 4px 8px;
    border: none;
    background: transparent;
    border-radius: 3px;
}
#titleBar QPushButton#menuBarItem:hover {
    background-color: {{bgHover}};
    color: {{fgOnHover}};
}

#titleBar #btnClose:hover {
    background-color: {{closeBtnHover}};
    color: #ffffff;
}
#titleBar #btnClose:pressed {
    background-color: {{errorColor}};
    color: #ffffff;
}

#titleBar #btnSettings {
    color: {{fgDisabled}};
    font-size: 15px;
    padding: 4px 6px;
}
#titleBar #btnSettings:hover {
    color: {{fgOnHover}};
    background-color: {{bgHover}};
}

#titleBar #btnMinimize, #titleBar #btnMaximize {
    color: {{fgSecondary}};
    font-size: 13px;
}

/* ============================================================
 * 标签页栏
 * ============================================================ */
#editorTabBar {
    background-color: {{bgTabInactive}};
    border-bottom: 1px solid {{borderDefault}};
}

QTabBar#editorTabBar {
    background: transparent;
    font-size: 13px;
}

QTabBar#editorTabBar::scroller {
    width: 0px;
}

QTabBar#editorTabBar::tab {
    color: {{fgSecondary}};
    background-color: {{bgTabBar}};
    border: none;
    border-top-left-radius: 5px;
    border-top-right-radius: 5px;
    padding: 7px 14px;
    margin-right: 1px;
    margin-top: 0;
    min-width: 100px;
    max-width: 200px;
}

QTabBar#editorTabBar::tab:hover {
    background-color: {{bgTitleBar}};
    color: {{fgPrimary}};
}

QTabBar#editorTabBar::tab:selected {
    background-color: {{bgEditor}};
    color: {{fgOnHover}};
    border-top: 2px solid {{accentPrimary}};
}

QTabBar#editorTabBar::close-button {
    image: url(:/close);
    subcontrol-position: right center;
    subcontrol-origin: padding;
    width: 16px; height: 16px;
    margin-right: 4px;
    border-radius: 8px;
    background: transparent;
}

QTabBar#editorTabBar::close-button:hover {
    background-color: {{bgHover}};
}

QTabBar#editorTabBar::tab:!selected:!hover::close-button {
    image: none;
    width: 0px; height: 0px;
}

#editorStack {
    background-color: {{bgEditor}};
    border: none;
}

/* ============================================================
 * 左侧资源栏
 * ============================================================ */
#sideBar {
    background-color: {{bgSideBar}};
    border-right: 1px solid {{borderDefault}};
}

#activityBar {
    background-color: {{bgActivityBar}};
}

#activityBar QPushButton {
    color: {{fgDisabled}};
    background: transparent;
    border: none;
    border-left: 3px solid transparent;
    border-radius: 0;
    padding: 10px 0;
    font-size: 20px;
    min-width: 48px;
    min-height: 44px;
}

#activityBar QPushButton:hover {
    color: {{fgPrimary}};
    background-color: {{bgHover}};
}

#activityBar QPushButton[active="true"] {
    color: {{fgOnHover}};
    border-left: 3px solid {{accentPrimary}};
}

#sidePanel {
    background-color: {{bgSideBar}};
}

#panelTitle {
    color: {{fgPrimary}};
    font-size: 11px;
    font-weight: bold;
    text-transform: uppercase;
    padding: 8px 4px 4px 4px;
    letter-spacing: 1px;
    background: transparent;
    border: none;
}

#pathLabel {
    color: {{fgDisabled}};
    font-size: 10px;
    font-family: "Consolas", "Courier New", monospace;
    background: transparent;
    border: none;
    padding: 0 4px;
}

#sideFileList, #sideFileTree {
    background-color: transparent;
    border: none;
    outline: none;
    font-size: 13px;
    color: {{fgPrimary}};
    line-height: 1.3;
}

#sideFileList::item, #sideFileTree::item {
    padding: 3px 8px;
    border-radius: 3px;
    margin: 0 4px;
    border: none;
}

#sideFileList::item:hover, #sideFileTree::item:hover {
    background-color: {{bgHover}};
    color: {{fgOnHover}};
}

#sideFileList::item:selected, #sideFileTree::item:selected {
    background-color: {{selectionBg}};
    color: {{fgOnHover}};
}

#searchInput {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
}

#searchInput:focus {
    border-color: {{borderFocus}};
}

/* ============================================================
 * 编辑器
 * ============================================================ */
QTextEdit {
    background-color: {{bgEditor}};
    color: {{fgPrimary}};
    selection-background-color: {{selectionBg}};
    selection-color: {{fgOnHover}};
    border: none;
    font-family: "Consolas", "Courier New", monospace;
    font-size: 14px;
    padding: 2px 4px;
}

QTextEdit:focus {
    border: none;
}

/* ============================================================
 * 状态栏
 * ============================================================ */
#statusBar {
    background-color: {{bgStatusBar}};
    border-top: none;
    min-height: 22px;
    max-height: 22px;
}

#statusBar QLabel {
    color: {{fgOnAccent}};
    font-size: 11px;
    background: transparent;
    border: none;
}

#statusBar QComboBox {
    color: {{fgOnAccent}};
    background-color: rgba(0, 0, 0, 0.2);
    border: none;
    border-radius: 3px;
    padding: 1px 6px;
    font-size: 11px;
    min-width: 80px;
}

#statusBar QComboBox:hover {
    background-color: rgba(255, 255, 255, 0.15);
}

#statusBar QComboBox::drop-down {
    border: none;
    width: 14px;
}

#statusBar QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid {{fgOnAccent}};
    margin-right: 2px;
}

#statusBar QComboBox QAbstractItemView {
    background-color: {{bgMenu}};
    color: {{fgPrimary}};
    selection-background-color: {{accentPrimary}};
    border: 1px solid {{borderDefault}};
}

/* 状态栏可点击项（VSCode 风格） */
#statusBar QLabel#statusBranch,
#statusBar QLabel#statusProblems,
#statusBar QLabel#statusEol,
#statusBar QLabel#statusLang,
#statusBar QLabel#statusSpaces {
    color: {{fgOnAccent}};
    font-size: 11px;
    background-color: rgba(0, 0, 0, 0.15);
    border-radius: 3px;
    padding: 0 6px;
}
#statusBar QLabel#statusBranch:hover,
#statusBar QLabel#statusProblems:hover,
#statusBar QLabel#statusEol:hover,
#statusBar QLabel#statusLang:hover,
#statusBar QLabel#statusSpaces:hover {
    background-color: rgba(255, 255, 255, 0.12);
}

/* 编码下拉框（状态栏专用，更紧凑） */
#statusEncodingCombo {
    color: {{fgOnAccent}};
    background-color: rgba(0, 0, 0, 0.18);
    border: none;
    border-radius: 3px;
    padding: 0 4px;
    font-size: 11px;
    min-width: 70px;
    max-width: 90px;
}
#statusEncodingCombo:hover {
    background-color: rgba(255, 255, 255, 0.15);
}
#statusEncodingCombo::drop-down {
    border: none;
    width: 12px;
}
#statusEncodingCombo::down-arrow {
    image: none;
    border-left: 3px solid transparent;
    border-right: 3px solid transparent;
    border-top: 4px solid {{fgOnAccent}};
    margin-right: 2px;
}

/* ============================================================
 * 滚动条
 * ============================================================ */
QScrollBar:vertical {
    width: 10px;
    background: {{scrollbarBg}};
    margin: 0;
    border: none;
    outline: none;
}

QScrollBar::handle:vertical {
    min-height: 30px;
    background-color: {{scrollbarHandle}};
    border-radius: 5px;
    margin: 2px;
    border: none;
    outline: none;
}

QScrollBar::handle:vertical:hover {
    background-color: {{scrollbarHandleHover}};
}

QScrollBar::handle:vertical:focus {
    background-color: {{scrollbarHandleHover}};
    outline: none;
    border: none;
}

QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0;
}

QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background: {{scrollbarBg}};
}

QScrollBar:horizontal {
    height: 10px;
    background: {{scrollbarBg}};
    margin: 0;
    border: none;
    outline: none;
}

QScrollBar::handle:horizontal {
    min-width: 30px;
    background-color: {{scrollbarHandle}};
    border-radius: 5px;
    margin: 2px;
    border: none;
    outline: none;
}

QScrollBar::handle:horizontal:hover {
    background-color: {{scrollbarHandleHover}};
}

QScrollBar::handle:horizontal:focus {
    background-color: {{scrollbarHandleHover}};
    outline: none;
    border: none;
}

QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal {
    width: 0;
}

QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
    background: {{scrollbarBg}};
}

/* ============================================================
 * 通用按钮
 * ============================================================ */
QPushButton[iconButton="true"] {
    border: none;
    border-radius: 4px;
    background-color: transparent;
    padding: 4px;
}

QPushButton[iconButton="true"]:hover {
    background-color: {{bgHover}};
}

QPushButton[iconButton="true"]:pressed {
    background-color: {{bgPressed}};
}

QPushButton {
    border: none;
    border-radius: 4px;
    background-color: transparent;
    padding: 4px 12px;
    color: {{fgPrimary}};
}

QPushButton:hover {
    background-color: {{bgHover}};
    color: {{fgOnHover}};
}

QPushButton:pressed {
    background-color: {{bgPressed}};
}

/* ============================================================
 * 通用下拉框
 * ============================================================ */
QComboBox {
    color: {{fgPrimary}};
    background-color: {{bgInput}};
    border: 1px solid {{borderDefault}};
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
    min-width: 120px;
}

QComboBox:hover {
    border: 1px solid {{borderHover}};
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QComboBox::down-arrow {
    image: none;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 6px solid {{fgSecondary}};
    margin-right: 8px;
}

QComboBox QAbstractItemView {
    color: {{fgPrimary}};
    background-color: {{bgMenu}};
    selection-background-color: {{selectionBg}};
    border: 1px solid {{borderDefault}};
    padding: 2px;
}

/* ============================================================
 * 补全弹窗
 * ============================================================ */
QListWidget {
    background-color: {{bgMenu}};
    border: 1px solid {{borderDefault}};
    border-radius: 6px;
    padding: 2px;
    color: {{fgPrimary}};
}

QListWidget::item {
    padding: 4px 12px;
    border-radius: 3px;
}

QListWidget::item:selected {
    background-color: {{selectionBg}};
    color: {{fgOnHover}};
}

QListWidget::item:hover:!selected {
    background-color: {{bgHover}};
}

/* ============================================================
 * 设置页面
 * ============================================================ */
#settingsPage {
    background-color: {{bgEditor}};
    padding: 24px 28px;
}

#settingsPage QLabel {
    color: {{fgPrimary}};
    font-size: 13px;
}

#settingsMainTitle {
    color: {{fgOnHover}};
    font-size: 24px;
    font-weight: 600;
    border: none;
    margin-bottom: 4px;
}

#settingsSectionTitle {
    color: {{fgOnHover}};
    font-size: 14px;
    font-weight: 600;
    padding: 16px 0 8px 0;
    border-bottom: 1px solid {{borderDefault}};
    margin-bottom: 12px;
}

#settingsHint {
    color: {{fgDisabled}};
    font-size: 12px;
    font-style: italic;
    background: transparent;
    border: none;
}

#settingsPage QCheckBox {
    color: {{fgPrimary}};
    font-size: 13px;
    spacing: 10px;
}

#settingsPage QCheckBox::indicator {
    width: 18px;
    height: 18px;
    border: 1.5px solid {{fgDisabled}};
    border-radius: 3px;
    background: transparent;
}

#settingsPage QCheckBox::indicator:hover {
    border-color: {{fgSecondary}};
}

#settingsPage QCheckBox::indicator:checked {
    background-color: {{accentPrimary}};
    border-color: {{accentPrimary}};
}

#settingsPage QComboBox {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 4px;
    padding: 6px 28px 6px 10px;
    font-size: 13px;
    min-width: 180px;
}

#settingsPage QComboBox:hover {
    border-color: {{borderHover}};
}

#settingsPage QComboBox::drop-down {
    border: none;
    width: 24px;
    subcontrol-origin: padding;
    subcontrol-position: right center;
}

#settingsPage QComboBox::down-arrow {
    image: none;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 6px solid {{fgSecondary}};
    width: 0; height: 0;
}

#settingsPage QComboBox QAbstractItemView {
    background-color: {{bgMenu}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    selection-background-color: {{selectionBg}};
    outline: none;
    padding: 2px;
}

#settingsPage QSpinBox {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 4px;
    padding: 6px 8px;
    font-size: 13px;
    min-width: 80px;
}

#settingsPage QSpinBox:hover {
    border-color: {{borderHover}};
}

/* ============================================================
 * Markdown 预览 — 仅QSS属性（CSS自定义属性由MarkdownParser嵌入HTML）
 * ============================================================ */
#mdPreview {
    background-color: {{bgEditor}};
    color: {{fgPrimary}};
    border: none;
    padding: 12px 20px;
    font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
    font-size: 14px;
    line-height: 1.7;
}
/* ============================================================
 * 内嵌终端
 * ============================================================ */
#embeddedTerminal {
    background-color: {{bgEditor}};
}

#terminalOutput {
    background-color: {{bgEditor}};
    color: {{fgPrimary}};
    border: none;
    font-family: "Consolas", "Courier New", monospace;
    font-size: 13px;
    padding: 4px 8px;
}

#terminalInput {
    background-color: {{bgEditor}};
    color: {{fgPrimary}};
    border: none;
    border-top: 1px solid {{borderDefault}};
    font-family: "Consolas", "Courier New", monospace;
    font-size: 13px;
    padding: 4px 8px;
}

#terminalInput:focus {
    border-top-color: {{borderFocus}};
}

/* 终端搜索栏 */
#terminalSearchBar {
    background-color: {{bgTitleBar}};
    border-bottom: 1px solid {{borderDefault}};
}
#terminalSearchInput {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
}
#terminalSearchInput:focus {
    border-color: {{accentPrimary}};
}
QPushButton#searchNavBtn {
    color: {{fgSecondary}};
    font-size: 14px;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 2px;
    min-width: 26px; max-width: 26px;
}
QPushButton#searchNavBtn:hover {
    background-color: {{bgHover}};
    color: {{fgOnHover}};
    border-color: {{borderHover}};
}
#searchResultLabel {
    color: {{fgSecondary}};
    font-size: 11px;
    min-width: 60px;
}

/* ============================================================
 * 右键菜单
 * ============================================================ */
QMenu {
    background-color: {{bgMenu}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 6px;
    padding: 4px 0;
}

QMenu::item {
    padding: 7px 28px;
    border-radius: 3px;
    margin: 1px 4px;
    font-size: 13px;
}

QMenu::item:selected {
    background-color: {{accentPrimary}};
    color: {{fgOnAccent}};
}

QMenu::separator {
    height: 1px;
    background-color: {{borderDefault}};
    margin: 4px 10px;
}

/* ============================================================
 * 工具提示
 * ============================================================ */
QToolTip {
    background-color: {{bgTooltip}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
}

/* ============================================================
 * 分割器手柄
 * ============================================================ */
QSplitter::handle {
    background-color: {{borderDefault}};
}

QSplitter::handle:hover {
    background-color: {{accentPrimary}};
}

QSplitter::handle:vertical {
    height: 2px;
}

QSplitter::handle:horizontal {
    width: 2px;
}

/* ============================================================
 * 对话框
 * ============================================================ */
QDialog {
    background-color: {{bgDialog}};
    color: {{fgPrimary}};
    border-radius: 8px;
    border: 1px solid {{borderDefault}};
}

QMessageBox {
    background-color: {{bgDialog}};
    color: {{fgPrimary}};
    border-radius: 8px;
    border: 1px solid {{borderDefault}};
}

QMessageBox QLabel {
    color: {{fgPrimary}};
    font-size: 13px;
    background: transparent;
    border: none;
}

QMessageBox#qt_msgbox_label {
    color: {{fgPrimary}};
    font-size: 13px;
}

QMessageBox QPushButton {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 6px;
    padding: 8px 20px;
    min-width: 80px;
    font-size: 13px;
}

QMessageBox QPushButton:hover {
    border-color: {{borderHover}};
    background-color: {{bgHover}};
}

QMessageBox QPushButton:pressed {
    background-color: {{accentPrimary}};
    color: {{fgOnAccent}};
}

QMessageBox QPushButton:default {
    background-color: {{accentPrimary}};
    color: {{fgOnAccent}};
    border-color: {{accentPrimary}};
}

QMessageBox QPushButton:default:hover {
    background-color: {{accentHover}};
}

QMessageBox QIcon {
    width: 48px;
    height: 48px;
}

QInputDialog {
    background-color: {{bgDialog}};
    color: {{fgPrimary}};
    border-radius: 8px;
    border: 1px solid {{borderDefault}};
}

QInputDialog QLabel {
    color: {{fgPrimary}};
}

QInputDialog QLineEdit {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 6px;
    padding: 8px 10px;
    selection-background-color: {{selectionBg}};
}

QInputDialog QLineEdit:focus {
    border-color: {{borderFocus}};
}

QInputDialog QPushButton {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 6px;
    padding: 8px 20px;
    min-width: 80px;
}

QInputDialog QPushButton:hover {
    border-color: {{borderHover}};
    background-color: {{bgHover}};
}

QInputDialog QPushButton:pressed {
    background-color: {{accentPrimary}};
    color: {{fgOnAccent}};
}

QFileDialog {
    background-color: {{bgDialog}};
    color: {{fgPrimary}};
}

QFileDialog QLineEdit {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 6px;
}

QFileDialog QPushButton {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 6px;
    padding: 6px 16px;
}

QFileDialog QPushButton:hover {
    border-color: {{borderHover}};
    background-color: {{bgHover}};
}

QFileDialog QListView {
    background-color: {{bgMenu}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
}

QFileDialog QListView::item {
    padding: 4px 8px;
    border-radius: 3px;
}

QFileDialog QListView::item:hover {
    background-color: {{bgHover}};
}

QFileDialog QListView::item:selected {
    background-color: {{selectionBg}};
}

/* ============================================================
 * 行编辑框
 * ============================================================ */
QLineEdit {
    background-color: {{bgInput}};
    color: {{fgPrimary}};
    border: 1px solid {{borderDefault}};
    border-radius: 4px;
    padding: 4px 8px;
    selection-background-color: {{selectionBg}};
}

QLineEdit:focus {
    border-color: {{borderFocus}};
}

/* ============================================================
 * 分隔线
 * ============================================================ */
QFrame[frameShape="4"] {
    color: {{borderDefault}};
    max-height: 1px;
}
)");

    return repl(qss);
}
