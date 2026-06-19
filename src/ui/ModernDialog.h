#ifndef MODERNDIALOG_H
#define MODERNDIALOG_H

#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>

/// @brief 现代化弹窗组件，替代原生 QMessageBox / QInputDialog
///
/// 特性：
/// - 圆角 + 阴影 + 透明度动画
/// - 自动适配 ThemeManager 主题色
/// - 支持 Info / Warning / Question / Error 四种图标类型
/// - 支持标准按钮 (Ok / Yes/No / Save/Discard/Cancel)
/// - 支持输入模式 (单行文本输入，替代 QInputDialog::getText)
/// - 模态执行，返回 StandardButton 或输入文本
class ModernDialog : public QDialog
{
    Q_OBJECT

public:
    /// @brief 弹窗图标类型
    enum class IconType {
        None,       ///< 无图标
        Info,       ///< 信息提示（蓝色 i）
        Warning,    ///< 警告（黄色 !）
        Question,   ///< 询问（蓝色 ?）
        Error       ///< 错误（红色 x）
    };
    Q_ENUM(IconType)

    /// @brief 标准按钮角色
    enum class ButtonRole {
        None = 0,
        Accept,     ///< 确认/保存/是
        Reject,     ///< 取消
        Destructive ///< 不保存/否/删除
    };

    explicit ModernDialog(QWidget* parent = nullptr);
    ~ModernDialog() override = default;

    // ========== 配置方法链 ==========

    /// @brief 设置标题
    ModernDialog& setTitle(const QString& title);

    /// @brief 设置正文内容（支持 \n 换行）
    ModernDialog& setMessage(const QString& message);

    /// @brief 设置图标类型
    ModernDialog& setIcon(IconType type);

    /// @brief 添加一个自定义按钮
    /// @param text 按钮文字
    /// @param role 按钮角色（决定样式和返回值）
    ModernDialog& addButton(const QString& text, ButtonRole role);

    // ========== 快捷工厂方法（静态）==========

    /// @brief 信息弹窗（单按钮 OK）
    static int information(QWidget* parent, const QString& title, const QString& text);

    /// @brief 警告弹窗（单按钮 OK）
    static int warning(QWidget* parent, const QString& title, const QString& text);

    /// @brief 错误弹窗（单按钮 OK）
    static int critical(QWidget* parent, const QString& title, const QString& text);

    /// @brief 询问弹窗（Yes / No）
    static int question(QWidget* parent, const QString& title, const QString& text);

    /// @brief 三按钮确认弹窗（保存 / 不保存 / 取消），返回点击的角色值
    static int confirm(QWidget* parent, const QString& title, const QString& text);

    /// @brief 输入弹窗（替代 QInputDialog::getText）
    /// @param ok 输出：是否点了确认
    /// @return 用户输入的文本；取消时返回空字符串
    static QString getText(QWidget* parent, const QString& title,
                           const QString& label, const QString& text = QString(),
                           bool* ok = nullptr);

    /// @brief 列表选择弹窗（替代 QInputDialog::getItem）
    /// @param items 可选项列表
    /// @param current 当前选中索引
    /// @param ok 输出：是否点了确认
    /// @return 选中的文本；取消时返回空字符串
    static QString getItem(QWidget* parent, const QString& title,
                           const QString& label, const QStringList& items,
                           int current = 0, bool* ok = nullptr);

    // ========== 公开常量（用于 exec() 返回值判断）==========
    static constexpr int ROLE_ACCEPT     = 1;  ///< 确认/保存/是
    static constexpr int ROLE_REJECT     = 0;  ///< 取消
    static constexpr int ROLE_DESTRUCTIVE = 2; ///< 不保存/否/删除

protected:
    void showEvent(QShowEvent* event) override;

private:
    void setupUI();
    void applyTheme();
    QLabel* createIconLabel(IconType type) const;
    QPixmap createIconPixmap(IconType type) const;
    QPushButton* createButton(const QString& text, ButtonRole role) const;
    void animateOpen();

    QVBoxLayout* m_layout = nullptr;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QLineEdit* m_inputEdit = nullptr;   ///< 输入模式下的文本框
    QListWidget* m_listWidget = nullptr; ///< 列表选择模式
    QDialogButtonBox* m_buttonBox = nullptr;

    IconType m_iconType = IconType::None;
    int m_resultRole = 0;               ///< exec() 返回值
};

#endif // MODERNDIALOG_H
