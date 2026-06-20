#include "ui/sidebar/TasksPanel.h"
#include "core/task/TaskManager.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextCursor>
#include <QFont>

TasksPanel::TasksPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 6, 2, 2);
    layout->setSpacing(2);

    auto* title = new QLabel(tr("任务"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);

    // 任务树（分组显示）
    m_taskTree = new QTreeWidget(this);
    m_taskTree->setObjectName(QStringLiteral("sideFileTree"));
    m_taskTree->setHeaderHidden(true);
    m_taskTree->setAnimated(true);
    m_taskTree->setIndentation(12);
    m_taskTree->setRootIsDecorated(true);
    m_taskTree->setColumnCount(1);
    m_taskTree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_taskTree, 1);

    // 工具栏按钮
    auto* btnLayout = new QHBoxLayout();
    m_btnRun = new QPushButton(tr("▶ 运行"), this);
    m_btnRun->setFixedSize(70, 24);
    m_btnRun->setObjectName(QStringLiteral("btnResetSection"));
    m_btnStopAll = new QPushButton(tr("⏹ 停止全部"), this);
    m_btnStopAll->setFixedSize(80, 24);
    m_btnStopAll->setObjectName(QStringLiteral("btnResetSection"));
    m_btnConfigure = new QPushButton(tr("⚙ 配置"), this);
    m_btnConfigure->setFixedSize(70, 24);
    m_btnConfigure->setObjectName(QStringLiteral("btnResetSection"));
    btnLayout->addWidget(m_btnRun);
    btnLayout->addWidget(m_btnStopAll);
    btnLayout->addWidget(m_btnConfigure);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    // 输出区域
    auto* outputLabel = new QLabel(tr("输出"), this);
    outputLabel->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(outputLabel);

    m_taskOutputView = new QPlainTextEdit(this);
    m_taskOutputView->setReadOnly(true);
    m_taskOutputView->setObjectName(QStringLiteral("settingsHint"));
    m_taskOutputView->setFont(QFont("Consolas", 9));
    m_taskOutputView->setMaximumHeight(150);
    layout->addWidget(m_taskOutputView);

    // === 信号连接 ===
    connect(m_taskTree, &QTreeWidget::itemDoubleClicked,
            this, &TasksPanel::onItemDoubleClicked);
    connect(m_taskTree, &QTreeWidget::customContextMenuRequested,
            this, &TasksPanel::onItemContextMenu);
    connect(m_btnRun, &QPushButton::clicked, this, &TasksPanel::onRunClicked);
    connect(m_btnStopAll, &QPushButton::clicked, this, &TasksPanel::onStopAllClicked);
    connect(m_btnConfigure, &QPushButton::clicked, this, &TasksPanel::onConfigureClicked);

    // 连接 TaskManager 单例信号
    auto& tm = TaskManager::instance();
    connect(&tm, &TaskManager::taskStarted, this, &TasksPanel::onTaskStarted);
    connect(&tm, &TaskManager::taskFinished, this, &TasksPanel::onTaskFinished);
    connect(&tm, &TaskManager::taskOutput, this, &TasksPanel::onTaskOutput);

    // 初始化任务树
    refreshTaskTree();
}

void TasksPanel::setWorkDirectory(const QString& dirPath)
{
    m_workDir = dirPath;
}

// ============================================================
// 任务树刷新
// ============================================================

void TasksPanel::refreshTaskTree()
{
    m_taskTree->clear();

    auto& tm = TaskManager::instance();
    QStringList groups = {QStringLiteral("build"), QStringLiteral("run"),
                         QStringLiteral("test"), QStringLiteral("lint"), QStringLiteral("format")};

    // 分组图标映射
    QMap<QString, QString> groupIcons = {
        {QStringLiteral("build"),  QString::fromUtf8("\xF0\x9F\x94\xA8")},   // 🚨
        {QStringLiteral("run"),    QString::fromUtf8("\xE2\x96\xBA")},       // ▶
        {QStringLiteral("test"),   QString::fromUtf8("\xE2\x9C\x94")},       // ✔
        {QStringLiteral("lint"),   QString::fromUtf8("\xF0\x9F\x94\x8D")},   // 🔍
        {QStringLiteral("format"), QString::fromUtf8("\xE2\x9C\xA8")}        // ✨
    };

    for (const QString& grp : groups) {
        QList<TaskItem> tasks = tm.tasksByGroup(grp);
        if (tasks.isEmpty()) continue;

        // 创建分组节点
        auto* groupItem = new QTreeWidgetItem(m_taskTree);
        groupItem->setText(0, groupIcons.value(grp) + QStringLiteral(" ") + (grp == QStringLiteral("build") ? tr("构建")
                              : grp == QStringLiteral("run")    ? tr("运行")
                              : grp == QStringLiteral("test")   ? tr("测试")
                              : grp == QStringLiteral("lint")   ? tr("检查")
                              : tr("格式化")));
        groupItem->setExpanded(true);

        for (const TaskItem& t : tasks) {
            auto* taskItem = new QTreeWidgetItem(groupItem);

            // 状态前缀：运行中/成功/失败
            QString prefix;
            if (tm.isTaskRunning(t.label)) {
                prefix = QString::fromUtf8("\xE2\x8F\xB0");  // ⟳ 运行中
            } else if (t.exitCode == 0 && t.lastRun.isValid()) {
                prefix = QString::fromUtf8("\xE2\x9C\x93");      // ✓ 成功
            } else if (t.exitCode != 0 && t.lastRun.isValid()) {
                prefix = QString::fromUtf8("\xE2\x9C\x97");      // ✗ 失败
            } else {
                prefix = QStringLiteral("  ");
            }

            taskItem->setText(0, prefix + QStringLiteral(" ") + t.label);
            taskItem->setData(0, Qt::UserRole, t.label);
            taskItem->setData(0, Qt::UserRole + 1, grp);
        }
    }
}

// ============================================================
// 槽函数
// ============================================================

void TasksPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;

    QString label = item->data(0, Qt::UserRole).toString();
    if (!label.isEmpty()) {
        TaskManager::instance().runTask(label);
    }
}

void TasksPanel::onItemContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_taskTree->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QString label = item->data(0, Qt::UserRole).toString();

    QAction* actRun = menu.addAction(tr("▶ 运行任务"));
    connect(actRun, &QAction::triggered, this, [this, label]() {
        if (!label.isEmpty()) TaskManager::instance().runTask(label);
    });

    menu.addSeparator();

    QAction* actCopyCmd = menu.addAction(tr("复制命令"));
    connect(actCopyCmd, &QAction::triggered, this, [this, label]() {
        if (!label.isEmpty()) {
            TaskItem t = TaskManager::instance().task(label);
            QApplication::clipboard()->setText(t.command + QStringLiteral(" ") + t.args.join(QLatin1Char(' ')));
        }
    });

    menu.exec(m_taskTree->mapToGlobal(pos));
}

void TasksPanel::onRunClicked()
{
    // 运行当前选中的任务
    QTreeWidgetItem* item = m_taskTree->currentItem();
    if (item) {
        QString label = item->data(0, Qt::UserRole).toString();
        if (!label.isEmpty()) {
            TaskManager::instance().runTask(label);
            return;
        }
    }
    // 如果没有选中，提示
    m_taskOutputView->appendPlainText(tr("> 请先在上方选择一个任务"));
}

void TasksPanel::onStopAllClicked()
{
    TaskManager::instance().stopAll();
    m_taskOutputView->appendPlainText(tr("> 已停止所有正在运行的任务"));
}

void TasksPanel::onConfigureClicked()
{
    // 打开或保存 tasks.json
    QString tasksPath = m_workDir + QStringLiteral("/.vscode/tasks.json");

    // 尝试加载已有配置
    if (QFile::exists(tasksPath)) {
        TaskManager::instance().loadTasksJson(tasksPath);
        refreshTaskTree();
        m_taskOutputView->appendPlainText(tr("> 已加载: %1").arg(tasksPath));
    } else {
        // 首次使用，创建默认配置
        QDir().mkpath(QFileInfo(tasksPath).absolutePath());
        TaskManager::instance().saveTasksJson(tasksPath);
        m_taskOutputView->appendPlainText(tr("> 已创建默认 tasks.json: %1").arg(tasksPath));
    }
}

void TasksPanel::onTaskStarted(const QString& label)
{
    m_taskOutputView->appendPlainText(QStringLiteral("> ▶ %1 ...").arg(label));
    refreshTaskTree();  // 更新状态图标
}

void TasksPanel::onTaskFinished(const QString& label, int exitCode, const QString& output)
{
    QString icon = (exitCode == 0) ? QString::fromUtf8("\xE2\x9C\x93") : QString::fromUtf8("\xE2\x9C\x97");
    m_taskOutputView->appendPlainText(QStringLiteral("%1 [%2] 退出码: %3").arg(icon).arg(label).arg(exitCode));

    // 显示最后几行输出（如果有）
    QStringList lines = output.split(QLatin1Char('\n'));
    if (lines.size() > 5) {
        m_taskOutputView->appendPlainText(tr("  (最后5行输出):"));
        for (int i = qMax(0, lines.size() - 5); i < lines.size(); ++i) {
            m_taskOutputView->appendPlainText(QStringLiteral("  | ") + lines[i].trimmed());
        }
    }

    refreshTaskTree();  // 更新状态图标
}

void TasksPanel::onTaskOutput(const QString& label, const QString& output)
{
    // 实时追加输出到面板
    QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (!line.trimmed().isEmpty()) {
            m_taskOutputView->appendPlainText(QStringLiteral("[%1] %2").arg(label, line.trimmed()));
        }
    }

    // 自动滚动到底部
    QTextCursor cursor = m_taskOutputView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_taskOutputView->setTextCursor(cursor);
}
