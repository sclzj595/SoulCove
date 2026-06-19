#ifndef SHORTCUTFILTER_H
#define SHORTCUTFILTER_H

#include "interfaces/shortcut/IShortcutCommand.h"

#include <QObject>
#include <QKeySequence>
#include <QHash>
#include <QVector>
#include <memory>

/// @brief 快捷键过滤器 — 统一拦截键盘事件并分发到注册的命令
///
/// ====== 设计模式 ======
///
///   Command Pattern  — IShortcutCommand 封装操作，Filter 作为 Invoker
///   Filter/Interceptor Pattern — eventFilter 拦截所有按键，匹配合并消费
///   Observer Pattern  — 监听 ShortcutManager::shortcutChanged 自动同步绑定
///   Singleton Pattern  — 全局唯一实例，确保快捷键无冲突
///   Strategy Pattern  — 不同上下文("global"/"editor"/"terminal")用不同命令集
///
/// ====== 使用方式 ======
///
///   // 1. 注册命令
///   auto& filter = ShortcutFilter::instance();
///   filter.registerCommand(make_command(
///       "edit.format", "格式化文档", "编辑",
///       QKeySequence("Ctrl+Shift+I"), "editor",
///       [this]{ onFormatDocument(); }));
///
///   // 2. 安装全局过滤器（推荐）
///   filter.installGlobal();  // 安装到 qApp，覆盖所有焦点
///
///   // 3. 切换上下文（可选，切换编辑器/终端/全局）
///   filter.setActiveContext("editor");
///
class ShortcutFilter : public QObject
{
    Q_OBJECT

public:
    static ShortcutFilter& instance();

    // 禁用拷贝 (Singleton)
    ShortcutFilter(const ShortcutFilter&) = delete;
    ShortcutFilter& operator=(const ShortcutFilter&) = delete;

    /// 注册一个快捷键命令（Command Pattern: 注册 ConcreteCommand）
    void registerCommand(std::unique_ptr<IShortcutCommand> cmd);

    /// 安装到全局（qApp），拦截所有按键
    void installGlobal();

    /// 安装到指定 widget（仅拦截该 widget 及其子控件的事件）
    void installOn(QWidget* target);

    /// 切换活跃上下文（用于区分编辑器/终端/全局快捷键）
    void setActiveContext(const QString& context);

    /// 获取活跃上下文
    QString activeContext() const { return m_activeContext; }

    /// 获取所有已注册的命令（只读）
    const std::vector<std::unique_ptr<IShortcutCommand>>& commands() const {
        return m_commands;
    }

    /// 查找命令
    IShortcutCommand* findCommand(const QString& id) const;

protected:
    /// Filter Pattern 核心: 拦截所有 keyPress 事件
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    /// Observer Pattern: 响应快捷键配置变更
    void onShortcutChanged(const QString& id,
                           const QKeySequence& oldKey,
                           const QKeySequence& newKey);

private:
    ShortcutFilter();   // 私有构造 (Singleton)
    ~ShortcutFilter() override = default;

    /// 重建 commandId → QKeySequence 绑定表
    void rebuildBindings();

    /// 判断给定命令在活跃上下文中是否应响应
    bool isCommandActive(const IShortcutCommand* cmd) const;

    std::vector<std::unique_ptr<IShortcutCommand>> m_commands;
    QHash<QString, QKeySequence> m_bindings;      // commandId → 当前绑定的快捷键
    QString m_activeContext = QStringLiteral("global");
    bool m_installed = false;
};

#endif // SHORTCUTFILTER_H
