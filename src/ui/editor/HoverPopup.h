#pragma once

#include <QWidget>
#include <QTextBrowser>
#include <QString>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

/// @file HoverPopup.h
/// @brief LSP 悬停预览弹窗 — Markdown 富文本渲染 + 淡入淡出动画
///
/// 设计模式：策略模式（Markdown→HTML 转换策略）
/// 职责：将 LSP 返回的 Markdown 文档转换为带样式的 HTML，分层富文本展示
///
/// 交互特性：
/// - 淡入淡出动画（150ms），消除突兀闪现
/// - H2: 50ms 防闪烁隐藏延时（鼠标离开后立即关闭，仅保留极短防抖防误闪）
/// - 鼠标进入弹窗时取消隐藏（允许用户阅读/选择文本）
/// - 鼠标离开弹窗时触发延时隐藏
///
/// 支持的 Markdown 语法：
/// - 标题 #/##/### → 放大字号 + 加粗
/// - 分割线 --- → 水平分隔线
/// - 代码块 ```cpp → 深色背景 + C++ 语法高亮
/// - Doxygen 标签 @brief/@param/@return → 特殊颜色高亮
/// - 行内代码 `code` → 浅色背景 + 等宽字体
/// - 普通段落 → 自动换行

/// @brief Markdown 悬停预览弹窗
class HoverPopup : public QWidget
{
    Q_OBJECT

public:
    explicit HoverPopup(QWidget* parent = nullptr);

    /// @brief 设置 Markdown 内容并显示（带淡入动画）
    /// @param markdown LSP 返回的 Markdown 原文
    /// @param pos 全局坐标位置（弹窗左上角对齐到此点下方）
    void showMarkdown(const QString& markdown, const QPoint& pos);

    /// @brief 隐藏弹窗（带淡出动画）
    void hidePopup();

    /// @brief 立即隐藏弹窗（无动画，用于切页/按键等即时场景）
    void hideImmediately();

protected:
    void focusOutEvent(QFocusEvent* event) override;
    /// 鼠标进入弹窗 → 取消防闪烁隐藏
    void enterEvent(QEnterEvent* event) override;
    /// 鼠标离开弹窗 → 启动防闪烁隐藏延时
    void leaveEvent(QEvent* event) override;

private:
    /// 将 Markdown 文本转换为带样式的 HTML
    QString markdownToHtml(const QString& markdown) const;

    /// 转义 HTML 特殊字符
    QString escapeHtml(const QString& text) const;

    /// 处理行内 Markdown 语法（行内代码、Doxygen 标签、加粗）
    QString processInlineMarkdown(const QString& text) const;

    /// 对 C++ 代码进行简单的关键字/类型/字符串/注释高亮
    QString highlightCppCode(const QString& code) const;

    /// 生成弹窗的 CSS 样式表
    QString generateStylesheet() const;

    /// 根据内容自适应弹窗大小
    void adjustSizeToFit();

    /// 启动淡入动画
    void startFadeIn();

    /// 启动淡出动画（动画结束后隐藏）
    void startFadeOut();

    QTextBrowser* m_textBrowser;

    // 淡入淡出动画
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_fadeAnimation = nullptr;

    // H2: 防闪烁隐藏延时（鼠标离开后 50ms 才隐藏，期间返回则取消）
    QTimer m_hideDelayTimer;
    bool m_fadingOut = false;  ///< 当前是否正在淡出
};
