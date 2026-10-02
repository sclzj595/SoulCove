#include "core/shortcut/ShortcutFilter.h"
#include "core/shortcut/ShortcutManager.h"
#include "Logger.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QWidget>

// ================================================================
// Singleton
// ================================================================

ShortcutFilter& ShortcutFilter::instance()
{
    static ShortcutFilter inst;
    return inst;
}

ShortcutFilter::ShortcutFilter()
    : QObject(nullptr)
{
}

// ================================================================
// 命令注册 (Command Pattern: 注册 ConcreteCommand)
// ================================================================

void ShortcutFilter::registerCommand(std::unique_ptr<IShortcutCommand> cmd)
{
    if (!cmd) return;

    const QString id = cmd->commandId();
    // 防止重复注册：移除旧命令（std::vector 支持 unique_ptr move-only 元素）
    for (auto it = m_commands.begin(); it != m_commands.end(); /* no inc */) {
        if ((*it)->commandId() == id) {
            it = m_commands.erase(it);
        } else {
            ++it;
        }
    }
    m_commands.push_back(std::move(cmd));
    rebuildBindings();
}

IShortcutCommand* ShortcutFilter::findCommand(const QString& id) const
{
    for (const auto& cmd : m_commands) {
        if (cmd->commandId() == id)
            return cmd.get();
    }
    return nullptr;
}

// ================================================================
// 安装过滤器
// ================================================================

void ShortcutFilter::installGlobal()
{
    if (m_installed) return;
    qApp->installEventFilter(this);
    m_installed = true;

    // Observer Pattern: 监听 ShortcutManager 变更
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutChanged,
            this, &ShortcutFilter::onShortcutChanged);

    // 初始化快捷键绑定（优先使用用户自定义，其次默认）
    rebuildBindings();

    LOG_INFO_S("ShortcutFilter", "installGlobal",
        "快捷键过滤器已安装到全局 (qApp)，已注册 " << m_commands.size() << " 个命令");
}

void ShortcutFilter::installOn(QWidget* target)
{
    if (!target) return;
    target->installEventFilter(this);
}

// ================================================================
// 上下文切换 (Strategy Pattern)
// ================================================================

void ShortcutFilter::setActiveContext(const QString& context)
{
    // 防抖：上下文未变化时直接返回，避免高频重复日志与无效处理
    // （鼠标移动、点击、切换标签会频繁触发，但上下文往往未变）
    if (m_activeContext == context) return;

    m_activeContext = context;
    LOG_DEBUG_S("ShortcutFilter", "setActiveContext", "活跃上下文切换为: " << context);
}

bool ShortcutFilter::isCommandActive(const IShortcutCommand* cmd) const
{
    if (!cmd) return false;
    const QString& ctx = cmd->context();
    // "global" 始终活跃；否则必须匹配当前上下文
    return ctx == QStringLiteral("global") || ctx == m_activeContext;
}

// ================================================================
// 绑定表重建 (Observer Pattern: 响应配置变更 + Template Method)
// ================================================================

void ShortcutFilter::rebuildBindings()
{
    m_bindings.clear();
    ShortcutManager& mgr = ShortcutManager::instance();

    for (const auto& cmd : m_commands) {
        const QString& id = cmd->commandId();
        // 优先使用用户在 ShortcutManager 中自定义的快捷键
        ShortcutItem item = mgr.shortcut(id);
        if (!item.id.isEmpty() && !item.currentKey.isEmpty()) {
            m_bindings[id] = item.currentKey;
        } else {
            m_bindings[id] = cmd->defaultKey();
        }
    }
}

void ShortcutFilter::onShortcutChanged(const QString& id,
                                        const QKeySequence& oldKey,
                                        const QKeySequence& newKey)
{
    Q_UNUSED(oldKey)
    if (m_bindings.contains(id)) {
        m_bindings[id] = newKey;
        LOG_DEBUG_S("ShortcutFilter", "onShortcutChanged",
            "快捷键已更新: " << id << " → " << newKey.toString());
    }
}

// ================================================================
// 核心: 事件过滤器 (Filter/Interceptor Pattern)
// ================================================================

bool ShortcutFilter::eventFilter(QObject* obj, QEvent* event)
{
    // 仅处理按键按下事件
    if (event->type() != QEvent::KeyPress)
        return QObject::eventFilter(obj, event);

    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

    // 无修饰键时：仅对可打印字符放行，功能键(F1-F12/方向键/Escape等)仍进入命令匹配
    // 修复：F12 是无修饰键单键，原逻辑直接放行导致 lsp.gotoDefinition 永不触发
    if (keyEvent->modifiers() == Qt::NoModifier) {
        int k = keyEvent->key();
        bool isFunctionOrControlKey =
            (k >= Qt::Key_F1 && k <= Qt::Key_F35) ||
            k == Qt::Key_Escape || k == Qt::Key_Tab || k == Qt::Key_Backtab ||
            k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Insert ||
            k == Qt::Key_Delete || k == Qt::Key_Home || k == Qt::Key_End ||
            k == Qt::Key_PageUp || k == Qt::Key_PageDown ||
            k == Qt::Key_Left || k == Qt::Key_Right ||
            k == Qt::Key_Up || k == Qt::Key_Down;
        if (!isFunctionOrControlKey)
            return QObject::eventFilter(obj, event);
    }

    // 构建按键序列用于匹配
    QKeySequence pressed(
        keyEvent->key() | keyEvent->modifiers());

    // 在注册的命令中查找匹配
    for (const auto& cmd : m_commands) {
        const QString& id = cmd->commandId();
        if (!m_bindings.contains(id)) continue;

        // 快捷键匹配 + 上下文匹配 + 启用状态
        if (m_bindings[id] == pressed
            && isCommandActive(cmd.get())
            && cmd->isEnabled())
        {
            cmd->execute();
            return true;  // 事件已消费，不再传播
        }
    }

    // 未匹配 → 传播事件
    return QObject::eventFilter(obj, event);
}
