#include "controller/CommandRegistry.h"
#include "Logger.hpp"

void CommandRegistry::registerCommand(const QString& id, Handler handler)
{
    m_commands.insert(id, std::move(handler));
}

void CommandRegistry::registerPrefixCommand(const QString& prefix, PrefixHandler handler)
{
    m_prefixCommands.insert(prefix, std::move(handler));
}

bool CommandRegistry::execute(const QString& id) const
{
    // 1. 精确匹配
    auto it = m_commands.constFind(id);
    if (it != m_commands.constEnd()) {
        if (it.value()) {
            it.value()();
        }
        return true;
    }

    // 2. 前缀匹配（用于动态命令如 "snippet.insert:keyword"）
    for (auto pit = m_prefixCommands.constBegin(); pit != m_prefixCommands.constEnd(); ++pit) {
        if (id.startsWith(pit.key())) {
            QString remainder = id.mid(pit.key().length());
            if (pit.value()) {
                pit.value()(remainder);
            }
            return true;
        }
    }

    LOG_DEBUG("[CommandRegistry] 未注册的命令:" << id);
    return false;
}

QStringList CommandRegistry::commandIds() const
{
    return m_commands.keys();
}

bool CommandRegistry::contains(const QString& id) const
{
    return m_commands.contains(id);
}
