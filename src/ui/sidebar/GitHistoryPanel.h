#ifndef GITHISTORYPANEL_H
#define GITHISTORYPANEL_H

#include "core/vcs/GitManager.h"  // GitCommit

#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>

/// @brief Git 提交历史面板（P2-H03 子项2）
///
/// 显示仓库提交历史，支持分支筛选、查看提交 diff、双击在 DiffViewer 中打开。
/// 与 GitPanel 同级，作为「历史」tab 内嵌于 GitPanel 的 QTabWidget。
class GitHistoryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GitHistoryPanel(QWidget* parent = nullptr);

    /// 刷新提交历史（使用当前工作区根目录）
    void refresh();

    /// 设置工作区根目录（由 GitPanel 在工作区切换时注入）
    void setWorkspaceRoot(const QString& root);

signals:
    /// @brief 请求在 DiffViewer 中显示某提交与父提交的 diff
    /// @param commitHash 提交哈希
    /// @param commitHashShort 短哈希（用于标题）
    /// @param commitMessage 提交消息首行（用于标题）
    void commitDiffRequested(const QString& commitHash,
                             const QString& commitHashShort,
                             const QString& commitMessage);

private slots:
    void onRefreshClicked();
    void onBranchFilterChanged(int index);
    void onCommitItemSelected(int currentRow);
    void onCommitItemDoubleClicked(QListWidgetItem* item);

private:
    void setupUi();
    QString relativeTime(const QDateTime& time) const;

    // === UI 组件 ===
    QPushButton* m_btnRefresh = nullptr;     // 刷新按钮
    QComboBox*   m_branchCombo = nullptr;    // 分支筛选下拉
    QListWidget* m_commitList = nullptr;     // 提交历史列表
    QTextEdit*   m_diffView = nullptr;       // 选中提交的 diff 预览
    QLabel*      m_statusLabel = nullptr;    // 状态提示

    // === 数据 ===
    QString            m_workspaceRoot;                // 工作区根目录
    QList<GitCommit>   m_commits;                      // 当前显示的提交列表
};

#endif // GITHISTORYPANEL_H
