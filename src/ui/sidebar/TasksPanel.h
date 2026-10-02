#ifndef TASKSPANEL_H
#define TASKSPANEL_H

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QLineEdit;
class QToolButton;

/// @brief 任务面板（M15: 任务系统）
///
/// 职责：显示任务列表（按 build/run/test/lint/format 分组）、运行/停止任务、
///       实时显示任务输出、配置 tasks.json。
///
/// 设计说明：
/// - 从 SideBar 抽取，自含 UI（任务树 + 工具栏 + 输出区）与逻辑
/// - 通过 setWorkDirectory 注入工作目录（用于定位 .vscode/tasks.json）
/// - 内部直连 TaskManager 单例信号（taskStarted/taskFinished/taskOutput）
/// - 输出区支持文本过滤（按行匹配，大小写不敏感）与「仅显示错误/警告」模式
/// - 双击识别 file:line:col 模式输出，发射 jumpToLocationRequested 供 Widget 跳转
class TasksPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TasksPanel(QWidget* parent = nullptr);

    /// @brief 设置工作目录（用于定位 .vscode/tasks.json）
    void setWorkDirectory(const QString& dirPath);

    /// @brief 刷新任务树（任务列表变更/状态变更时调用）
    void refreshTaskTree();

protected:
    /// @brief 事件过滤器 — 捕获输出区 viewport 双击事件以触发错误跳转
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    /// @brief 双击输出行识别到 file:line:col 模式时发射，请求 Widget 跳转
    /// @param filePath 文件路径（已解析为绝对路径，若无法解析则为原文本）
    /// @param line 行号（1-based），失败时为 -1
    /// @param col  列号（1-based），失败时为 -1
    void jumpToLocationRequested(const QString& filePath, int line, int col);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemContextMenu(const QPoint& pos);
    void onRunClicked();
    void onStopAllClicked();
    void onConfigureClicked();
    void onTaskStarted(const QString& label);
    void onTaskFinished(const QString& label, int exitCode, const QString& output);
    void onTaskOutput(const QString& label, const QString& output);

    // === P3-M04 子项1: 输出过滤 ===
    void onFilterTextChanged(const QString& text);
    void onFilterErrorsToggled(bool checked);
    void onOutputDoubleClicked();

private:
    // === 输出过滤辅助 ===
    /// @brief 重新根据当前过滤条件渲染输出区
    void applyOutputFilter();
    /// @brief 尝试解析一行输出中的 file:line:col 或 file:line 模式
    /// @return 解析成功返回 true，filePath/line/col 输出参数
    bool parseLocationFromLine(const QString& line, QString& filePath, int& lineNum, int& col) const;
    /// @brief 将可能相对的文件路径解析为绝对路径（基于 m_workDir）
    QString resolveFilePath(const QString& filePath) const;

    QTreeWidget*    m_taskTree = nullptr;
    QPlainTextEdit* m_taskOutputView = nullptr;
    QLineEdit*      m_filterEdit = nullptr;          // P3-M04 子项1: 过滤输入框
    QToolButton*    m_btnFilterErrors = nullptr;     // P3-M04 子项1: 仅显示错误/警告
    QPushButton*    m_btnRun = nullptr;
    QPushButton*    m_btnStopAll = nullptr;
    QPushButton*    m_btnConfigure = nullptr;
    QString         m_workDir;

    // === P3-M04 子项1: 输出历史与过滤状态 ===
    QStringList m_outputLines;          // 完整输出历史（按行存储）
    bool        m_filterErrorsOnly = false;  // 是否仅显示错误/警告行
};

#endif // TASKSPANEL_H
