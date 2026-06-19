#ifndef UIFACTORY_H
#define UIFACTORY_H

#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QWidget>
#include <QString>
#include <memory>

class IMarkdownParser;
class IUiLibrary;

/// @brief UI组件工厂
/// 统一创建美化按钮、标签、下拉框等UI组件
/// 支持动态切换原生控件/第三方UI库控件（开闭原则）
/// 通过IUiLibrary接口实现UI库可插拔
class UIFactory
{
public:
    /// 创建图标按钮（支持hover双态图标）
    static QPushButton* createIconButton(QWidget* parent,
                                         const QString& normalIcon,
                                         const QString& hoverIcon,
                                         int size = 40);

    /// 创建状态栏标签
    static QLabel* createStatusLabel(QWidget* parent, const QString& text = QString());

    /// 创建编码选择下拉框
    static QComboBox* createEncodingComboBox(QWidget* parent);

    /// 创建工具栏容器
    static QWidget* createToolBar(QWidget* parent);

    /// 创建状态栏容器
    static QWidget* createStatusBar(QWidget* parent);

    /// 创建 Markdown 解析器
    /// @param type "maddy"(默认) | "simple"(自研fallback)
    /// @return 解析器实例（调用方负责释放）
    static IMarkdownParser* createMarkdownParser(const QString& type = QStringLiteral("maddy"));

    /// 通过IUiLibrary接口创建无边框窗口
    static QWidget* createFramelessWindow(QWidget* parent = nullptr);

    /// 通过IUiLibrary接口创建现代化按钮
    static QWidget* createModernButton(const QString& text, QWidget* parent = nullptr);

    /// 获取当前UI库实例引用
    static IUiLibrary& uiLibrary();

private:
    UIFactory() = delete;   // 纯静态工具类，禁止外部实例化

    /// 确保UI库实例已初始化（懒加载）
    static IUiLibrary& ensureUiLib();

    static std::unique_ptr<IUiLibrary> m_uiLib;  // UI库实例（可插拔）
};

#endif // UIFACTORY_H
