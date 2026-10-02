#ifndef DEBUGMANAGER_H
#define DEBUGMANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QHash>
#include <QSet>

/// @brief 调试状态枚举（P3-M04 子项3）
enum class DebugState {
    Stopped,   ///< 未启动 / 已退出
    Running,   ///< 调试程序运行中
    Paused     ///< 命中断点 / 单步停止
};

/// @brief GDB MI 接口调试管理器（P3-M04 子项3）
///
/// 通过 QProcess 驱动 GDB（--interpreter=mi），实现基础调试功能：
/// - 启动 / 停止调试会话
/// - 断点管理（设置 / 删除）
/// - 继续执行 / 单步（跳过/进入/跳出）/ 运行到光标
/// - 解析 GDB MI 异步记录（*stopped / =breakpoint-created / ~"..." 等）
///
/// 信号约定：
/// - stateChanged: 状态变更通知（UI 刷新按钮 enable 状态）
/// - breakpointHit: 命中断点（UI 跳转到对应文件:行）
/// - outputMessage: GDB 控制台输出 / 调试目标 stdout
/// - variablesReady: 变量列表（停止时通过 -stack-list-variables 获取）
class DebugManager : public QObject
{
    Q_OBJECT

public:
    explicit DebugManager(QObject* parent = nullptr);
    ~DebugManager() override;

    // ========== 调试会话控制 ==========

    /// @brief 启动调试会话
    /// @param program 要调试的可执行文件路径
    /// @param args    程序命令行参数
    /// @param workingDir 工作目录（空则使用应用目录）
    void startDebug(const QString& program, const QStringList& args, const QString& workingDir);

    /// @brief 停止调试会话（终止 GDB 进程与被调试程序）
    void stopDebug();

    /// @brief 当前调试状态
    DebugState state() const { return m_state; }

    /// @brief 是否处于活跃调试会话（Running 或 Paused）
    bool isActive() const { return m_state != DebugState::Stopped; }

    // ========== 断点管理 ==========

    /// @brief 设置断点（重复设置同一位置自动忽略）
    void setBreakpoint(const QString& file, int line);

    /// @brief 移除断点
    void removeBreakpoint(const QString& file, int line);

    /// @brief 清除所有断点
    void clearBreakpoints();

    /// @brief 获取当前所有断点（file → 行号集合，行号为 1-based）
    QHash<QString, QSet<int>> breakpoints() const { return m_breakpoints; }

    // ========== 执行控制（仅在 Paused 状态有效）==========

    /// @brief 继续执行
    void continueExecution();

    /// @brief 单步跳过（next）
    void stepOver();

    /// @brief 单步进入（step）
    void stepInto();

    /// @brief 单步跳出（finish）
    void stepOut();

    /// @brief 运行到光标位置（临时断点 + continue）
    void runToCursor(const QString& file, int line);

signals:
    /// @brief 调试状态变更
    void stateChanged(DebugState state);

    /// @brief 命中断点 / 单步停止位置
    /// @param file 源文件路径
    /// @param line 行号（1-based）
    void breakpointHit(const QString& file, int line);

    /// @brief GDB / 调试目标输出消息
    void outputMessage(const QString& msg);

    /// @brief 变量列表就绪（停止时自动获取）
    /// 每个变量为 QVariantMap: { name, value, type }
    void variablesReady(const QVariantList& vars);

private slots:
    void onGdbReadyReadStandardOutput();
    void onGdbReadyReadStandardError();
    void onGdbFinished(int exitCode, QProcess::ExitStatus status);
    void onGdbErrorOccurred(QProcess::ProcessError error);

private:
    // ========== 内部辅助 ==========
    void setState(DebugState newState);
    /// @brief 发送 MI 命令（自动附加换行；token 由调用方按需拼接）
    void sendMiCommand(const QString& miCmd);

    /// @brief 按行处理 GDB MI 输出
    void processMiLine(const QString& line);

    /// @brief 解析 *stopped 异步记录，提取停止原因与位置
    void handleStopped(const QString& recordBody);

    /// @brief 解析 =breakpoint-created / =breakpoint-deleted 异步记录
    void handleBreakpointCreated(const QString& recordBody);

    /// @brief 解析 ~"..." GDB 控制台流输出（转义处理）
    void handleConsoleStream(const QString& text);

    /// @brief 解析 @"..." 调试目标 stdout 流输出
    void handleTargetStream(const QString& text);

    /// @brief 解析 &"..." GDB 日志流输出
    void handleLogStream(const QString& text);

    /// @brief 解析 MI 命令的 (gdb) 提示符 / ^done / ^error 等结果记录
    void handleResultRecord(const QString& line);

    /// @brief 请求 GDB 列出当前栈帧变量（停止时调用）
    void requestVariables();

    /// @brief 简易 MI 字符串列表解析（从 "frame={...}" 提取 field）
    static QString extractMiField(const QString& src, const QString& fieldName);

    /// @brief 反转义 MI 字符串字面量（去除 \" \n 等转义）
    static QString unescapeMiString(const QString& s);

    QProcess* m_gdb = nullptr;
    DebugState m_state = DebugState::Stopped;

    QString m_program;             ///< 当前调试程序
    QStringList m_args;            ///< 程序参数
    QString m_workingDir;          ///< 工作目录
    QString m_pendingOutput;       ///< 输出缓冲（按 \n 分行）

    QHash<QString, QSet<int>> m_breakpoints;   ///< 当前断点集合（file → 行号集合）
};

#endif // DEBUGMANAGER_H
