#include "ui/terminal/TerminalBackend.h"

#include <QProcessEnvironment>

// ============================================================
// 构造 / 析构 — RAII 资源管理
// ============================================================

TerminalBackend::TerminalBackend(QObject* parent)
    : QObject(parent)
    , m_process(nullptr)  // RAII：延迟初始化
{
}

TerminalBackend::~TerminalBackend()
{
    // RAII：析构时自动清理资源
    stop();
}

std::unique_ptr<QProcess> TerminalBackend::createProcess()
{
    // RAII：工厂方法创建进程对象
    auto process = std::make_unique<QProcess>(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    // 环境变量：禁用 Python 缓冲等
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));  // 告诉程序支持 ANSI
    process->setProcessEnvironment(env);

    return process;
}

void TerminalBackend::destroyProcess()
{
    // RAII：安全销毁（先断开信号再释放）
    if (!m_process) return;

    // ✅ R2修复：先断开所有信号连接，避免访问已删除对象
    m_process->disconnect();

    // 智能指针自动释放（RAII保证）
    m_process.reset();
}

bool TerminalBackend::start(ShellType type, const QString& workDir)
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        return false;  // 已在运行
    }

    m_shellType = type;

    // RAII：使用工厂方法创建新进程
    m_process = createProcess();

    QString program;
    QStringList args;

    if (type == ShellType::PowerShell) {
        program = QStringLiteral("powershell.exe");
        args = {
            QStringLiteral("-NoProfile"),     // 不加载配置文件（加速启动）
            QStringLiteral("-NoExit"),         // 执行完不退出
            QStringLiteral("-Command"),
            // 初始化命令：显示当前路径 + 配置 PSReadLine
            QStringLiteral("$host.UI.RawUI.WindowTitle = 'SoulCove'; ")
            + QStringLiteral("Write-Host ('PS ' + (Get-Location).Path + '> ') -NoNewline")
        };
    } else {
        program = QStringLiteral("cmd.exe");
        // CMD 初始化：简化命令链，避免管道模式下解析异常
        args = {
            QStringLiteral("/Q"),                          // 静默模式（不显示版本信息）
            QStringLiteral("/K"),                           // 执行后保持交互
            QStringLiteral("chcp 65001 >nul & prompt $P$G") // UTF-8 + 路径prompt
        };
    }

    if (!workDir.isEmpty()) {
        m_process->setWorkingDirectory(workDir);
    }

    // 连接信号（使用智能指针的get()获取原始指针）
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, [this]() {
        emit readyReadStandardOutput(m_process->readAllStandardOutput());
    });

    connect(m_process.get(), &QProcess::readyReadStandardError, this, [this]() {
        emit readyReadStandardError(m_process->readAllStandardError());
    });

    connect(m_process.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        emit finished(code, status == QProcess::CrashExit);
    });

    m_process->start(program, args);
    return m_process->waitForStarted(3000);
}

void TerminalBackend::stop()
{
    if (!m_process) return;

    // 先尝试优雅退出（在销毁前）
    if (m_process->state() == QProcess::Running) {
        if (m_shellType == ShellType::PowerShell) {
            m_process->write("exit\n");
        } else {
            m_process->write("exit\r\n");
        }

        if (!m_process->waitForFinished(2000)) {
            m_process->kill();
        }
    }

    // RAII：安全销毁（自动disconnect + 释放内存）
    destroyProcess();
}

void TerminalBackend::write(const QByteArray& data)
{
    if (!m_process || m_process->state() != QProcess::Running) return;
    m_process->write(data);
}

void TerminalBackend::sendInterrupt()
{
    if (!m_process || m_process->state() != QProcess::Running) return;
    // Windows: 发送 Ctrl+C (0x03) 到控制台输入
    m_process->write("\x03");
}

bool TerminalBackend::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

// ============================================================
// P2-H01: 引用计数（终端复用/克隆）
// ============================================================

void TerminalBackend::addRef()
{
    ++m_refCount;
}

void TerminalBackend::release()
{
    if (--m_refCount <= 0) {
        // 引用计数归零：销毁自身（析构函数会自动调用 stop() 优雅退出进程）
        delete this;
    }
}
