#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QColor>
#include <QMutex>

#include "interfaces/core/IThemeManager.h"

/// @brief 主题色板数据结构（完整版，覆盖所有UI元素）
struct ThemePalette {
    QString name;
    QString displayName;

    // 主色调
    QColor accentPrimary;       // 主强调色
    QColor accentHover;         // 悬浮色
    QColor accentPressed;       // 按压色

    // 背景色
    QColor bgWindow;            // 窗口背景
    QColor bgTitleBar;          // 标题栏背景
    QColor bgSideBar;           // 侧边栏背景
    QColor bgEditor;            // 编辑器背景
    QColor bgStatusBar;         // 状态栏背景
    QColor bgTabBar;            // 标签栏背景
    QColor bgTabActive;         // 活跃标签背景
    QColor bgTabInactive;       // 非活跃标签背景
    QColor bgInput;             // 输入框背景
    QColor bgActivityBar;       // 活动图标栏背景
    QColor bgHover;             // 通用悬浮背景
    QColor bgPressed;           // 通用按压背景
    QColor bgMenu;              // 菜单/下拉背景
    QColor bgTooltip;           // 工具提示背景
    QColor bgDialog;            // 对话框背景

    // 前景色
    QColor fgPrimary;           // 主文字色
    QColor fgSecondary;         // 次要文字色
    QColor fgDisabled;          // 禁用文字色
    QColor fgLineNumber;        // 行号颜色
    QColor fgOnAccent;          // 强调色上的文字色（如状态栏文字）
    QColor fgOnHover;           // 悬浮态文字色

    // 边框色
    QColor borderDefault;       // 默认边框
    QColor borderHover;         // 悬浮边框
    QColor borderFocus;         // 焦点边框

    // 特殊色
    QColor selectionBg;         // 选中背景
    QColor currentLineBg;       // 当前行背景
    QColor closeBtnHover;       // 关闭按钮悬浮
    QColor errorColor;          // 错误色
    QColor warningColor;        // 警告色

    // 滚动条
    QColor scrollbarBg;         // 滚动条轨道背景
    QColor scrollbarHandle;     // 滚动条手柄
    QColor scrollbarHandleHover;// 滚动条手柄悬浮

    // P1-2: 语法高亮配色（纳入主题管理，支持主题热切换）
    // 编辑器与 Markdown 预览共享同一套配色，消除视觉割裂
    struct SyntaxColors {
        QColor keyword;         // 关键字 (int, const, static...)
        QColor control;         // 控制流 (if, for, while, return...)
        QColor type;            // 类型 (class, struct, enum, namespace...)
        QColor string;          // 字符串
        QColor number;          // 数字
        QColor comment;         // 注释
        QColor function;        // 函数调用
        QColor funcDecl;        // 函数声明（LSP 语义，斜体）
        QColor preprocessor;    // 预处理指令 (#define, #include)
        QColor builtin;         // 内置类型/函数
        QColor decorator;       // 装饰器/注解
        QColor constant;        // 常量 (true, false, nullptr)
        QColor tag;             // HTML/XML 标签
        QColor typeDef;         // 类型定义（LSP 语义）
        QColor memberVar;       // 成员变量（LSP 语义）
        QColor localVar;        // 局部变量（LSP 语义）
        QColor yamlKey;         // YAML 键名
        QColor tomlKey;         // TOML 键名
        QColor tomlSection;     // TOML 段落头
        QColor doxy;            // P3: Doxygen 标签 (@brief/@param/@return...)
        QColor todo;            // P4: TODO/FIXME/NOTE 待办标记
        QColor headerPath;      // Bug1: #include 头文件路径（区别于普通字符串，可点击跳转）
    } syntax;

    /// 初始化为 VSCode Dark+ 配色（暗色主题用）
    void initSyntaxDark();
    /// 初始化为 GitHub Light 配色（亮色主题用）
    void initSyntaxLight();

    /// 生成完整QSS样式表
    QString generateQSS() const;
};

/// @brief 主题管理器（单例模式）
/// 管理多主题色板，支持动态注册、热切换
/// 主题切换通过Qt信号槽通知所有UI组件刷新
class ThemeManager : public QObject, public IThemeManager
{
    Q_OBJECT

public:
    static ThemeManager& instance();

    /// 注册主题色板
    void registerTheme(const QString& key, const ThemePalette& palette);

    /// 切换主题（强制应用，即使key相同也重新加载QSS）
    void switchTheme(const QString& key) override;

    /// 当前主题key
    QString currentTheme() const override { return m_currentKey; }

    /// 当前主题色板
    const ThemePalette& currentPalette() const {
        auto it = m_themes.find(m_currentKey);
        Q_ASSERT(it != m_themes.end());
        return it.value();
    }

    /// 获取所有已注册主题
    QStringList themeKeys() const override { return m_themes.keys(); }

    /// 获取主题显示名
    QString themeDisplayName(const QString& key) const override;

    /// 预设主题工厂
    static ThemePalette createPurpleDark();
    static ThemePalette createBlueDark();
    static ThemePalette createBlackDark();
    static ThemePalette createLightClassic();   // 亮色经典主题（蓝调）
    static ThemePalette createPinkLight();      // 白粉主题（粉紫调）

signals:
    void themeChanged(const QString& themeKey);

private:
    ThemeManager();
    ~ThemeManager() override = default;
    Q_DISABLE_COPY(ThemeManager)

    QMap<QString, ThemePalette> m_themes;
    QString m_currentKey;
    mutable QMutex m_mutex;     // 线程安全保护
};

#endif // THEMEMANAGER_H
