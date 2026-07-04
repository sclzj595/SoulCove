#include "ui/sidebar/TasksPanel.h"
#include "core/task/TaskManager.h"  // P3-M04 子项2: importTasksJson/exportTasksJson

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
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
#include <QTextBlock>
#include <QEvent>
#include <QFont>
#include <QFileDialog>      // P3-M04 子项2: 导入/导出文件选择
#include <QRegularExpression>
#include <QScrollBar>

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

    // === P3-M04 子项1: 输出过滤工具条 ===
    auto* filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(4);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("过滤输出..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setObjectName(QStringLiteral("tasksFilterEdit"));

    m_btnFilterErrors = new QToolButton(this);
    m_btnFilterErrors->setText(tr("⚠"));
    m_btnFilterErrors->setCheckable(true);
    m_btnFilterErrors->setToolTip(tr("仅显示错误/警告行"));
    m_btnFilterErrors->setObjectName(QStringLiteral("btnResetSection"));
    m_btnFilterErrors->setFixedSize(24, 22);

    filterLayout->addWidget(m_filterEdit, 1);
    filterLayout->addWidget(m_btnFilterErrors);
    layout->addLayout(filterLayout);

    m_taskOutputView = new QPlainTextEdit(this);
    m_taskOutputView->setReadOnly(true);
    m_taskOutputView->setObjectName(QStringLiteral("settingsHint"));
    m_taskOutputView->setFont(QFont("Consolas", 9));
    m_taskOutputView->setMaximumHeight(150);
    // P3-M04 子项1: 双击触发错误跳转
    m_taskOutputView->setMouseTracking(true);
    layout->addWidget(m_taskOutputView);

    // === 信号连接 ===
    connect(m_taskTree, &QTreeWidget::itemDoubleClicked,
            this, &TasksPanel::onItemDoubleClicked);
    connect(m_taskTree, &QTreeWidget::customContextMenuRequested,
            this, &TasksPanel::onItemContextMenu);
    connect(m_btnRun, &QPushButton::clicked, this, &TasksPanel::onRunClicked);
    connect(m_btnStopAll, &QPushButton::clicked, this, &TasksPanel::onStopAllClicked);
    connect(m_btnConfigure, &QPushButton::clicked, this, &TasksPanel::onConfigureClicked);

    // P3-M04 子项1: 过滤信号
    connect(m_filterEdit, &QLineEdit::textChanged, this, &TasksPanel::onFilterTextChanged);
    connect(m_btnFilterErrors, &QToolButton::toggled, this, &TasksPanel::onFilterErrorsToggled);
    // QPlainTextEdit 没有双击信号，使用 viewport 的事件过滤方式不可靠，
    // 改为重写为通过 QObject::connect 到 QPlainTextEdit::copyAvailable 不可行，
    // 此处直接安装事件过滤器在 viewport 上以捕获双击
    m_taskOutputView->viewport()->installEventFilter(this);

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
// P3-M04 子项1: 事件过滤器 — 捕获输出区双击
// ============================================================
bool TasksPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_taskOutputView->viewport() &&
        event->type() == QEvent::MouseButtonDblClick) {
        onOutputDoubleClicked();
        return true;  // 阻止默认行为（避免选中单词）
    }
    return QWidget::eventFilter(watched, event);
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
    QMenu menu(this);
    QString label = item ? item->data(0, Qt::UserRole).toString() : QString();

    if (item && !label.isEmpty()) {
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

        menu.addSeparator();
    }

    // === P3-M04 子项2: tasks.json 导入/导出 ===
    QAction* actImport = menu.addAction(tr("导入 tasks.json"));
    connect(actImport, &QAction::triggered, this, [this]() {
        QString startDir = m_workDir.isEmpty() ? QDir::homePath() : m_workDir;
        QString filePath = QFileDialog::getOpenFileName(
            this, tr("导入 tasks.json"), startDir,
            tr("tasks.json (tasks.json);;JSON 文件 (*.json);;所有文件 (*)"));
        if (filePath.isEmpty()) return;
        if (TaskManager::instance().importTasksJson(filePath)) {
            refreshTaskTree();
            m_outputLines.append(tr("> 已导入 tasks.json: %1").arg(filePath));
            applyOutputFilter();
        } else {
            m_outputLines.append(tr("> 导入失败: %1").arg(filePath));
            applyOutputFilter();
        }
    });

    QAction* actExport = menu.addAction(tr("导出 tasks.json"));
    connect(actExport, &QAction::triggered, this, [this]() {
        QString startDir = m_workDir.isEmpty() ? QDir::homePath() : m_workDir;
        QString filePath = QFileDialog::getSaveFileName(
            this, tr("导出 tasks.json"), startDir + QStringLiteral("/tasks.json"),
            tr("tasks.json (tasks.json);;JSON 文件 (*.json);;所有文件 (*)"));
        if (filePath.isEmpty()) return;
        if (TaskManager::instance().exportTasksJson(filePath)) {
            m_outputLines.append(tr("> 已导出 tasks.json: %1").arg(filePath));
            applyOutputFilter();
        } else {
            m_outputLines.append(tr("> 导出失败: %1").arg(filePath));
            applyOutputFilter();
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
    m_outputLines.append(tr("> 请先在上方选择一个任务"));
    applyOutputFilter();
}

void TasksPanel::onStopAllClicked()
{
    TaskManager::instance().stopAll();
    m_outputLines.append(tr("> 已停止所有正在运行的任务"));
    applyOutputFilter();
}

void TasksPanel::onConfigureClicked()
{
    // 打开或保存 tasks.json
    QString tasksPath = m_workDir + QStringLiteral("/.vscode/tasks.json");

    // 尝试加载已有配置
    if (QFile::exists(tasksPath)) {
        TaskManager::instance().loadTasksJson(tasksPath);
        refreshTaskTree();
        m_outputLines.append(tr("> 已加载: %1").arg(tasksPath));
        applyOutputFilter();
    } else {
        // 首次使用，创建默认配置
        QDir().mkpath(QFileInfo(tasksPath).absolutePath());
        TaskManager::instance().saveTasksJson(tasksPath);
        m_outputLines.append(tr("> 已创建默认 tasks.json: %1").arg(tasksPath));
        applyOutputFilter();
    }
}

void TasksPanel::onTaskStarted(const QString& label)
{
    m_outputLines.append(QStringLiteral("> ▶ %1 ...").arg(label));
    applyOutputFilter();
    refreshTaskTree();  // 更新状态图标
}

void TasksPanel::onTaskFinished(const QString& label, int exitCode, const QString& output)
{
    QString icon = (exitCode == 0) ? QString::fromUtf8("\xE2\x9C\x93") : QString::fromUtf8("\xE2\x9C\x97");
    m_outputLines.append(QStringLiteral("%1 [%2] 退出码: %3").arg(icon).arg(label).arg(exitCode));

    // 显示最后几行输出（如果有）
    QStringList lines = output.split(QLatin1Char('\n'));
    if (lines.size() > 5) {
        m_outputLines.append(tr("  (最后5行输出):"));
        for (int i = qMax(0, lines.size() - 5); i < lines.size(); ++i) {
            m_outputLines.append(QStringLiteral("  | ") + lines[i].trimmed());
        }
    }

    applyOutputFilter();
    refreshTaskTree();  // 更新状态图标
}

void TasksPanel::onTaskOutput(const QString& label, const QString& output)
{
    // 实时追加输出到历史缓冲
    QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (!line.trimmed().isEmpty()) {
            m_outputLines.append(QStringLiteral("[%1] %2").arg(label, line.trimmed()));
        }
    }
    applyOutputFilter();

    // 自动滚动到底部
    QTextCursor cursor = m_taskOutputView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_taskOutputView->setTextCursor(cursor);
}

// ============================================================
// P3-M04 子项1: 输出过滤实现
// ============================================================

void TasksPanel::onFilterTextChanged(const QString& text)
{
    Q_UNUSED(text)
    applyOutputFilter();
}

void TasksPanel::onFilterErrorsToggled(bool checked)
{
    m_filterErrorsOnly = checked;
    applyOutputFilter();
}

void TasksPanel::applyOutputFilter()
{
    if (!m_taskOutputView) return;

    QString filterText = m_filterEdit ? m_filterEdit->text() : QString();
    bool hasTextFilter = !filterText.isEmpty();
    bool caseSensitive = false;  // 大小写不敏感

    // 「仅显示错误/警告」模式：匹配包含 error/warning/错误/警告 的行（大小写不敏感）
    // 同时保留所有 "▶/✓/✗/>" 开头的状态行（保留任务开始/结束等结构性输出）
    static const QRegularExpression errWarnRe(
        QStringLiteral("(error|warning|错误|警告)"),
        QRegularExpression::CaseInsensitiveOption);

    QStringList filtered;
    filtered.reserve(m_outputLines.size());
    for (const QString& raw : m_outputLines) {
        QString line = raw;

        // 错误/警告过滤
        if (m_filterErrorsOnly) {
            bool isStatusLine = line.startsWith(QLatin1Char('>')) ||
                                line.startsWith(QStringLiteral("  |")) ||
                                line.contains(QStringLiteral("退出码"));
            if (!isStatusLine && !errWarnRe.match(line).hasMatch()) {
                continue;
            }
        }

        // 文本过滤
        if (hasTextFilter) {
            if (caseSensitive) {
                if (!line.contains(filterText)) continue;
            } else {
                if (!line.contains(filterText, Qt::CaseInsensitive)) continue;
            }
        }

        filtered.append(line);
    }

    // 重新渲染（保留滚动位置）
    int scrollVal = m_taskOutputView->verticalScrollBar()->value();
    m_taskOutputView->setPlainText(filtered.join(QLatin1Char('\n')));
    // 如果原滚动条在底部，则保持在底部
    if (scrollVal >= m_taskOutputView->verticalScrollBar()->maximum() - 4) {
        QTextCursor cursor = m_taskOutputView->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_taskOutputView->setTextCursor(cursor);
    }
}

void TasksPanel::onOutputDoubleClicked()
{
    // 双击当前光标所在行，尝试解析 file:line:col 模式
    QTextCursor cursor = m_taskOutputView->textCursor();
    QTextBlock block = cursor.block();
    if (!block.isValid()) return;
    QString lineText = block.text();
    if (lineText.isEmpty()) return;

    QString filePath;
    int lineNum = -1;
    int col = -1;
    if (parseLocationFromLine(lineText, filePath, lineNum, col)) {
        QString resolved = resolveFilePath(filePath);
        emit jumpToLocationRequested(resolved, lineNum, col);
    }
}

bool TasksPanel::parseLocationFromLine(const QString& line, QString& filePath, int& lineNum, int& col) const
{
    // 支持以下模式（取首个匹配）：
    //   1) GCC/Clang:  path:line:col: ...     (file:line:col)
    //   2) Generic:    path:line: ...          (file:line)
    //   3) MSVC:       path(line,col) ...      (file(line,col))
    //
    // 路径允许 Windows 盘符 (C:/...) 与反斜杠

    // 模式1: file:line:col  (GCC/Clang)
    static const QRegularExpression re1(
        QStringLiteral("([a-zA-Z]:[\\\\/][^:]*|[^:*?\"<>|\\s]+\\.[A-Za-z]+):"
                       "(\\d+):(\\d+)"));
    // 模式2: file:line  (无列号)
    static const QRegularExpression re2(
        QStringLiteral("([a-zA-Z]:[\\\\/][^:]*|[^:*?\"<>|\\s]+\\.[A-Za-z]+):(\\d+)(?!\\d|:)"));
    // 模式3: file(line,col)  (MSVC)
    static const QRegularExpression re3(
        QStringLiteral("([a-zA-Z]:[\\\\/][^()]*|[^:*?\"<>|\\s]+\\.[A-Za-z]+)\\((\\d+),(\\d+)\\)"));

    auto tryMatch = [&line](const QRegularExpression& re, QString& fp, int& ln, int& co,
                            bool hasCol) -> bool {
        QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch()) {
            fp = m.captured(1);
            ln = m.captured(2).toInt();
            if (hasCol) co = m.captured(3).toInt();
            return true;
        }
        return false;
    };

    if (tryMatch(re1, filePath, lineNum, col, true)) return true;
    if (tryMatch(re3, filePath, lineNum, col, true)) return true;
    if (tryMatch(re2, filePath, lineNum, col, false)) {
        col = 1;  // 默认列号 1
        return true;
    }
    return false;
}

QString TasksPanel::resolveFilePath(const QString& filePath) const
{
    if (filePath.isEmpty()) return filePath;

    QFileInfo fi(filePath);
    if (fi.isAbsolute() && fi.exists()) {
        return QDir::toNativeSeparators(fi.absoluteFilePath());
    }

    // 基于 m_workDir 解析
    if (!m_workDir.isEmpty()) {
        QString candidate = m_workDir + QStringLiteral("/") + filePath;
        if (QFile::exists(candidate)) {
            return QDir::toNativeSeparators(QFileInfo(candidate).absoluteFilePath());
        }
    }

    // 基于 cwd 解析
    if (QFile::exists(filePath)) {
        return QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    }

    // 解析失败：返回原路径（Widget 仍可尝试打开）
    return filePath;
}
