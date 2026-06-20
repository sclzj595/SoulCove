#ifndef TASKSPANEL_H
#define TASKSPANEL_H

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QPushButton;

/// @brief 任务面板（M15: 任务系统）
///
/// 职责：显示任务列表（按 build/run/test/lint/format 分组）、运行/停止任务、
///       实时显示任务输出、配置 tasks.json。
///
/// 设计说明：
/// - 从 SideBar 抽取，自含 UI（任务树 + 工具栏 + 输出区）与逻辑
/// - 通过 setWorkDirectory 注入工作目录（用于定位 .vscode/tasks.json）
/// - 内部直连 TaskManager 单例信号（taskStarted/taskFinished/taskOutput）
/// - 零对外信号（任务运行为内部动作，不需要 SideBar 转发）
class TasksPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TasksPanel(QWidget* parent = nullptr);

    /// @brief 设置工作目录（用于定位 .vscode/tasks.json）
    void setWorkDirectory(const QString& dirPath);

    /// @brief 刷新任务树（任务列表变更/状态变更时调用）
    void refreshTaskTree();

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemContextMenu(const QPoint& pos);
    void onRunClicked();
    void onStopAllClicked();
    void onConfigureClicked();
    void onTaskStarted(const QString& label);
    void onTaskFinished(const QString& label, int exitCode, const QString& output);
    void onTaskOutput(const QString& label, const QString& output);

private:
    QTreeWidget*    m_taskTree = nullptr;
    QPlainTextEdit* m_taskOutputView = nullptr;
    QPushButton*    m_btnRun = nullptr;
    QPushButton*    m_btnStopAll = nullptr;
    QPushButton*    m_btnConfigure = nullptr;
    QString         m_workDir;
};

#endif // TASKSPANEL_H
