#ifndef COMMANDREGISTRY_H
#define COMMANDREGISTRY_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>

/// @brief 命令注册表（Command Pattern + Registry Pattern）
///
/// 替代 Widget::onCommandTriggered 中的 184 行 if-else 链，
/// 用哈希表 O(1) 查找命令处理器。
///
/// 设计要点：
/// - 命令ID格式: "category.action"（如 "file.open" / "edit.format"）
/// - 每个命令绑定一个 std::function<void()> 回调
/// - 支持前缀匹配（用于 "snippet.insert:keyword" 等动态命令）
/// - 不依赖 QObject，纯 C++ 类，降低耦合
///
/// 使用方式：
///   CommandRegistry registry;
///   registry.registerCommand("file.open", [this]{ onOpen(); });
///   registry.execute("file.open");  // 调用 onOpen()
class CommandRegistry
{
public:
    /// 命令处理器类型
    using Handler = std::function<void()>;

    /// 前缀处理器类型（接收命令ID的剩余部分）
    using PrefixHandler = std::function<void(const QString&)>;

    /// 注册命令
    /// @param id 命令ID（如 "file.open"）
    /// @param handler 命令处理器
    void registerCommand(const QString& id, Handler handler);

    /// 注册前缀命令（用于动态命令，如 "snippet.insert:keyword"）
    /// @param prefix 命令前缀（如 "snippet.insert:"）
    /// @param handler 前缀处理器（接收前缀后的部分）
    void registerPrefixCommand(const QString& prefix, PrefixHandler handler);

    /// 执行命令
    /// @param id 命令ID
    /// @return true 如果命令存在并执行，false 如果未找到
    bool execute(const QString& id) const;

    /// 查询所有已注册的命令ID
    QStringList commandIds() const;

    /// 查询命令是否已注册
    bool contains(const QString& id) const;

private:
    QHash<QString, Handler> m_commands;          ///< 精确匹配命令表
    QHash<QString, PrefixHandler> m_prefixCommands; ///< 前缀匹配命令表
};

#endif // COMMANDREGISTRY_H
