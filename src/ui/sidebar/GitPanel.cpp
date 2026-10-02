#include "ui/sidebar/GitPanel.h"
#include "ui/sidebar/GitHistoryPanel.h"
#include "core/vcs/GitManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileInfo>
#include "ui/dialog/ModernDialog.h"
#include <QDebug>

GitPanel::GitPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    // 连接 GitManager 信号
    connect(&GitManager::instance(), &GitManager::repoChanged,
            this, &GitPanel::onRepoChanged);
    connect(&GitManager::instance(), &GitManager::operationFinished,
            this, &GitPanel::onOperationFinished);

    // P2-H03 子项2: 转发历史面板的 diff 请求信号
    if (m_historyPanel) {
        connect(m_historyPanel, &GitHistoryPanel::commitDiffRequested,
                this, &GitPanel::commitDiffRequested);
    }
}

void GitPanel::setWorkspaceRoot(const QString& root)
{
    if (m_historyPanel) {
        m_historyPanel->setWorkspaceRoot(root);
    }
}

void GitPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === P2-H03 子项2: 顶层 QTabWidget ===
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("gitPanelTabs"));
    m_tabWidget->setDocumentMode(true);
    mainLayout->addWidget(m_tabWidget);

    // ===== 「更改」tab：原有 GitPanel 内容 =====
    auto* changesTab = new QWidget(m_tabWidget);
    auto* changesLayout = new QVBoxLayout(changesTab);
    changesLayout->setContentsMargins(6, 4, 4, 4);
    changesLayout->setSpacing(4);

    // === 顶部：当前分支 + 同步按钮 ===
    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(4);

    m_branchLabel = new QLabel(tr("分支:"), changesTab);
    m_branchCombo = new QComboBox(changesTab);
    m_branchCombo->setObjectName(QStringLiteral("gitBranchCombo"));
    m_branchCombo->setMinimumWidth(120);
    m_branchCombo->setToolTip(tr("切换分支"));

    m_btnPull = new QPushButton(QString::fromUtf8("\xE2\x86\x93"), changesTab);  // ↓
    m_btnPull->setFixedSize(28, 24);
    m_btnPull->setToolTip(tr("拉取 (Pull)"));
    m_btnPull->setProperty("iconButton", true);

    m_btnPush = new QPushButton(QString::fromUtf8("\xE2\x86\x91"), changesTab);  // ↑
    m_btnPush->setFixedSize(28, 24);
    m_btnPush->setToolTip(tr("推送 (Push)"));
    m_btnPush->setProperty("iconButton", true);

    m_btnRefresh = new QPushButton(QString::fromUtf8("\u21BB"), changesTab);  // ↻
    m_btnRefresh->setFixedSize(28, 24);
    m_btnRefresh->setToolTip(tr("刷新"));
    m_btnRefresh->setProperty("iconButton", true);

    topLayout->addWidget(m_branchLabel);
    topLayout->addWidget(m_branchCombo, 1);
    topLayout->addWidget(m_btnPull);
    topLayout->addWidget(m_btnPush);
    topLayout->addWidget(m_btnRefresh);

    changesLayout->addLayout(topLayout);

    // === 文件状态树 ===
    m_fileTree = new QTreeWidget(changesTab);
    m_fileTree->setObjectName(QStringLiteral("gitFileTree"));
    m_fileTree->setHeaderHidden(true);
    m_fileTree->setAnimated(true);
    m_fileTree->setIndentation(12);
    m_fileTree->setRootIsDecorated(false);
    m_fileTree->setColumnCount(2);
    m_fileTree->header()->setStretchLastSection(false);
    m_fileTree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_fileTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fileTree->setColumnWidth(0, 100);
    changesLayout->addWidget(m_fileTree, 1);

    // === 操作按钮行 ===
    auto* btnLayout1 = new QHBoxLayout();
    btnLayout1->setSpacing(4);

    m_btnCommit = new QPushButton(tr("√ 提交"), changesTab);
    m_btnCommit->setObjectName(QStringLiteral("gitActionBtn"));

    m_btnDiscard = new QPushButton(tr("⟲ 放弃更改"), changesTab);
    m_btnDiscard->setObjectName(QStringLiteral("gitActionBtn"));

    m_btnStage = new QPushButton(tr("≡ 暂存更改"), changesTab);
    m_btnStage->setObjectName(QStringLiteral("gitActionBtn"));

    btnLayout1->addWidget(m_btnCommit);
    btnLayout1->addWidget(m_btnDiscard);
    btnLayout1->addWidget(m_btnStage);
    btnLayout1->addStretch();

    changesLayout->addLayout(btnLayout1);

    // === 提交消息输入框 ===
    auto* commitLayout = new QHBoxLayout();
    commitLayout->setSpacing(4);

    auto* commitLabel = new QLabel(tr("提交消息:"), changesTab);
    m_commitMsgEdit = new QLineEdit(changesTab);
    m_commitMsgEdit->setPlaceholderText(tr("输入提交描述..."));
    m_commitMsgEdit->setObjectName(QStringLiteral("gitCommitEdit"));

    commitLayout->addWidget(commitLabel);
    commitLayout->addWidget(m_commitMsgEdit, 1);
    changesLayout->addLayout(commitLayout);

    // === 状态标签 ===
    m_statusLabel = new QLabel(changesTab);
    m_statusLabel->setObjectName(QStringLiteral("gitStatusLabel"));
    m_statusLabel->setWordWrap(true);
    changesLayout->addWidget(m_statusLabel);

    m_tabWidget->addTab(changesTab, tr("更改"));

    // ===== 「历史」tab：GitHistoryPanel =====
    m_historyPanel = new GitHistoryPanel(m_tabWidget);
    m_tabWidget->addTab(m_historyPanel, tr("历史"));

    // === 信号连接 ===
    connect(m_btnRefresh, &QPushButton::clicked, this, &GitPanel::onRefreshClicked);
    connect(m_branchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GitPanel::onBranchChanged);
    connect(m_fileTree, &QTreeWidget::itemDoubleClicked,
            this, &GitPanel::onFileItemDoubleClicked);
    connect(m_btnCommit, &QPushButton::clicked, this, &GitPanel::onCommitClicked);
    connect(m_btnPull, &QPushButton::clicked, [this]() {
        GitManager::instance().pull();
    });
    connect(m_btnPush, &QPushButton::clicked, [this]() {
        GitManager::instance().push();
    });
    connect(m_btnDiscard, &QPushButton::clicked, this, &GitPanel::onDiscardClicked);
    connect(m_btnStage, &QPushButton::clicked, this, &GitPanel::onStageClicked);

    // 回车键提交
    connect(m_commitMsgEdit, &QLineEdit::returnPressed, this, &GitPanel::onCommitClicked);
}

void GitPanel::refresh()
{
    auto& git = GitManager::instance();

    if (!git.isGitRepo()) {
        m_fileTree->clear();
        m_branchCombo->clear();
        m_statusLabel->setText(tr("⚠ 当前目录不是 Git 仓库"));
        return;
    }

    // 更新分支列表
    QString currentBranch = git.currentBranch();
    QStringList allBranches = git.branches();
    m_branchCombo->clear();
    m_branchCombo->addItems(allBranches);
    int idx = m_branchCombo->findText(currentBranch);
    if (idx >= 0) {
        m_branchCombo->setCurrentIndex(idx);
    }

    // 更新文件状态树
    m_fileTree->clear();
    QList<GitFileStatus> statuses = git.fileStatuses();

    if (statuses.isEmpty()) {
        m_statusLabel->setText(tr("✅ 工作区干净，没有更改"));
        return;
    }

    for (const auto& fs : statuses) {
        createFileItem(fs.filePath, fs.status);
    }

    m_statusLabel->setText(tr("共 %1 个文件有更改").arg(statuses.size()));
}

QString GitPanel::statusText(GitFileStatus::Status status) const
{
    switch (status) {
    case GitFileStatus::Modified:  return tr("Modified");
    case GitFileStatus::Added:     return tr("Added");
    case GitFileStatus::Deleted:   return tr("Deleted");
    case GitFileStatus::Renamed:   return tr("Renamed");
    case GitFileStatus::Untracked: return tr("Untracked");
    default:                                return QString();
    }
}

QString GitPanel::statusIcon(GitFileStatus::Status status) const
{
    switch (status) {
    case GitFileStatus::Modified:  return QStringLiteral(" M ");
    case GitFileStatus::Added:     return QStringLiteral(" A ");
    case GitFileStatus::Deleted:   return QStringLiteral(" D ");
    case GitFileStatus::Renamed:   return QStringLiteral(" R ");
    case GitFileStatus::Untracked: return QStringLiteral("?? ");
    default:                                return QStringLiteral("   ");
    }
}

QTreeWidgetItem* GitPanel::createFileItem(const QString& filePath,
                                           GitFileStatus::Status status)
{
    auto* item = new QTreeWidgetItem(m_fileTree);
    item->setText(0, statusIcon(status) + statusText(status));
    item->setText(1, filePath);
    item->setData(0, Qt::UserRole, filePath);
    item->setData(1, Qt::UserRole, static_cast<int>(status));

    // 根据状态设置颜色
    QColor color;
    switch (status) {
    case GitFileStatus::Modified:  color = QColor(220, 180, 50);  break;  // 黄色
    case GitFileStatus::Added:     color = QColor(80, 180, 80);    break;  // 绿色
    case GitFileStatus::Deleted:   color = QColor(200, 80, 80);    break;  // 红色
    case GitFileStatus::Renamed:   color = QColor(80, 150, 220);   break;  // 蓝色
    case GitFileStatus::Untracked: color = QColor(160, 140, 120);  break;  // 灰色
    default: break;
    }
    item->setForeground(0, color);
    return item;
}

// ========== 槽函数 ==========

void GitPanel::onRefreshClicked()
{
    refresh();
}

void GitPanel::onPullClicked()
{
    GitManager::instance().pull();
}

void GitPanel::onPushClicked()
{
    GitManager::instance().push();
}

void GitPanel::onBranchChanged(int index)
{
    if (index < 0) return;
    QString branch = m_branchCombo->itemText(index);
    if (branch.isEmpty()) return;

    // 确认是否切换
    auto& git = GitManager::instance();
    if (git.currentBranch() == branch) return;

    int result = ModernDialog::question(this, tr("切换分支"),
        tr("确定要切换到分支 \"%1\" 吗？\n未保存的更改可能会丢失。").arg(branch));

    if (result == ModernDialog::ROLE_ACCEPT) {
        git.checkoutBranch(branch);
        refresh();
    } else {
        // 恢复选择到当前分支
        int currentIdx = m_branchCombo->findText(git.currentBranch());
        if (currentIdx >= 0) {
            m_branchCombo->blockSignals(true);
            m_branchCombo->setCurrentIndex(currentIdx);
            m_branchCombo->blockSignals(false);
        }
    }
}

void GitPanel::onFileItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;

    QString filePath = item->data(0, Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    // 发射信号通知外部打开 DiffViewer（通过 Widget 中转）
    emit fileDiffRequested(filePath);
}

void GitPanel::onCommitClicked()
{
    QString msg = m_commitMsgEdit->text().trimmed();
    if (msg.isEmpty()) {
        m_statusLabel->setText(tr("⚠ 请输入提交消息"));
        return;
    }

    auto& git = GitManager::instance();
    if (git.commit(msg)) {
        m_commitMsgEdit->clear();
        refresh();
    }
}

void GitPanel::onDiscardClicked()
{
    auto* item = m_fileTree->currentItem();
    if (!item) {
        m_statusLabel->setText(tr("⚠ 请先选择一个文件"));
        return;
    }

    QString filePath = item->data(0, Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    int result = ModernDialog::warning(this, tr("放弃更改"),
        tr("确定要放弃对 \"%1\" 的所有更改吗？此操作不可撤销。")
            .arg(QFileInfo(filePath).fileName()));

    if (result == ModernDialog::ROLE_DESTRUCTIVE) {
        GitManager::instance().discardChanges(filePath);
        refresh();
    }
}

void GitPanel::onStageClicked()
{
    auto* item = m_fileTree->currentItem();
    if (!item) {
        m_statusLabel->setText(tr("⚠ 请先选择一个文件"));
        return;
    }

    QString filePath = item->data(0, Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    int status = item->data(1, Qt::UserRole).toInt();
    auto& git = GitManager::instance();

    if (static_cast<GitFileStatus::Status>(status) == GitFileStatus::Untracked ||
        static_cast<GitFileStatus::Status>(status) == GitFileStatus::Modified) {
        git.stageFile(filePath);
    } else {
        git.unstageFile(filePath);
    }
    refresh();
}

void GitPanel::onRepoChanged()
{
    refresh();
    // P2-H03 子项2: 同步刷新历史面板（提交/推送/拉取后历史会变化）
    if (m_historyPanel) m_historyPanel->refresh();
}

void GitPanel::onOperationFinished(const QString& op, bool success, const QString& output)
{
    Q_UNUSED(op)
    if (success) {
        m_statusLabel->setText(tr("✅ ") + output.left(100));
    } else {
        m_statusLabel->setText(tr("❌ ") + output.left(200));
    }
}
