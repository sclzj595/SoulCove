#ifndef ISHORTCUTCOMMAND_H
#define ISHORTCUTCOMMAND_H

#include <QString>
#include <QKeySequence>

/// @brief 快捷键命令抽象接口 (Command Pattern)
///
/// 设计模式：
///   Command Pattern — 将"操作"封装为对象，支持队列/日志/撤销
///   Strategy Pattern — 不同的 IShortcutCommand 实现可插拔替换
///
/// 每个命令有唯一 ID（与 ShortcutManager 中的键对应），
/// 由 ShortcutFilter 统一拦截键盘事件并分发执行。
class IShortcutCommand
{
public:
    virtual ~IShortcutCommand() = default;

    /// 命令唯一标识符（如 "edit.format"、"file.save"）
    virtual QString commandId() const = 0;

    /// 显示名称（如 "格式化文档"）
    virtual QString displayName() const = 0;

    /// 分类（如 "文件"、"编辑"、"视图"）
    virtual QString category() const = 0;

    /// 默认快捷键序列
    virtual QKeySequence defaultKey() const = 0;

    /// 所属上下文（"global" 始终有效，"editor" 仅编辑器焦点时有效，"terminal" 仅终端焦点时）
    virtual QString context() const { return QStringLiteral("global"); }

    /// 当前是否可用（不可用时快捷键被忽略）
    virtual bool isEnabled() const { return true; }

    /// 执行命令逻辑（核心方法）
    virtual void execute() = 0;
};

// ================================================================
// LambdaCommand — 便捷实现：用 lambda 构造命令
// ================================================================

/// @brief 基于 lambda 的快捷键命令实现 (Strategy + Template Method)
///
/// 使用示例：
///   auto cmd = make_command("edit.format", "格式化", "编辑",
///       QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_I), "editor",
///       [&]{ formatter.format(document); });
class LambdaCommand : public IShortcutCommand
{
public:
    using Action = std::function<void()>;
    using Condition = std::function<bool()>;

    LambdaCommand(QString id, QString name, QString cat,
                  QKeySequence defKey, QString ctx = QStringLiteral("global"),
                  Action act = nullptr, Condition en = nullptr)
        : m_id(std::move(id)), m_name(std::move(name)), m_cat(std::move(cat))
        , m_defKey(defKey), m_ctx(ctx.isEmpty() ? QStringLiteral("global") : ctx)
        , m_action(std::move(act)), m_enabled(std::move(en))
    {}

    QString     commandId()   const override { return m_id; }
    QString     displayName() const override { return m_name; }
    QString     category()    const override { return m_cat; }
    QKeySequence defaultKey() const override { return m_defKey; }
    QString     context()     const override { return m_ctx; }

    bool isEnabled() const override {
        return m_enabled ? m_enabled() : true;
    }

    void execute() override {
        if (m_action) m_action();
    }

    /// 动态修改执行逻辑（用于命令绑定需要运行时上下文的情况）
    void setAction(Action act) { m_action = std::move(act); }

private:
    QString m_id, m_name, m_cat, m_ctx;
    QKeySequence m_defKey;
    Action m_action;
    Condition m_enabled;
};

/// @brief 工厂函数，创建 LambdaCommand 的 unique_ptr
inline std::unique_ptr<LambdaCommand> make_command(
    QString id, QString name, QString cat,
    QKeySequence defKey, QString ctx = QStringLiteral("global"),
    LambdaCommand::Action act = nullptr, LambdaCommand::Condition en = nullptr)
{
    return std::make_unique<LambdaCommand>(
        std::move(id), std::move(name), std::move(cat),
        defKey, ctx, std::move(act), std::move(en)
    );
}

#endif // ISHORTCUTCOMMAND_H
