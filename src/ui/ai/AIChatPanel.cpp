#include "ui/ai/AIChatPanel.h"
#include "Logger.hpp"
#include "core/config/ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QScrollBar>
#include <QDateTime>
#include <QGuiApplication>
#include <QClipboard>
#include <algorithm>

AIChatPanel::AIChatPanel(QWidget* parent)
    : QWidget(parent)
    , m_client(new AIClient(this))
{
    setupUi();
    applyTheme();
    refreshProviders();
    connect(&m_flushTimer, &QTimer::timeout, this, [this]() {
        if (m_dirty) {
            m_dirty = false;
            rebuildHtml();
        }
    });
    m_flushTimer.setInterval(100);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this]() { applyTheme(); rebuildHtml(); });
}

void AIChatPanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // === 顶部：服务商选择 + 新会话 ===
    auto* topRow = new QHBoxLayout();
    auto* providerLabel = new QLabel(tr("服务商:"), this);
    m_providerCombo = new QComboBox(this);
    m_providerCombo->setToolTip(tr("在 设置 → AI 助手 中管理服务商"));
    m_newSessionBtn = new QPushButton(tr("新会话"), this);
    topRow->addWidget(providerLabel);
    topRow->addWidget(m_providerCombo, 1);
    topRow->addWidget(m_newSessionBtn);
    layout->addLayout(topRow);

    // === 中部：对话历史 ===
    m_historyView = new QTextBrowser(this);
    m_historyView->setOpenExternalLinks(true);
    layout->addWidget(m_historyView, 1);

    // === 回答操作行 ===
    auto* actionRow = new QHBoxLayout();
    m_statusLabel = new QLabel(tr("就绪"), this);
    m_copyBtn = new QPushButton(tr("复制回答"), this);
    m_insertBtn = new QPushButton(tr("插入到编辑器"), this);
    actionRow->addWidget(m_statusLabel, 1);
    actionRow->addWidget(m_copyBtn);
    actionRow->addWidget(m_insertBtn);
    layout->addLayout(actionRow);

    // === 底部：输入区 ===
    auto* inputRow = new QHBoxLayout();
    m_inputEdit = new QPlainTextEdit(this);
    m_inputEdit->setPlaceholderText(tr("向 AI 提问…（Enter 发送，Shift+Enter 换行）"));
    m_inputEdit->setMaximumHeight(96);
    m_sendBtn = new QPushButton(tr("发送"), this);
    m_stopBtn = new QPushButton(tr("停止"), this);
    m_stopBtn->setEnabled(false);
    inputRow->addWidget(m_inputEdit, 1);
    inputRow->addWidget(m_sendBtn);
    inputRow->addWidget(m_stopBtn);
    layout->addLayout(inputRow);

    // 信号
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { /* 切换服务商仅影响下一次发送 */ });
    connect(m_newSessionBtn, &QPushButton::clicked, this, &AIChatPanel::onNewSessionClicked);
    connect(m_sendBtn, &QPushButton::clicked, this, &AIChatPanel::onSendClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &AIChatPanel::onStopClicked);
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        // 复制最近一条 AI 回答（流式进行中取累积内容）
        QString last;
        for (int i = m_history.size() - 1; i >= 0; --i) {
            if (m_history[i].role == QStringLiteral("assistant")) { last = m_history[i].content; break; }
        }
        if (last.isEmpty()) last = m_pendingAnswer;
        if (!last.isEmpty()) QGuiApplication::clipboard()->setText(last);
    });
    connect(m_insertBtn, &QPushButton::clicked, this, [this]() {
        QString last;
        for (int i = m_history.size() - 1; i >= 0; --i) {
            if (m_history[i].role == QStringLiteral("assistant")) { last = m_history[i].content; break; }
        }
        if (last.isEmpty()) last = m_pendingAnswer;
        if (!last.isEmpty()) emit insertToEditorRequested(last);
    });
    connect(m_client, &AIClient::deltaReceived, this, &AIChatPanel::onDelta);
    connect(m_client, &AIClient::finished, this, &AIChatPanel::onFinished);
    m_inputEdit->installEventFilter(this);
    updateButtons();
}

void AIChatPanel::applyTheme()
{
    const auto& palette = ThemeManager::instance().currentPalette();
    m_historyView->setStyleSheet(QStringLiteral(
        "QTextBrowser { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 6px; }")
        .arg(palette.bgEditor.name(), palette.fgPrimary.name(), palette.borderDefault.name()));
    m_inputEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px; }")
        .arg(palette.bgInput.name(), palette.fgPrimary.name(), palette.borderDefault.name()));
    m_statusLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(palette.fgSecondary.name()));
}

void AIChatPanel::refreshProviders()
{
    const QString prevId = m_comboIds.value(m_providerCombo->currentIndex(), QString());
    QSignalBlocker blocker(m_providerCombo);
    m_providerCombo->clear();
    m_comboIds.clear();

    const auto& providers = AIProviderStore::instance().providers();
    for (const AIProvider& p : providers) {
        if (!p.enabled) continue;
        m_providerCombo->addItem(
            QStringLiteral("%1（%2）").arg(p.name.isEmpty() ? QStringLiteral("未命名") : p.name, p.model), p.id);
        m_comboIds.append(p.id);
    }

    // 优先恢复原选择，否则仓库的 activeProvider
    const AIProvider active = AIProviderStore::instance().activeProvider();
    QString target = m_comboIds.contains(prevId) ? prevId : active.id;
    int idx = m_comboIds.indexOf(target);
    if (idx < 0 && m_providerCombo->count() > 0) idx = 0;
    if (idx >= 0) m_providerCombo->setCurrentIndex(idx);

    if (m_providerCombo->count() == 0) {
        m_statusLabel->setText(tr("未配置服务商 — 请到 设置 → AI 助手 添加"));
    }
    updateButtons();
}

AIProvider AIChatPanel::currentProvider() const
{
    const QString id = m_comboIds.value(m_providerCombo->currentIndex());
    for (const AIProvider& p : AIProviderStore::instance().providers()) {
        if (p.id == id) return p;
    }
    return AIProvider();
}

void AIChatPanel::onSendClicked()
{
    if (m_streaming) return;
    const QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    const AIProvider p = currentProvider();
    if (p.id.isEmpty()) {
        m_statusLabel->setText(tr("请先在 设置 → AI 助手 中添加并启用服务商"));
        return;
    }

    m_inputEdit->clear();
    m_history.append({ QStringLiteral("user"), text });
    m_pendingAnswer.clear();
    m_streaming = true;
    m_dirty = true;
    rebuildHtml();
    updateButtons();
    m_statusLabel->setText(tr("请求中…"));

    QList<AIChatMessage> msgs;
    msgs.append({ QStringLiteral("system"),
                  QStringLiteral("你是 SoulCove 编辑器内置的 AI 编程助手，回答简洁准确，代码用 markdown 代码块并标注语言。") });
    for (const AIChatMessage& m : m_history) msgs.append(m);
    m_client->chatStream(p, msgs);
    LOG_DEBUG("[AIChatPanel] 发送消息，服务商:" << p.name.toStdString());
}

void AIChatPanel::onStopClicked()
{
    if (m_client) m_client->cancel();
}

void AIChatPanel::onNewSessionClicked()
{
    if (m_streaming) m_client->cancel();
    m_history.clear();
    m_pendingAnswer.clear();
    rebuildHtml();
    m_statusLabel->setText(tr("已开始新会话"));
    updateButtons();
}

void AIChatPanel::onDelta(const QString& delta)
{
    m_pendingAnswer += delta;
    m_dirty = true;
    if (!m_flushTimer.isActive()) m_flushTimer.start();
}

void AIChatPanel::onFinished(bool ok, const QString& error)
{
    m_flushTimer.stop();
    m_dirty = false;
    m_streaming = false;

    if (ok) {
        if (!m_pendingAnswer.isEmpty()) {
            m_history.append({ QStringLiteral("assistant"), m_pendingAnswer });
        }
        m_pendingAnswer.clear();
        rebuildHtml();
        m_statusLabel->setText(tr("完成"));
    } else {
        m_pendingAnswer.clear();
        rebuildHtml();
        m_statusLabel->setText(tr("失败：") + error);
    }
    updateButtons();
}

void AIChatPanel::rebuildHtml()
{
    const auto& palette = ThemeManager::instance().currentPalette();
    const QString userColor = palette.accentPrimary.name();
    const QString aiColor = palette.fgPrimary.name();
    const QString errColor = palette.errorColor.name();

    QString html = QStringLiteral("<html><body style='font-size:13px;'>");
    for (const AIChatMessage& m : m_history) {
        const bool isUser = (m.role == QStringLiteral("user"));
        html += QStringLiteral(
                    "<div style='margin:8px 0 2px 0;'><b style='color:%1;'>%2</b></div>"
                    "<div style='margin:0 0 10px 0; white-space:pre-wrap;'>%3</div>")
                    .arg(isUser ? userColor : aiColor,
                         isUser ? tr("你") : tr("AI"),
                         m.content.toHtmlEscaped());
    }
    if (!m_pendingAnswer.isEmpty()) {
        html += QStringLiteral(
                    "<div style='margin:8px 0 2px 0;'><b style='color:%1;'>%2</b></div>"
                    "<div style='margin:0 0 10px 0; white-space:pre-wrap;'>%3</div>")
                    .arg(aiColor, tr("AI"), m_pendingAnswer.toHtmlEscaped());
    }
    html += QStringLiteral("</body></html>");
    m_historyView->setHtml(html);

    // 保持滚动到底部
    QScrollBar* bar = m_historyView->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void AIChatPanel::updateButtons()
{
    const bool hasProvider = m_providerCombo->count() > 0;
    const bool hasAnswer = !m_pendingAnswer.isEmpty()
        || std::any_of(m_history.cbegin(), m_history.cend(),
                       [](const AIChatMessage& m) { return m.role == QStringLiteral("assistant"); });
    m_sendBtn->setEnabled(hasProvider && !m_streaming);
    m_stopBtn->setEnabled(m_streaming);
    m_copyBtn->setEnabled(hasAnswer);
    m_insertBtn->setEnabled(hasAnswer);
}

bool AIChatPanel::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_inputEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !(ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
            onSendClicked();
            return true;  // 已消费，不插入换行
        }
    }
    return QWidget::eventFilter(obj, event);
}
