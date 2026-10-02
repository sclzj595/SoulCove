#include "ui/debug/DebugView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QTableWidget>
#include <QListWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QSplitter>
#include <QVariantMap>
#include <QFileInfo>
#include <QScrollBar>

// ============================================================
// P3-M04 子项3: DebugView 实现
// ============================================================

DebugView::DebugView(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === 顶部工具栏 ===
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("debugToolbar"));
    toolbar->setFixedHeight(32);
    auto* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(8, 0, 8, 0);
    tbLayout->setSpacing(4);

    auto makeBtn = [this](const QString& text, const QString& tooltip) -> QToolButton* {
        auto* btn = new QToolButton(this);
        btn->setText(text);
        btn->setToolTip(tooltip);
        btn->setObjectName(QStringLiteral("btnResetSection"));
        btn->setFixedSize(56, 24);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    m_btnStartContinue = makeBtn(tr("▶ 开始"), tr("开始调试 (F5)"));
    m_btnStepOver      = makeBtn(tr("⏭ 跳过"), tr("单步跳过 (F10)"));
    m_btnStepInto      = makeBtn(tr("⏵ 进入"), tr("单步进入 (F11)"));
    m_btnStepOut       = makeBtn(tr("⏴ 跳出"), tr("单步跳出 (Shift+F11)"));
    m_btnStop          = makeBtn(tr("⏹ 停止"), tr("停止调试 (Shift+F5)"));

    m_stateLabel = new QLabel(tr("未启动"), this);
    m_stateLabel->setObjectName(QStringLiteral("debugStateLabel"));

    tbLayout->addWidget(m_btnStartContinue);
    tbLayout->addWidget(m_btnStepOver);
    tbLayout->addWidget(m_btnStepInto);
    tbLayout->addWidget(m_btnStepOut);
    tbLayout->addWidget(m_btnStop);
    tbLayout->addSpacing(12);
    tbLayout->addWidget(m_stateLabel);
    tbLayout->addStretch();

    mainLayout->addWidget(toolbar);

    // === 中间 / 底部使用 QSplitter 分割 ===
    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName(QStringLiteral("debugSplitter"));

    // --- 变量表 ---
    m_varsTable = new QTableWidget(0, 3, splitter);
    m_varsTable->setObjectName(QStringLiteral("debugVarsTable"));
    m_varsTable->setHorizontalHeaderLabels({tr("变量名"), tr("值"), tr("类型")});
    m_varsTable->horizontalHeader()->setStretchLastSection(true);
    m_varsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_varsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_varsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_varsTable->verticalHeader()->setVisible(false);
    m_varsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_varsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    splitter->addWidget(m_varsTable);

    // --- 调用栈列表 ---
    m_callStackList = new QListWidget(splitter);
    m_callStackList->setObjectName(QStringLiteral("debugCallStackList"));
    m_callStackList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 标签占位（用首项做标题，不可选）
    auto* callStackLabel = new QListWidgetItem(tr("调用栈"), m_callStackList);
    callStackLabel->setFlags(Qt::NoItemFlags);
    QFont boldFont = m_callStackList->font();
    boldFont.setBold(true);
    callStackLabel->setFont(boldFont);
    splitter->addWidget(m_callStackList);

    // --- 断点列表 ---
    m_breakpointList = new QListWidget(splitter);
    m_breakpointList->setObjectName(QStringLiteral("debugBreakpointList"));
    m_breakpointList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* bpLabel = new QListWidgetItem(tr("断点"), m_breakpointList);
    bpLabel->setFlags(Qt::NoItemFlags);
    bpLabel->setFont(boldFont);
    splitter->addWidget(m_breakpointList);

    // 默认分割比例：变量表(3) : 调用栈(2) : 断点(2)
    splitter->setSizes({120, 80, 80});
    mainLayout->addWidget(splitter, 1);

    // === 信号连接 ===
    connect(m_btnStartContinue, &QToolButton::clicked,
            this, &DebugView::onStartOrContinueClicked);
    connect(m_btnStepOver, &QToolButton::clicked,
            this, &DebugView::onStepOverClicked);
    connect(m_btnStepInto, &QToolButton::clicked,
            this, &DebugView::onStepIntoClicked);
    connect(m_btnStepOut, &QToolButton::clicked,
            this, &DebugView::onStepOutClicked);
    connect(m_btnStop, &QToolButton::clicked,
            this, &DebugView::onStopClicked);

    updateButtonStates();
}

void DebugView::setDebugManager(DebugManager* manager)
{
    if (m_debugManager == manager) return;

    // 断开旧连接
    if (m_debugManager) {
        disconnect(m_debugManager, nullptr, this, nullptr);
    }
    m_debugManager = manager;

    if (m_debugManager) {
        connect(m_debugManager, &DebugManager::stateChanged,
                this, &DebugView::onStateChanged);
        connect(m_debugManager, &DebugManager::breakpointHit,
                this, &DebugView::onBreakpointHit);
        connect(m_debugManager, &DebugManager::outputMessage,
                this, &DebugView::onOutputMessage);
        connect(m_debugManager, &DebugManager::variablesReady,
                this, &DebugView::onVariablesReady);

        // 同步初始状态
        m_state = m_debugManager->state();
        updateButtonStates();
    }
}

// ============================================================
// 按钮状态控制
// ============================================================

void DebugView::updateButtonStates()
{
    bool active = (m_state != DebugState::Stopped);
    bool paused = (m_state == DebugState::Paused);

    // 开始 / 继续 按钮：未启动时显示「开始」，暂停时显示「继续」
    m_btnStartContinue->setEnabled(!active || paused);
    m_btnStartContinue->setText(m_state == DebugState::Stopped ? tr("▶ 开始")
                                    : tr("▶ 继续"));

    // 单步按钮：仅在 Paused 时可用
    m_btnStepOver->setEnabled(paused);
    m_btnStepInto->setEnabled(paused);
    m_btnStepOut->setEnabled(paused);

    // 停止按钮：仅在活跃会话时可用
    m_btnStop->setEnabled(active);

    // 状态文字
    QString stateText;
    switch (m_state) {
    case DebugState::Stopped: stateText = tr("未启动");  break;
    case DebugState::Running: stateText = tr("运行中");  break;
    case DebugState::Paused:  stateText = tr("已暂停");  break;
    }
    m_stateLabel->setText(stateText);
}

// ============================================================
// 工具栏按钮槽
// ============================================================

void DebugView::onStartOrContinueClicked()
{
    if (!m_debugManager) return;

    if (m_state == DebugState::Paused) {
        // 继续
        m_debugManager->continueExecution();
    } else if (m_state == DebugState::Stopped) {
        // 开始调试（由 Widget 决定调试目标程序）
        emit startDebugRequested();
    }
}

void DebugView::onStepOverClicked()
{
    if (m_debugManager && m_state == DebugState::Paused) {
        m_debugManager->stepOver();
    }
}

void DebugView::onStepIntoClicked()
{
    if (m_debugManager && m_state == DebugState::Paused) {
        m_debugManager->stepInto();
    }
}

void DebugView::onStepOutClicked()
{
    if (m_debugManager && m_state == DebugState::Paused) {
        m_debugManager->stepOut();
    }
}

void DebugView::onStopClicked()
{
    if (m_debugManager) {
        m_debugManager->stopDebug();
    }
}

// ============================================================
// DebugManager 信号槽
// ============================================================

void DebugView::onStateChanged(DebugState state)
{
    m_state = state;
    updateButtonStates();
}

void DebugView::onBreakpointHit(const QString& file, int line)
{
    // 添加到调用栈顶部（最新停止位置）
    QString item = tr("→ %1:%2").arg(QFileInfo(file).fileName()).arg(line);
    // 移除标题项后插入再恢复
    QListWidgetItem* titleItem = m_callStackList->takeItem(0);
    m_callStackList->insertItem(0, item);
    if (titleItem) {
        m_callStackList->insertItem(0, titleItem);
    }
    // 限制调用栈最大长度 50
    while (m_callStackList->count() > 51) {
        delete m_callStackList->takeItem(m_callStackList->count() - 1);
    }

    // 滚动到顶部
    m_callStackList->scrollToTop();

    // 通知 Widget 跳转
    emit jumpToLocationRequested(file, line);
}

void DebugView::onOutputMessage(const QString& msg)
{
    // 将输出作为调用栈列表的日志项（简化处理）
    Q_UNUSED(msg)
    // 实际项目可改为单独的调试控制台；此处仅追加到调用栈列表底部（不影响主流程）
    // 此处注释以避免视觉混乱；输出由 Widget 状态栏/任务输出区路由处理
}

void DebugView::onVariablesReady(const QVariantList& vars)
{
    m_varsTable->setRowCount(0);
    for (const QVariant& v : vars) {
        QVariantMap vm = v.toMap();
        int row = m_varsTable->rowCount();
        m_varsTable->insertRow(row);

        m_varsTable->setItem(row, 0, new QTableWidgetItem(vm.value(QStringLiteral("name")).toString()));
        m_varsTable->setItem(row, 1, new QTableWidgetItem(vm.value(QStringLiteral("value")).toString()));
        m_varsTable->setItem(row, 2, new QTableWidgetItem(vm.value(QStringLiteral("type")).toString()));
    }
}

// ============================================================
// 断点列表 UI 维护
// ============================================================

void DebugView::addBreakpointToList(const QString& file, int line)
{
    QString text = tr("%1 : %2").arg(QFileInfo(file).fileName()).arg(line);
    // 去重（避免重复添加）
    for (int i = 1; i < m_breakpointList->count(); ++i) {
        if (m_breakpointList->item(i)->text() == text) return;
    }
    auto* item = new QListWidgetItem(text, m_breakpointList);
    item->setData(Qt::UserRole, file);
    item->setData(Qt::UserRole + 1, line);
}

void DebugView::removeBreakpointFromList(const QString& file, int line)
{
    QString text = tr("%1 : %2").arg(QFileInfo(file).fileName()).arg(line);
    for (int i = 1; i < m_breakpointList->count(); ++i) {
        if (m_breakpointList->item(i)->text() == text) {
            delete m_breakpointList->takeItem(i);
            return;
        }
    }
}

void DebugView::clearBreakpointList()
{
    // 保留首项（标题）
    while (m_breakpointList->count() > 1) {
        delete m_breakpointList->takeItem(m_breakpointList->count() - 1);
    }
}
