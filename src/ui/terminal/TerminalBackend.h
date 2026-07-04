#ifndef TERMINALBACKEND_H
#define TERMINALBACKEND_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <memory>

/// @brief 终端后端 — 管理单个 shell 进程的生命周期和数据 I/O
///
/// 职责：
/// - 启动/停止 shell 进程（cmd.exe / powershell.exe）
/// - 管理 stdin/stdout/stderr 数据通道
/// - 提供清晰的信号/槽接口供 TerminalView 使用
///
/// RAII保证：
/// - 析构时自动停止进程并清理资源
/// - 智能指针管理进程对象生命周期
/// - 异常安全的资源释放
class TerminalBackend : public QObject
{
    Q_OBJECT

public:
    explicit TerminalBackend(QObject* parent = nullptr);
    ~TerminalBackend() override;

    // 禁用拷贝（含唯一资源）
    TerminalBackend(const TerminalBackend&) = delete;
    TerminalBackend& operator=(const TerminalBackend&) = delete;

    // 支持移动（C++11起）
    TerminalBackend(TerminalBackend&&) = default;
    TerminalBackend& operator=(TerminalBackend&&) = default;

    /// @brief Shell 类型
    enum class ShellType {
        CMD,
        PowerShell
    };

    /// @brief 启动 shell 进程
    /// @param type  shell 类型（CMD / PowerShell）
    /// @param workDir 工作目录（空则使用当前目录）
    /// @return 是否成功启动
    bool start(ShellType type, const QString& workDir = QString());

    /// @brief 停止进程（优雅退出 → 强制杀死）
    /// @note RAII安全：析构函数也会调用此方法
    void stop();

    /// @brief 向 stdin 写入数据（发送命令/按键）
    void write(const QByteArray& data);

    /// @brief 发送 Ctrl+C 中断信号（Windows 下发送 0x03）
    void sendInterrupt();

    /// @brief 进程是否正在运行
    bool isRunning() const;

    /// @brief 获取当前 shell 类型
    ShellType shellType() const { return m_shellType; }

    // ========== P2-H01: 引用计数（终端复用/克隆）==========
    /// @brief 增加引用计数（克隆标签页时调用）
    void addRef();
    /// @brief 减少引用计数，归零时自动销毁（关闭标签页时调用）
    void release();
    /// @brief 当前引用计数
    int refCount() const { return m_refCount; }

signals:
    /// @brief stdout 有新数据可读
    void readyReadStandardOutput(const QByteArray& data);

    /// @brief stderr 有新数据可读
    void readyReadStandardError(const QByteArray& data);

    /// @brief 进程已退出
    void finished(int exitCode, bool crashed);

private:
    /// @brief 安全创建进程对象（RAII包装）
    std::unique_ptr<QProcess> createProcess();

    /// @brief 安全销毁进程对象（先断开信号再删除）
    void destroyProcess();

    std::unique_ptr<QProcess> m_process;  // RAII：智能指针管理
    ShellType m_shellType = ShellType::CMD;
    int m_refCount = 1;  // P2-H01: 引用计数（创建者持有1，克隆时 addRef，关闭时 release）
};

#endif // TERMINALBACKEND_H
