#ifndef TASKRUNNER_H
#define TASKRUNNER_H

#include <QObject>
#include <QProcess>
#include <QMap>
#include <QStringList>
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

/// @brief 任务配置结构（兼容 VSCode tasks.json 格式）
struct TaskConfig {
    QString id;            // 唯一标识符
    QString label;         // 显示名称
    QString type;          // 类型: "shell" / "process"
    QString command;       // 要执行的命令
    QStringList args;      // 命令参数
    QString workingDir;    // 工作目录

    enum ShowType { Always, Silent, Never } showOutput = Always;
    bool problemMatcher = false;  // 是否解析错误输出
    QString group;          // 分组: "build" / "test" / "clean" / "other"
    bool isDefault = false; // 是否为 group 的默认任务
};

/// @brief 任务运行器（单例）
///
/// 支持 VSCode Tasks.json 风格的任务配置和执行。
/// 内置默认构建/测试任务，支持变量替换、输出解析。
class TaskRunner : public QObject
{
    Q_OBJECT

public:
    /// @brief 获取单例实例
    static TaskRunner& instance();

    // ========== 任务 CRUD ==========

    /// @brief 获取所有已注册任务
    QList<TaskConfig> allTasks() const;

    /// @brief 根据 ID 获取任务配置
    TaskConfig task(const QString& id) const;

    /// @brief 添加新任务
    void addTask(const TaskConfig& task);

    /// @brief 根据 ID 移除任务
    void removeTask(const QString& id);

    /// @brief 更新已有任务
    void updateTask(const TaskConfig& task);

    // ========== 执行控制 ==========

    /// @brief 按 ID 运行指定任务
    void runTask(const QString& id);

    /// @brief 运行指定分组的默认任务
    /// @param group 分组名称，默认 "build"
    void runDefaultTask(const QString& group = QStringLiteral("build"));

    /// @brief 停止当前正在运行的任务
    void stopCurrentTask();

    /// @brief 查询是否有任务正在执行
    bool isRunning() const;

    /// @brief 获取当前正在运行的任务的标签
    QString currentTaskLabel() const;

    // ========== 配置文件操作 ==========

    /// @brief 从 .vscode/tasks.json 格式文件加载任务
    void loadFromTasksJson(const QString& filePath);

    /// @brief 保存任务到 .vscode/tasks.json 格式文件
    void saveToTasksJson(const QString& filePath);

signals:
    /// @brief 任务开始执行
    void taskStarted(const QString& label);

    /// @brief 任务执行完毕
    /// @param label 任务显示名称
    /// @param exitCode 退出码（0=成功）
    /// @param output 完整输出内容
    void taskFinished(const QString& label, int exitCode, const QString& output);

    /// @brief 任务实时输出（逐行）
    void taskOutput(const QString& outputLine);

private:
    TaskRunner();
    ~TaskRunner() = default;
    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    QProcess* m_currentProcess = nullptr;
    QMap<QString, TaskConfig> m_tasks;
    QString m_currentTaskLabel;

    /// @brief 加载内置默认任务（CMake 构建/测试等）
    void loadDefaultTasks();

    /// @brief 替换命令中的变量（${workspaceFolder}, ${file} 等）
    QString expandVariables(const QString& command) const;

    /// @brief 解析编译器输出中的错误/警告行
    QVariantMap parseProblemLine(const QString& line) const;
};

#endif // TASKRUNNER_H
