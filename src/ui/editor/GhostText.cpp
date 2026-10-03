#include "ui/editor/GhostText.h"
#include "core/ai/AIClient.h"
#include "core/ai/AIProviderStore.h"
#include "core/config/ConfigManager.h"
#include "core/config/ThemeManager.h"
#include "Logger.hpp"

#include <QKeyEvent>
#include <QRegularExpression>

// ============================================================
// M8 stage5: AI 内联补全 ghost text
// ============================================================

GhostText::GhostText(QTextEdit* editor, QObject* parent)
    : QObject(parent)
    , m_editor(editor)
    , m_client(new AIClient(this))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(800);
    connect(&m_debounce, &QTimer::timeout, this, &GhostText::requestCompletion);

    // 文档变更三态：本类插入 / 本类撤下（统一走 m_inserting 抑制）；
    // 外部编辑（ghost 存活期间用户/程序改了文档）→ 撤下；普通输入 → 重置防抖
    connect(m_editor, &QTextEdit::textChanged, this, [this]() {
        if (m_inserting) {
            m_inserting = false;
            return;
        }
        if (isActive()) {
            removeGhostText(false);   // 外部变更导致 ghost 失效，直接撤下
            emit refreshRequested();
            return;
        }
        if (m_triggerGuard && m_triggerGuard()) return;   // 让位（补全弹窗可见等）
        m_debounce.start();
    });

    connect(m_client, &AIClient::deltaReceived, this, [this](const QString& d) {
        m_replyBuffer += d;
    });
    connect(m_client, &AIClient::finished, this, [this](bool ok, const QString& err) {
        const QString reply = m_replyBuffer;
        m_replyBuffer.clear();
        if (!ok) {
            LOG_DEBUG("[GhostText] 内联补全请求失败:" << err.toStdString());
            return;
        }
        showGhost(reply);
    });
}

GhostText::~GhostText()
{
    if (isActive()) {
        m_inserting = true;
        m_ghostCursor.removeSelectedText();
    }
}

bool GhostText::isActive() const
{
    return !m_ghostText.isEmpty();
}

bool GhostText::isBusy() const
{
    return m_client && m_client->isBusy();
}

bool GhostText::handleKeyPress(QKeyEvent* event)
{
    if (!isActive()) return false;

    switch (event->key()) {
    case Qt::Key_Escape:
        dismiss();
        return true;    // 已消费，Esc 不再传给补全弹窗等其他组件
    case Qt::Key_Tab:
        // 接受：文本保留为正常编辑一步，光标移到 ghost 末尾
        {
            QTextCursor cur = m_editor->textCursor();
            cur.setPosition(m_ghostCursor.position());
            m_editor->setTextCursor(cur);
            clearGhostMarkers();
            emit refreshRequested();
        }
        return true;
    default:
        // 其余按键（含普通输入/方向键/回车）：先撤下 ghost 再放行默认处理
        dismiss();
        return false;
    }
}

void GhostText::dismiss()
{
    if (!isActive()) return;
    removeGhostText(true);   // 撤销合并：Ctrl+Z 不会翻出幽灵文本
    emit refreshRequested();
}

void GhostText::requestCompletion()
{
    if (m_editor->isReadOnly() || isActive() || isBusy()) return;
    if (m_triggerGuard && m_triggerGuard()) return;
    // M8 收口: 设置页开关（每次请求时读取，改配置即实时生效）
    if (!ConfigManager::instance().inlineCompletion()) return;

    const AIProvider p = AIProviderStore::instance().activeProvider();
    if (p.id.isEmpty()) return;

    // 上下文：光标前最多 1500 字符（约 3~5 行），⟨CURSOR⟩ 标记续写点
    QTextCursor ctx = m_editor->textCursor();
    const int pos = ctx.position();
    ctx.setPosition(qMax(0, pos - 1500));
    ctx.setPosition(pos, QTextCursor::KeepAnchor);
    QString prefix = ctx.selectedText();
    prefix.replace(QChar(0x2029), QChar('\n'));
    if (prefix.trimmed().isEmpty()) return;

    QList<AIChatMessage> msgs;
    msgs.append({ QStringLiteral("system"),
                  QStringLiteral("你是代码内联补全引擎。根据上下文输出 ⟨CURSOR⟩ 处应续写的代码（最多 4 行）。"
                                 "只输出要续写的代码本身：无解释、无 markdown 围栏、不要重复已有内容。无法补全时只输出空。") });
    msgs.append({ QStringLiteral("user"), prefix + QStringLiteral("⟨CURSOR⟩") });

    m_replyBuffer.clear();
    m_client->chatOnce(p, msgs);
}

void GhostText::showGhost(const QString& rawReply)
{
    if (m_editor->isReadOnly() || isActive()) return;

    // 清洗：剥 markdown 围栏、限 4 行 / 240 字符
    QString text = rawReply;
    text.remove(QRegularExpression(QStringLiteral("^```[a-zA-Z0-9+#]*\\n?")));
    text.remove(QRegularExpression(QStringLiteral("\\n?```\\s*$")));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QStringList lines = text.split(QLatin1Char('\n'));
    while (lines.size() > 4) lines.removeLast();
    text = lines.join(QLatin1Char('\n')).left(240).trimmed();
    if (text.isEmpty()) return;

    // 模型复读光标后已有内容 → 不插（避免重复代码）
    const QString after = m_editor->toPlainText().mid(
        m_editor->textCursor().position(), text.size());
    if (after == text) return;

    m_inserting = true;
    QTextCursor cur = m_editor->textCursor();
    cur.insertText(text);
    // m_ghostCursor：anchor=ghost 起点、position=ghost 末尾，随文档编辑自动调整
    m_ghostCursor = cur;
    m_ghostCursor.setPosition(cur.position() - text.size());
    m_ghostText = text;
    // 编辑器自身 textCursor 未被移动 —— 停留在 ghost 起点，即"插入在光标之后"
    emit refreshRequested();
}

void GhostText::removeGhostText(bool joinUndo)
{
    if (m_ghostText.isEmpty()) {
        clearGhostMarkers();
        return;
    }
    m_inserting = true;
    QTextCursor cur = m_ghostCursor;
    if (joinUndo) cur.joinPreviousEditBlock();   // 与插入合并为同一撤销步
    cur.removeSelectedText();
    if (joinUndo) cur.endEditBlock();
    clearGhostMarkers();
}

void GhostText::clearGhostMarkers()
{
    m_ghostText.clear();
    m_ghostCursor = QTextCursor();
}

void GhostText::appendSelection(QList<QTextEdit::ExtraSelection>& out) const
{
    if (!isActive()) return;
    QTextEdit::ExtraSelection sel;
    sel.cursor = m_ghostCursor;
    // M8 收口: 主题感知 —— 用当前主题次级前景色（明暗主题自动适配）
    sel.format.setForeground(ThemeManager::instance().currentPalette().fgSecondary);
    out.append(sel);
}
