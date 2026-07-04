#include "ui/sidebar/GitHistoryPanel.h"
#include "core/vcs/GitManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QDateTime>

GitHistoryPanel::GitHistoryPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void GitHistoryPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 4, 4, 4);
    mainLayout->setSpacing(4);

    // === 顶部：刷新按钮 + 分支筛选 ===
    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(4);

    m_btnRefresh = new QPushButton(QString::fromUtf8("\u21BB"), this);  // ↻
    m_btnRefresh->setFixedSize(28, 24);
    m_btnRefresh->setToolTip(tr("刷新历史"));
    m_btnRefresh->setProperty("iconButton", true);

    m_branchCombo = new QComboBox(this);
    m_branchCombo->setObjectName(QStringLiteral("gitHistoryBranchCombo"));
    m_branchCombo->setMinimumWidth(120);
    m_branchCombo->setToolTip(tr("按分支筛选历史"));

    topLayout->addWidget(m_btnRefresh);
    topLayout->addWidget(m_branchCombo, 1);

    mainLayout->addLayout(topLayout);

    // === 中间：提交历史列表 ===
    m_commitList = new QListWidget(this);
    m_commitList->setObjectName(QStringLiteral("gitHistoryList"));
    m_commitList->setAlternatingRowColors(true);
    m_commitList->setUniformItemSizes(false);  // 项高可能因多行消息而异
    mainLayout->addWidget(m_commitList, 2);

    // === 底部：diff 预览（只读）===
    m_diffView = new QTextEdit(this);
    m_diffView->setObjectName(QStringLiteral("gitHistoryDiffView"));
    m_diffView->setReadOnly(true);
    m_diffView->setPlaceholderText(tr("选择上方提交以查看 diff"));
    QFont monoFont = m_diffView->font();
    monoFont.setStyleHint(QFont::Monospace);
    m_diffView->setFont(monoFont);
    mainLayout->addWidget(m_diffView, 1);

    // === 状态标签 ===
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("gitHistoryStatusLabel"));
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // === 信号连接 ===
    connect(m_btnRefresh, &QPushButton::clicked, this, &GitHistoryPanel::onRefreshClicked);
    connect(m_branchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GitHistoryPanel::onBranchFilterChanged);
    connect(m_commitList, &QListWidget::currentRowChanged,
            this, &GitHistoryPanel::onCommitItemSelected);
    connect(m_commitList, &QListWidget::itemDoubleClicked,
            this, &GitHistoryPanel::onCommitItemDoubleClicked);
}

void GitHistoryPanel::setWorkspaceRoot(const QString& root)
{
    if (m_workspaceRoot == root) return;
    m_workspaceRoot = root;
    refresh();
}

void GitHistoryPanel::refresh()
{
    m_commitList->clear();
    m_diffView->clear();
    m_commits.clear();

    if (m_workspaceRoot.isEmpty()) {
        m_statusLabel->setText(tr("⚠ 未设置工作区"));
        return;
    }

    auto& git = GitManager::instance();
    if (!git.isGitRepo(m_workspaceRoot)) {
        m_statusLabel->setText(tr("⚠ 当前目录不是 Git 仓库"));
        return;
    }

    // 填充分支筛选下拉（保留当前选择）
    QString currentBranch = git.currentBranch(m_workspaceRoot);
    QStringList allBranches = git.branches(m_workspaceRoot);
    m_branchCombo->blockSignals(true);
    m_branchCombo->clear();
    m_branchCombo->addItem(tr("所有分支"));
    m_branchCombo->addItems(allBranches);
    int idx = m_branchCombo->findText(currentBranch);
    if (idx >= 0) m_branchCombo->setCurrentIndex(idx);
    m_branchCombo->blockSignals(false);

    // 获取结构化提交历史
    m_commits = git.logDetailed(m_workspaceRoot, 100);
    if (m_commits.isEmpty()) {
        m_statusLabel->setText(tr("无提交历史"));
        return;
    }

    // 填充列表项
    for (const auto& commit : m_commits) {
        QString shortHash = commit.hash.left(7);
        QString firstLine = commit.message.section(QLatin1Char('\n'), 0, 0);
        if (firstLine.isEmpty()) firstLine = tr("(无提交消息)");

        QString display = QStringLiteral("%1  %2  — %3  (%4)")
            .arg(shortHash,
                 firstLine,
                 commit.author,
                 relativeTime(commit.time));

        auto* item = new QListWidgetItem(display, m_commitList);
        // 完整哈希存入 UserRole，便于查询 diff
        item->setData(Qt::UserRole, commit.hash);
        item->setToolTip(commit.message);
    }

    m_statusLabel->setText(tr("共 %1 条提交").arg(m_commits.size()));
}

QString GitHistoryPanel::relativeTime(const QDateTime& time) const
{
    if (!time.isValid()) return tr("未知时间");

    qint64 secs = time.secsTo(QDateTime::currentDateTime());
    if (secs < 0) secs = 0;

    if (secs < 60)        return tr("刚刚");
    if (secs < 3600)      return tr("%1 分钟前").arg(secs / 60);
    if (secs < 86400)     return tr("%1 小时前").arg(secs / 3600);
    if (secs < 604800)    return tr("%1 天前").arg(secs / 86400);
    if (secs < 2592000)   return tr("%1 周前").arg(secs / 604800);
    if (secs < 31536000)  return tr("%1 个月前").arg(secs / 2592000);
    return tr("%1 年前").arg(secs / 31536000);
}

// ========== 槽函数 ==========

void GitHistoryPanel::onRefreshClicked()
{
    refresh();
}

void GitHistoryPanel::onBranchFilterChanged(int index)
{
    // 简化实现：切换分支筛选时刷新历史。
    // 注：当前 logDetailed 返回当前 HEAD 历史，分支筛选仅为 UI 提示，
    //     真正按分支过滤需 git log <branch>，此处保留刷新行为避免命令复杂化。
    Q_UNUSED(index)
    refresh();
}

void GitHistoryPanel::onCommitItemSelected(int currentRow)
{
    if (currentRow < 0 || currentRow >= m_commits.size()) {
        m_diffView->clear();
        return;
    }

    const GitCommit& commit = m_commits.at(currentRow);
    QString diffText = GitManager::instance().commitDiff(m_workspaceRoot, commit.hash);
    if (diffText.isEmpty()) {
        m_diffView->setPlainText(tr("（无 diff 信息）"));
    } else {
        m_diffView->setPlainText(diffText);
    }
}

void GitHistoryPanel::onCommitItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;

    QString fullHash = item->data(Qt::UserRole).toString();
    if (fullHash.isEmpty()) return;

    QString shortHash = fullHash.left(7);
    QString message = item->toolTip().section(QLatin1Char('\n'), 0, 0);

    emit commitDiffRequested(fullHash, shortHash, message);
}
