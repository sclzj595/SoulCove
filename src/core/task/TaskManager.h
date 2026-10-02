#ifndef TASKMANAGER_H
#define TASKMANAGER_H

#include <QObject>
#include <QProcess>
#include <QMap>
#include <QStringList>
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

/// @brief 任务配置结构（兼容 VSCode tasks.json 格式）
struct TaskItem {
    QString label;           // 任务名称（唯一标识）
    QString type;            // 类型: "shell" / "process"
    QString command;         // 要执行的命令
    QStringList args;        // 命令参数
    QString group;           // 分组: "build" / "test" / "lint" / "format" / "run"
    bool isBackground = false;       // 是否后台运行
    QString problemMatcher;  // 问题匹配器模式（如 $gcc / $mscompile / $pylint）
    QString presentation;    // 显示方式: "silent" / "verbose"
    int exitCode = 0;        // 上次退出码
    QDateTime lastRun;       // 上次运行时间
};

/// @brief 问题匹配结果（解析编译输出中的错误/警告）
struct ProblemMatch {
    QString file;            // 文件路径
    int line = -1;           // 行号
    int column = -1;         // 列号
    QString severity;        // "error" / "warning" / "info"
    QString message;         // 错误/警告信息
};

/// @brief 任务管理器（单例）
///
/// 支持 VSCode Tasks.json 风格的任务配置和执行。
/// 内置默认构建/测试/格式化任务，支持变量替换、问题匹配器。
class TaskManager : public QObject
{
    Q_OBJECT

public:
    /// @brief 获取单例实例
    static TaskManager& instance();

    // ========== 任务 CRUD ==========

    /// @brief 获取所有已注册任务
    QList<TaskItem> allTasks() const;

    /// @brief 根据分组获取任务列表
    QList<TaskItem> tasksByGroup(const QString& group) const;

    /// @brief 根据 label 获取任务配置
    TaskItem task(const QString& label) const;

    /// @brief 添加新任务
    void addTask(const TaskItem& task);

    /// @brief 根据 label 移除任务
    void removeTask(const QString& label);

    /// @brief 更新已有任务
    void updateTask(const TaskItem& task);

    // ========== 任务执行控制 ==========

    /// @brief 按 label 运行指定任务
    void runTask(const QString& label);

    /// @brief 运行指定分组的所有任务
    void runGroup(const QString& group);

    /// @brief 停止指定任务
    void stopTask(const QString& label);

    /// @brief 停止所有正在运行的任务
    void stopAll();

    /// @brief 查询是否有任务正在执行
    bool isRunning() const;

    /// @brief 查询指定任务是否正在运行
    bool isTaskRunning(const QString& label) const;

    // ========== Tasks.json 文件操作 ==========

    /// @brief 从 .vscode/tasks.json 格式文件加载任务
    bool loadTasksJson(const QString& filePath);

    /// @brief 保存任务到 .vscode/tasks.json 格式文件
    bool saveTasksJson(const QString& filePath);

    // ========== P3-M04 子项2: 导入/导出（通过 TasksJsonParser，与现有任务合并）==========

    /// @brief 从 VSCode tasks.json 文件导入任务（合并到现有任务列表，相同 label 覆盖）
    /// @param filePath tasks.json 文件路径
    /// @return 成功导入至少 1 个任务返回 true
    bool importTasksJson(const QString& filePath);

    /// @brief 导出当前所有任务到 VSCode tasks.json 文件
    /// @param filePath 目标文件路径
    /// @return 写入成功返回 true
    bool exportTasksJson(const QString& filePath) const;

    // ========== 默认任务模板 ==========

    /// @brief 创建常用构建/测试/格式化任务的默认模板
    void createDefaultTasks();

    // ========== 问题匹配器 ==========

    /// @brief 解析编译器输出行，提取错误/警告信息
    static ProblemMatch parseProblemLine(const QString& line, const QString& matcher = QStringLiteral("gcc"));

signals:
    /// @brief 任务开始执行
    void taskStarted(const QString& label);

    /// @brief 任务执行完毕
    /// @param label 任务显示名称
    /// @param exitCode 退出码（0=成功）
    /// @param output 完整输出内容
    void taskFinished(const QString& label, int exitCode, const QString& output);

    /// @brief 任务实时输出（逐行）
    void taskOutput(const QString& label, const QString& outputLine);

    /// @brief 任务出错
    void taskError(const QString& label, const QString& error);

private:
    TaskManager();
    ~TaskManager() = default;
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;

    QMap<QString, TaskItem> m_tasks;              // label → task
    QMap<QString, QProcess*> m_runningProcesses;   // label → 运行中的进程

    /// @brief 解析 VSCode tasks.json 格式的 JSON
    void parseVsCodeTasksJson(const QJsonDocument& doc);

    /// @brief 替换命令中的变量（${workspaceFolder}, ${file} 等）
    QString expandVariables(const QString& cmd) const;

    /// @brief 启动一个任务进程
    void startProcess(const QString& label, const TaskItem& task);
};

#endif // TASKMANAGER_H
