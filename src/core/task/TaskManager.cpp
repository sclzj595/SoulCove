#include "core/task/TaskManager.h"
#include "core/task/TasksJsonParser.h"  // P3-M04 子项2: 导入/导出
#include "Logger.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QRegularExpression>
#include <QFile>

// ============================================================
// 单例
// ============================================================

TaskManager& TaskManager::instance()
{
    static TaskManager inst;
    return inst;
}

TaskManager::TaskManager()
    : QObject(nullptr)
{
    // 创建默认任务模板
    createDefaultTasks();
}

// ============================================================
// 任务 CRUD
// ============================================================

QList<TaskItem> TaskManager::allTasks() const
{
    return m_tasks.values();
}

QList<TaskItem> TaskManager::tasksByGroup(const QString& group) const
{
    QList<TaskItem> result;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.value().group == group) {
            result.append(it.value());
        }
    }
    return result;
}

TaskItem TaskManager::task(const QString& label) const
{
    return m_tasks.value(label);
}

void TaskManager::addTask(const TaskItem& task)
{
    if (!task.label.isEmpty()) {
        m_tasks[task.label] = task;
        LOG_DEBUG_S("TaskManager", "addTask", "添加任务:" << task.label << "分组:" << task.group);
    }
}

void TaskManager::removeTask(const QString& label)
{
    if (m_runningProcesses.contains(label)) {
        stopTask(label);
    }
    m_tasks.remove(label);
    LOG_DEBUG_S("TaskManager", "removeTask", "移除任务:" << label);
}

void TaskManager::updateTask(const TaskItem& task)
{
    if (!task.label.isEmpty()) {
        m_tasks[task.label] = task;
    }
}

// ============================================================
// 任务执行控制
// ============================================================

void TaskManager::runTask(const QString& label)
{
    if (!m_tasks.contains(label)) {
        emit taskError(label, tr("任务不存在: %1").arg(label));
        return;
    }

    if (m_runningProcesses.contains(label)) {
        emit taskError(label, tr("任务正在运行中: %1").arg(label));
        return;
    }

    startProcess(label, m_tasks[label]);
}

void TaskManager::runGroup(const QString& group)
{
    QList<TaskItem> tasks = tasksByGroup(group);
    for (const auto& t : tasks) {
        runTask(t.label);
    }
}

void TaskManager::stopTask(const QString& label)
{
    auto* proc = m_runningProcesses.take(label);
    if (proc && proc->state() == QProcess::Running) {
        proc->terminate();
        if (!proc->waitForFinished(3000)) {
            proc->kill();
            proc->waitForFinished(2000);
        }
        LOG_DEBUG_S("TaskManager", "stopTask", "已停止任务:" << label);
        emit taskFinished(label, -1, tr("用户终止"));
    }
}

void TaskManager::stopAll()
{
    QStringList labels = m_runningProcesses.keys();
    for (const QString& lbl : labels) {
        stopTask(lbl);
    }
}

bool TaskManager::isRunning() const
{
    return !m_runningProcesses.isEmpty();
}

bool TaskManager::isTaskRunning(const QString& label) const
{
    return m_runningProcesses.contains(label);
}

// ============================================================
// 进程管理（内部）
// ============================================================

void TaskManager::startProcess(const QString& label, const TaskItem& task)
{
    auto* proc = new QProcess(this);

    // 展开变量
    QString expandedCmd = expandVariables(task.command);
    QStringList expandedArgs;
    for (const QString& arg : task.args) {
        expandedArgs.append(expandVariables(arg));
    }

    // 设置工作目录
    QString workDir = QCoreApplication::applicationDirPath();
    if (QDir(QStringLiteral("./build")).exists()) {
        workDir = QDir::currentPath();  // 使用 CMake 构建目录
    }
    proc->setWorkingDirectory(workDir);

    // 设置进程通道模式
    proc->setProcessChannelMode(QProcess::MergedChannels);

    // 连接信号
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, label, proc]() {
        QByteArray data = proc->readAllStandardOutput();
        QString output = QString::fromUtf8(data);
        emit taskOutput(label, output);
    });

    connect(proc, &QProcess::readyReadStandardError, this, [this, label, proc]() {
        QByteArray data = proc->readAllStandardError();
        QString output = QString::fromUtf8(data);
        emit taskOutput(label, output);
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, label, proc](int exitCode, QProcess::ExitStatus /*exitStatus*/) {
        // 读取剩余输出
        QByteArray remaining = proc->readAllStandardOutput() + proc->readAllStandardError();
        QString output = QString::fromUtf8(remaining);

        // 更新任务状态
        if (m_tasks.contains(label)) {
            TaskItem t = m_tasks[label];
            t.exitCode = exitCode;
            t.lastRun = QDateTime::currentDateTime();
            m_tasks[label] = t;
        }

        m_runningProcesses.remove(label);
        proc->deleteLater();

        LOG_DEBUG_S("TaskManager", "startProcess", "任务结束:" << label << "退出码:" << exitCode);
        emit taskFinished(label, exitCode, output);
    });

    connect(proc, &QProcess::errorOccurred, this, [this, label](QProcess::ProcessError error) {
        QString errMsg;
        switch (error) {
        case QProcess::FailedToStart:  errMsg = tr("进程启动失败"); break;
        case QProcess::Crashed:        errMsg = tr("进程崩溃"); break;
        case QProcess::Timedout:       errMsg = tr("操作超时"); break;
        default:                       errMsg = tr("未知错误"); break;
        }
        emit taskError(label, errMsg);
    });

    // 启动进程
    if (task.type == QStringLiteral("shell")) {
        // shell 类型: 将命令和参数拼接后通过系统 shell 执行
        QString fullCmd = expandedCmd + QStringLiteral(" ") + expandedArgs.join(QLatin1Char(' '));
#ifdef Q_OS_WIN
        proc->start(QStringLiteral("cmd"), {QStringLiteral("/c"), fullCmd});
#else
        proc->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), fullCmd});
#endif
    } else {
        // process 类型: 直接执行命令
        proc->start(expandedCmd, expandedArgs);
    }

    if (proc->waitForStarted(3000)) {
        m_runningProcesses[label] = proc;
        LOG_DEBUG_S("TaskManager", "startProcess", "任务已启动:" << label << "命令:" << expandedCmd);
        emit taskStarted(label);
    } else {
        delete proc;
        emit taskError(label, tr("无法启动进程: %1").arg(expandedCmd));
    }
}

// ============================================================
// Tasks.json 文件操作
// ============================================================

bool TaskManager::loadTasksJson(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_WARN_S("TaskManager", "loadTasksJson", "无法打开 tasks.json:" << filePath);
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN_S("TaskManager", "loadTasksJson", "JSON 解析错误:" << error.errorString());
        return false;
    }

    parseVsCodeTasksJson(doc);
    LOG_DEBUG_S("TaskManager", "loadTasksJson", "从 tasks.json 加载了" << m_tasks.size() << "个任务");
    return true;
}

bool TaskManager::saveTasksJson(const QString& filePath)
{
    QJsonObject root;
    root[QStringLiteral("version")] = QStringLiteral("2.0.0");

    QJsonArray tasksArray;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        const TaskItem& t = it.value();

        QJsonObject taskObj;
        taskObj[QStringLiteral("label")] = t.label;
        taskObj[QStringLiteral("type")] = t.type;
        taskObj[QStringLiteral("command")] = t.command;

        QJsonArray argsArray;
        for (const QString& arg : t.args) {
            argsArray.append(arg);
        }
        taskObj[QStringLiteral("args")] = argsArray;

        QJsonObject groupObj;
        groupObj[QStringLiteral("kind")] = t.group;
        groupObj[QStringLiteral("isDefault")] = (t.group == QStringLiteral("build"));
        taskObj[QStringLiteral("group")] = groupObj;

        if (!t.problemMatcher.isEmpty()) {
            taskObj[QStringLiteral("problemMatcher")] = t.problemMatcher;
        }

        tasksArray.append(taskObj);
    }

    root[QStringLiteral("tasks")] = tasksArray;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        LOG_DEBUG_S("TaskManager", "saveTasksJson", "任务已保存到:" << filePath);
        return true;
    }

    return false;
}

void TaskManager::parseVsCodeTasksJson(const QJsonDocument& doc)
{
    QJsonObject root = doc.object();
    QJsonArray tasksArray = root[QStringLiteral("tasks")].toArray();

    for (const QJsonValue& val : tasksArray) {
        QJsonObject obj = val.toObject();

        TaskItem t;
        t.label = obj[QStringLiteral("label")].toString();
        t.type = obj[QStringLiteral("type")].toString(QStringLiteral("shell"));
        t.command = obj[QStringLiteral("command")].toString();

        QJsonArray argsArr = obj[QStringLiteral("args")].toArray();
        for (const QJsonValue& a : argsArr) {
            t.args.append(a.toString());
        }

        // 解析 group
        QJsonObject groupObj = obj[QStringLiteral("group")].toObject();
        t.group = groupObj[QStringLiteral("kind")].toString(QStringLiteral("build"));

        t.problemMatcher = obj[QStringLiteral("problemMatcher")].toString();
        t.presentation = obj[QStringLiteral("presentation")].toString();

        if (!t.label.isEmpty()) {
            m_tasks[t.label] = t;
        }
    }
}

// ============================================================
// P3-M04 子项2: 导入/导出（通过 TasksJsonParser，与现有任务合并）
// ============================================================

bool TaskManager::importTasksJson(const QString& filePath)
{
    QList<TaskItem> imported = TasksJsonParser::parseFile(filePath);
    if (imported.isEmpty()) {
        LOG_WARN_S("TaskManager", "importTasksJson", "导入失败或文件无任务:" << filePath);
        return false;
    }

    int mergedCount = 0;
    for (const TaskItem& t : imported) {
        if (t.label.isEmpty()) continue;
        // 合并策略：相同 label 直接覆盖（保留运行时状态 exitCode/lastRun）
        TaskItem existing = m_tasks.value(t.label);
        TaskItem merged = t;
        if (existing.label == t.label) {
            merged.exitCode = existing.exitCode;
            merged.lastRun  = existing.lastRun;
        }
        m_tasks[t.label] = merged;
        ++mergedCount;
    }

    LOG_DEBUG_S("TaskManager", "importTasksJson",
                "从" << filePath << "导入了" << mergedCount << "个任务");
    return mergedCount > 0;
}

bool TaskManager::exportTasksJson(const QString& filePath) const
{
    QString jsonText = TasksJsonParser::toJson(m_tasks.values());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_WARN_S("TaskManager", "exportTasksJson", "无法写入文件:" << filePath);
        return false;
    }
    file.write(jsonText.toUtf8());
    file.close();

    LOG_DEBUG_S("TaskManager", "exportTasksJson",
                "已导出" << m_tasks.size() << "个任务到" << filePath);
    return true;
}

// ============================================================
// 默认任务模板
// ============================================================

void TaskManager::createDefaultTasks()
{
    // === CMake 构建相关 ===
    TaskItem buildCMake;
    buildCMake.label = tr("Build (CMake)");
    buildCMake.type = QStringLiteral("shell");
    buildCMake.command = QStringLiteral("cmake");
    buildCMake.args = {QStringLiteral("--build"), QStringLiteral("build")};
    buildCMake.group = QStringLiteral("build");
    buildCMake.isBackground = true;
    buildCMake.problemMatcher = QStringLiteral("$gcc");
    addTask(buildCMake);

    TaskItem rebuildCMake;
    rebuildCMake.label = tr("Rebuild (CMake)");
    rebuildCMake.type = QStringLiteral("shell");
    rebuildCMake.command = QStringLiteral("cmake");
    rebuildCMake.args = {QStringLiteral("--build"), QStringLiteral("build"), QStringLiteral("--clean-first")};
    rebuildCMake.group = QStringLiteral("build");
    rebuildCMake.isBackground = true;
    rebuildCMake.problemMatcher = QStringLiteral("$gcc");
    addTask(rebuildCMake);

    TaskItem cleanCMake;
    cleanCMake.label = tr("Clean (CMake)");
    cleanCMake.type = QStringLiteral("shell");
    cleanCMake.command = QStringLiteral("cmake");
    cleanCMake.args = {QStringLiteral("--build"), QStringLiteral("build"), QStringLiteral("--target"), QStringLiteral("clean")};
    cleanCMake.group = QStringLiteral("build");
    cleanCMake.isBackground = true;
    addTask(cleanCMake);

    // === 运行 ===
    TaskItem runApp;
    runApp.label = tr("Run Application");
    runApp.type = QStringLiteral("shell");
    runApp.command = QStringLiteral("./build/SoulCoveNotebookLite.exe");  // SoulCove Notebook Lite
    runApp.args = {};
    runApp.group = QStringLiteral("run");
    runApp.isBackground = false;
    addTask(runApp);

    // === 测试 ===
    TaskItem testCtest;
    testCtest.label = tr("Test (CTest)");
    testCtest.type = QStringLiteral("shell");
    testCtest.command = QStringLiteral("ctest");
    testCtest.args = {QStringLiteral("--output-on-failure")};
    testCtest.group = QStringLiteral("test");
    testCtest.isBackground = true;
    testCtest.problemMatcher = QStringLiteral("$gcc");
    addTask(testCtest);

    // === Lint / 格式化 ===
    TaskItem lintClangTidy;
    lintClangTidy.label = tr("Lint (clang-tidy)");
    lintClangTidy.type = QStringLiteral("shell");
    lintClangTidy.command = QStringLiteral("clang-tidy");
    lintClangTidy.args = {QStringLiteral("src/*.cpp")};
    lintClangTidy.group = QStringLiteral("lint");
    lintClangTidy.isBackground = true;
    lintClangTidy.problemMatcher = QStringLiteral("$gcc");
    addTask(lintClangTidy);

    TaskItem formatClangFormat;
    formatClangFormat.label = tr("Format (clang-format)");
    formatClangFormat.type = QStringLiteral("shell");
    formatClangFormat.command = QStringLiteral("clang-format");
    formatClangFormat.args = {QStringLiteral("-i"), QStringLiteral("src/*.cpp")};
    formatClangFormat.group = QStringLiteral("format");
    formatClangFormat.isBackground = true;
    addTask(formatClangFormat);

    LOG_DEBUG_S("TaskManager", "createDefaultTasks", "已创建默认任务模板，共" << m_tasks.size() << "个");
}

// ============================================================
// 变量替换
// ============================================================

QString TaskManager::expandVariables(const QString& cmd) const
{
    QString result = cmd;

    // ${workspaceFolder} → 当前工作目录
    result.replace(QStringLiteral("${workspaceFolder}"), QDir::currentPath());

    // ${file} → 占位（需要外部注入当前文件路径）
    // result.replace("${file}", currentFile);

    // ${workspaceFolderBasename} → 工作目录名
    result.replace(QStringLiteral("${workspaceFolderBasename}"),
                   QDir::current().dirName());

    return result;
}

// ============================================================
// 问题匹配器
// ============================================================

ProblemMatch TaskManager::parseProblemLine(const QString& line, const QString& matcher)
{
    ProblemMatch match;

    if (matcher == QStringLiteral("gcc") || matcher == QStringLiteral("$gcc")) {
        // GCC/Clang 格式: /path/to/file.cpp:line:col: error: message
        // 或: /path/to/file.cpp:line:col: warning: message
        static QRegularExpression re(
            R"(^([^:]+):(\d+):(\d+):\s*(error|warning|note):\s*(.+)$)"
        );
        QRegularExpressionMatch m = re.match(line.trimmed());
        if (m.hasMatch()) {
            match.file = m.captured(1);
            match.line = m.captured(2).toInt();
            match.column = m.captured(3).toInt();
            match.severity = m.captured(4);
            match.message = m.captured(5);
        }
    }
    else if (matcher == QStringLiteral("mscompile") || matcher == QStringLiteral("$mscompile")) {
        // MSVC 格式: path_to_file(line,col): error/warning XXXX: message
        static QRegularExpression re(
            R"(^([^(]+)\((\d+),(\d+)\)\s*:\s*(error|warning)\s+(\w+)\s*:\s*(.+)$)"
        );
        QRegularExpressionMatch m = re.match(line.trimmed());
        if (m.hasMatch()) {
            match.file = m.captured(1).trimmed();
            match.line = m.captured(2).toInt();
            match.column = m.captured(3).toInt();
            match.severity = m.captured(4);
            match.message = m.captured(6);
        }
    }
    else if (matcher == QStringLiteral("pylint") || matcher == QStringLiteral("$pylint")) {
        // Pylint 格式: path_to_file:line: [type] CODE: message
        static QRegularExpression re(
            R"(^([^:]+):(\d+):\s*\[(\w+)[^\]]*\]\s*([\w-]*):\s*(.+)$)"
        );
        QRegularExpressionMatch m = re.match(line.trimmed());
        if (m.hasMatch()) {
            match.file = m.captured(1);
            match.line = m.captured(2).toInt();
            match.severity = m.captured(3);  // error/warning/info
            match.message = m.captured(5);
        }
    }

    return match;
}
