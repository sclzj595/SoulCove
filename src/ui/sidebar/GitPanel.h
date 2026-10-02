#ifndef GITPANEL_H
#define GITPANEL_H

#include "interfaces/vcs/IGitManager.h"

#include <QWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>

class GitHistoryPanel;  // P2-H03 子项2: 历史面板（前向声明）

/// @brief 源代码管理面板（Git 集成 UI）
/// 显示分支、文件状态、diff，支持基本 Git 操作
/// P2-H03 子项2: 改造为 QTabWidget，包含「更改」和「历史」两个 tab
class GitPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GitPanel(QWidget* parent = nullptr);

    /// 刷新所有状态
    void refresh();

    /// P2-H03: 设置工作区根目录（同步给历史面板）
    void setWorkspaceRoot(const QString& root);

    /// P2-H03: 获取内嵌的历史面板（供外部连接信号）
    GitHistoryPanel* historyPanel() const { return m_historyPanel; }

signals:
    /// @brief 请求打开文件 Diff 视图
    void fileDiffRequested(const QString& filePath);

    /// @brief P2-H03: 请求打开某提交与父提交的 diff 视图
    void commitDiffRequested(const QString& commitHash,
                             const QString& commitHashShort,
                             const QString& commitMessage);

private slots:
    void onRefreshClicked();
    void onBranchChanged(int index);
    void onFileItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCommitClicked();
    void onPullClicked();
    void onPushClicked();
    void onDiscardClicked();
    void onStageClicked();
    void onRepoChanged();
    void onOperationFinished(const QString& op, bool success, const QString& output);

private:
    void setupUi();
    QTreeWidgetItem* createFileItem(const QString& filePath,
                                     GitFileStatus::Status status);
    QString statusText(GitFileStatus::Status status) const;
    QString statusIcon(GitFileStatus::Status status) const;

    // === 顶层 Tab 容器（P2-H03 子项2）===
    QTabWidget*     m_tabWidget = nullptr;   // 更改/历史 tab 容器

    // === UI 组件（「更改」tab 内）===
    QLabel*        m_branchLabel = nullptr;       // 当前分支标签
    QComboBox*     m_branchCombo = nullptr;       // 分支选择器
    QPushButton*   m_btnPull = nullptr;           // 拉取按钮
    QPushButton*   m_btnPush = nullptr;           // 推送按钮
    QPushButton*   m_btnRefresh = nullptr;        // 刷新按钮

    QTreeWidget*   m_fileTree = nullptr;          // 文件状态树

    QLineEdit*     m_commitMsgEdit = nullptr;     // 提交消息输入框
    QPushButton*   m_btnCommit = nullptr;         // 提交按钮
    QPushButton*   m_btnDiscard = nullptr;        // 放弃更改按钮
    QPushButton*   m_btnStage = nullptr;          // 暂存更改按钮

    QLabel*        m_statusLabel = nullptr;       // 状态提示标签

    // === 「历史」tab（P2-H03 子项2）===
    GitHistoryPanel* m_historyPanel = nullptr;
};

#endif // GITPANEL_H
