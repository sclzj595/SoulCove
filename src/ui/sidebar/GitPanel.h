#ifndef GITPANEL_H
#define GITPANEL_H

#include "interfaces/vcs/IGitManager.h"

#include <QWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

/// @brief 源代码管理面板（Git 集成 UI）
/// 显示分支、文件状态、diff，支持基本 Git 操作
class GitPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GitPanel(QWidget* parent = nullptr);

    /// 刷新所有状态
    void refresh();

signals:
    /// @brief 请求打开文件 Diff 视图
    void fileDiffRequested(const QString& filePath);

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

    // === UI 组件 ===
    QLabel*        m_branchLabel;       // 当前分支标签
    QComboBox*     m_branchCombo;       // 分支选择器
    QPushButton*   m_btnPull;           // 拉取按钮
    QPushButton*   m_btnPush;           // 推送按钮
    QPushButton*   m_btnRefresh;        // 刷新按钮

    QTreeWidget*   m_fileTree;          // 文件状态树

    QLineEdit*     m_commitMsgEdit;     // 提交消息输入框
    QPushButton*   m_btnCommit;         // 提交按钮
    QPushButton*   m_btnDiscard;        // 放弃更改按钮
    QPushButton*   m_btnStage;          // 暂存更改按钮

    QLabel*        m_statusLabel;       // 状态提示标签
};

#endif // GITPANEL_H
