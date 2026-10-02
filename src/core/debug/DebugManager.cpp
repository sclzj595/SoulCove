#include "core/debug/DebugManager.h"
#include "core/config/ConfigManager.h"  // P3-M04 子项3: debuggerPath
#include "Logger.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ============================================================
// P3-M04 子项3: DebugManager (GDB MI 接口驱动)
// ============================================================
//
// 实现说明：
// - 通过 QProcess 启动 gdb --interpreter=mi
// - MI 协议基础：每行响应为 (gdb) 提示符 / ^done/^error 结果记录 /
//   *stopped/=breakpoint-created 异步记录 / ~"& @" 流输出
// - 此实现采用「宽松解析」策略：仅提取调试 UI 所需核心字段
//   （停止位置/断点编号/变量名值），完整 MI 协议解析不在本任务范围
// - 调试目标程序使用 -exec-run 启动（在已加载程序 + 设置断点后）

DebugManager::DebugManager(QObject* parent)
    : QObject(parent)
{
}

DebugManager::~DebugManager()
{
    stopDebug();
}

// ============================================================
// 调试会话控制
// ============================================================

void DebugManager::startDebug(const QString& program, const QStringList& args, const QString& workingDir)
{
    if (m_state != DebugState::Stopped) {
        emit outputMessage(tr("[调试] 已有活跃调试会话，请先停止"));
        return;
    }
    if (program.isEmpty()) {
        emit outputMessage(tr("[调试] 未指定可执行文件"));
        return;
    }

    m_program = program;
    m_args = args;
    m_workingDir = workingDir;

    // 解析 gdb 路径（默认 "gdb"，可通过 ConfigManager 配置）
    QString gdbPath = ConfigManager::instance().debuggerPath();
    if (gdbPath.isEmpty()) {
        gdbPath = QStringLiteral("gdb");
    }

    // 创建 GDB 进程
    if (!m_gdb) {
        m_gdb = new QProcess(this);
        connect(m_gdb, &QProcess::readyReadStandardOutput,
                this, &DebugManager::onGdbReadyReadStandardOutput);
        connect(m_gdb, &QProcess::readyReadStandardError,
                this, &DebugManager::onGdbReadyReadStandardError);
        connect(m_gdb, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &DebugManager::onGdbFinished);
        connect(m_gdb, &QProcess::errorOccurred,
                this, &DebugManager::onGdbErrorOccurred);
    }

    // 设置工作目录
    QString workDir = workingDir.isEmpty() ? QCoreApplication::applicationDirPath() : workingDir;
    m_gdb->setWorkingDirectory(workDir);

    // 启动 gdb（MI 模式）
    QStringList gdbArgs = {
        QStringLiteral("--interpreter=mi"),
        QStringLiteral("--quiet")
    };
    emit outputMessage(tr("[调试] 启动 GDB: %1 %2").arg(gdbPath, gdbArgs.join(QLatin1Char(' '))));
    m_gdb->start(gdbPath, gdbArgs);

    if (!m_gdb->waitForStarted(3000)) {
        emit outputMessage(tr("[调试] GDB 启动失败，请检查 debuggerPath 配置: %1").arg(gdbPath));
        // 错误细节由 errorOccurred 槽进一步输出
        return;
    }

    setState(DebugState::Running);

    // === 初始化序列 ===
    // 1) 设置被调试程序
    sendMiCommand(QStringLiteral("-file-exec-and-symbols \"") + program + QStringLiteral("\""));
    // 2) 设置参数（如果有）
    if (!args.isEmpty()) {
        sendMiCommand(QStringLiteral("-exec-arguments ") + args.join(QLatin1Char(' ')));
    }
    // 3) 重新设置已注册断点（会话级恢复）
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        for (int line : it.value()) {
            sendMiCommand(QStringLiteral("-break-insert ") +
                          QStringLiteral("\"") + it.key() + QStringLiteral(":") +
                          QString::number(line) + QStringLiteral("\""));
        }
    }
    // 4) 启动被调试程序（-exec-run 会触发 GDB 加载并运行至首个断点或退出）
    sendMiCommand(QStringLiteral("-exec-run"));
}

void DebugManager::stopDebug()
{
    if (m_gdb && m_gdb->state() != QProcess::NotRunning) {
        // 优雅退出 GDB
        sendMiCommand(QStringLiteral("-gdb-exit"));
        if (!m_gdb->waitForFinished(2000)) {
            m_gdb->kill();
            m_gdb->waitForFinished(1500);
        }
    }
    setState(DebugState::Stopped);
}

// ============================================================
// 断点管理
// ============================================================

void DebugManager::setBreakpoint(const QString& file, int line)
{
    if (file.isEmpty() || line < 1) return;

    // 去重
    auto& set = m_breakpoints[file];
    if (set.contains(line)) return;
    set.insert(line);

    // 若 GDB 活跃，立即下发
    if (m_gdb && m_gdb->state() == QProcess::Running) {
        sendMiCommand(QStringLiteral("-break-insert \"") + file + QStringLiteral(":") +
                      QString::number(line) + QStringLiteral("\""));
    }
}

void DebugManager::removeBreakpoint(const QString& file, int line)
{
    auto it = m_breakpoints.find(file);
    if (it == m_breakpoints.end()) return;
    if (!it.value().remove(line)) return;
    if (it.value().isEmpty()) m_breakpoints.erase(it);

    // GDB 不支持按 file:line 直接删除，需先 -break-list 取得编号再 -break-delete
    // 简化处理：发送 -break-delete 配合已知编号；此处用 Clear + Re-insert 策略
    // （基础版本：仅在 GDB 活跃时通过 -break-delete 全清后重设剩余断点）
    if (m_gdb && m_gdb->state() == QProcess::Running) {
        sendMiCommand(QStringLiteral("-break-delete"));
        for (auto it2 = m_breakpoints.constBegin(); it2 != m_breakpoints.constEnd(); ++it2) {
            for (int ln : it2.value()) {
                sendMiCommand(QStringLiteral("-break-insert \"") + it2.key() +
                              QStringLiteral(":") + QString::number(ln) + QStringLiteral("\""));
            }
        }
    }
}

void DebugManager::clearBreakpoints()
{
    m_breakpoints.clear();
    if (m_gdb && m_gdb->state() == QProcess::Running) {
        sendMiCommand(QStringLiteral("-break-delete"));
    }
}

// ============================================================
// 执行控制
// ============================================================

void DebugManager::continueExecution()
{
    if (m_state != DebugState::Paused) return;
    sendMiCommand(QStringLiteral("-exec-continue"));
    setState(DebugState::Running);
}

void DebugManager::stepOver()
{
    if (m_state != DebugState::Paused) return;
    sendMiCommand(QStringLiteral("-exec-next"));
    setState(DebugState::Running);
}

void DebugManager::stepInto()
{
    if (m_state != DebugState::Paused) return;
    sendMiCommand(QStringLiteral("-exec-step"));
    setState(DebugState::Running);
}

void DebugManager::stepOut()
{
    if (m_state != DebugState::Paused) return;
    sendMiCommand(QStringLiteral("-exec-finish"));
    setState(DebugState::Running);
}

void DebugManager::runToCursor(const QString& file, int line)
{
    if (m_state != DebugState::Paused) return;
    // 临时断点 + continue（不加入 m_breakpoints 集合，避免 UI 显示）
    sendMiCommand(QStringLiteral("-break-insert -t \"") + file + QStringLiteral(":") +
                  QString::number(line) + QStringLiteral("\""));
    sendMiCommand(QStringLiteral("-exec-continue"));
    setState(DebugState::Running);
}

// ============================================================
// GDB 输出处理
// ============================================================

void DebugManager::onGdbReadyReadStandardOutput()
{
    if (!m_gdb) return;
    QByteArray data = m_gdb->readAllStandardOutput();
    m_pendingOutput.append(QString::fromUtf8(data));

    // 按 \n 分行处理
    int idx = -1;
    while ((idx = m_pendingOutput.indexOf(QLatin1Char('\n'))) != -1) {
        QString line = m_pendingOutput.left(idx);
        m_pendingOutput = m_pendingOutput.mid(idx + 1);
        if (!line.isEmpty()) {
            processMiLine(line);
        }
    }
}

void DebugManager::onGdbReadyReadStandardError()
{
    if (!m_gdb) return;
    QByteArray data = m_gdb->readAllStandardError();
    QString text = QString::fromUtf8(data);
    if (!text.isEmpty()) {
        emit outputMessage(QStringLiteral("[GDB stderr] ") + text.trimmed());
    }
}

void DebugManager::onGdbFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    LOG_DEBUG_S("DebugManager", "onGdbFinished", "GDB 进程结束 exitCode=" << exitCode);
    setState(DebugState::Stopped);
    emit outputMessage(tr("[调试] GDB 会话已结束 (退出码 %1)").arg(exitCode));
}

void DebugManager::onGdbErrorOccurred(QProcess::ProcessError error)
{
    QString msg;
    switch (error) {
    case QProcess::FailedToStart: msg = tr("GDB 启动失败（请检查路径: %1）")
                                             .arg(ConfigManager::instance().debuggerPath());
        break;
    case QProcess::Crashed:        msg = tr("GDB 进程崩溃"); break;
    case QProcess::Timedout:       msg = tr("GDB 操作超时"); break;
    default:                       msg = tr("GDB 未知错误"); break;
    }
    LOG_WARN_S("DebugManager", "onGdbErrorOccurred", msg);
    emit outputMessage(QStringLiteral("[调试] ") + msg);
    setState(DebugState::Stopped);
}

// ============================================================
// MI 协议解析
// ============================================================

void DebugManager::setState(DebugState newState)
{
    if (m_state == newState) return;
    m_state = newState;
    emit stateChanged(m_state);
}

void DebugManager::sendMiCommand(const QString& miCmd)
{
    if (!m_gdb || m_gdb->state() != QProcess::Running) {
        LOG_DEBUG_S("DebugManager", "sendMiCommand", "GDB 未运行，丢弃命令:" << miCmd);
        return;
    }
    QByteArray data = (miCmd + QStringLiteral("\n")).toUtf8();
    m_gdb->write(data);
    LOG_DEBUG_S("DebugManager", "sendMiCommand", "→" << miCmd);
}

void DebugManager::processMiLine(const QString& line)
{
    QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return;

    // (gdb) 提示符
    if (trimmed == QStringLiteral("(gdb)")) return;

    // 流输出记录：~"..." 控制台 / @"..." 目标 / &"..." 日志
    if (trimmed.startsWith(QLatin1Char('~'))) {
        // 提取引号内容
        int firstQuote = trimmed.indexOf(QLatin1Char('"'));
        int lastQuote  = trimmed.lastIndexOf(QLatin1Char('"'));
        if (firstQuote >= 0 && lastQuote > firstQuote) {
            QString text = trimmed.mid(firstQuote + 1, lastQuote - firstQuote - 1);
            handleConsoleStream(text);
        }
        return;
    }
    if (trimmed.startsWith(QLatin1Char('@'))) {
        int firstQuote = trimmed.indexOf(QLatin1Char('"'));
        int lastQuote  = trimmed.lastIndexOf(QLatin1Char('"'));
        if (firstQuote >= 0 && lastQuote > firstQuote) {
            QString text = trimmed.mid(firstQuote + 1, lastQuote - firstQuote - 1);
            handleTargetStream(text);
        }
        return;
    }
    if (trimmed.startsWith(QLatin1Char('&'))) {
        int firstQuote = trimmed.indexOf(QLatin1Char('"'));
        int lastQuote  = trimmed.lastIndexOf(QLatin1Char('"'));
        if (firstQuote >= 0 && lastQuote > firstQuote) {
            QString text = trimmed.mid(firstQuote + 1, lastQuote - firstQuote - 1);
            handleLogStream(text);
        }
        return;
    }

    // 异步执行记录：*stopped / *running
    if (trimmed.startsWith(QStringLiteral("*stopped"))) {
        // *stopped,reason="breakpoint-hit",frame={file="x.cpp",line="10",...}
        QString body = trimmed.mid(QString(QStringLiteral("*stopped")).length());
        if (body.startsWith(QLatin1Char(','))) body = body.mid(1);
        handleStopped(body);
        return;
    }
    if (trimmed.startsWith(QStringLiteral("*running"))) {
        setState(DebugState::Running);
        return;
    }

    // 状态变更记录：=breakpoint-created / =breakpoint-deleted / =thread-group-exited
    if (trimmed.startsWith(QStringLiteral("=breakpoint-created"))) {
        QString body = trimmed.mid(QString(QStringLiteral("=breakpoint-created")).length());
        if (body.startsWith(QLatin1Char(','))) body = body.mid(1);
        handleBreakpointCreated(body);
        return;
    }
    if (trimmed.startsWith(QStringLiteral("=thread-group-exited"))) {
        // 被调试程序退出
        emit outputMessage(tr("[调试] 调试目标已退出"));
        setState(DebugState::Stopped);
        return;
    }

    // 结果记录：^done / ^running / ^error / ^exit
    if (trimmed.startsWith(QLatin1Char('^')) ||
        trimmed.startsWith(QChar::fromLatin1('0')) ||
        (trimmed.size() > 0 && trimmed.at(0).isDigit())) {
        handleResultRecord(trimmed);
        return;
    }

    // 其他未识别行作为通用输出
    LOG_DEBUG_S("DebugManager", "processMiLine", "未识别 MI 行:" << trimmed);
}

void DebugManager::handleStopped(const QString& recordBody)
{
    // 提取 reason / frame 中的 file / line / func
    QString reason = extractMiField(recordBody, QStringLiteral("reason"));

    QString file = extractMiField(recordBody, QStringLiteral("file"));
    if (file.isEmpty()) {
        file = extractMiField(recordBody, QStringLiteral("fullname"));
    }
    QString lineStr = extractMiField(recordBody, QStringLiteral("line"));
    QString func    = extractMiField(recordBody, QStringLiteral("func"));

    int lineNum = lineStr.toInt();

    setState(DebugState::Paused);

    // 触发命中信号（即使无 reason 也发射位置）
    if (!file.isEmpty() && lineNum > 0) {
        emit breakpointHit(file, lineNum);
    }

    QString reasonText;
    if (reason == QStringLiteral("breakpoint-hit")) {
        reasonText = tr("断点命中");
    } else if (reason == QStringLiteral("end-stepping-range")) {
        reasonText = tr("单步停止");
    } else if (reason == QStringLiteral("function-finished")) {
        reasonText = tr("单步跳出完成");
    } else if (reason == QStringLiteral("exited-normally") || reason == QStringLiteral("exited")) {
        reasonText = tr("程序退出");
        emit outputMessage(tr("[调试] 程序正常退出"));
        return;
    } else if (reason == QStringLiteral("signal-received")) {
        reasonText = tr("收到信号");
    } else {
        reasonText = reason.isEmpty() ? tr("已停止") : reason;
    }

    emit outputMessage(tr("[调试] %1 → %2:%3 (%4)").arg(reasonText, file).arg(lineNum).arg(func));

    // 自动请求变量列表
    requestVariables();
}

void DebugManager::handleBreakpointCreated(const QString& recordBody)
{
    // =breakpoint-created,bkpt={number="1",type="breakpoint",disp="keep",enabled="y",
    //                            addr="...",func="...",file="...",fullname="...",line="10",...}
    QString number = extractMiField(recordBody, QStringLiteral("number"));
    QString file   = extractMiField(recordBody, QStringLiteral("file"));
    if (file.isEmpty()) {
        file = extractMiField(recordBody, QStringLiteral("fullname"));
    }
    QString line   = extractMiField(recordBody, QStringLiteral("line"));

    LOG_DEBUG_S("DebugManager", "handleBreakpointCreated",
                "断点已创建 #" << number << file << ":" << line);
    Q_UNUSED(number)
}

void DebugManager::handleConsoleStream(const QString& text)
{
    // GDB 控制台输出（~"..."），通常是 GDB 自身消息
    emit outputMessage(unescapeMiString(text));
}

void DebugManager::handleTargetStream(const QString& text)
{
    // 调试目标 stdout（@"..."）
    emit outputMessage(unescapeMiString(text));
}

void DebugManager::handleLogStream(const QString& text)
{
    // GDB 日志（&"..."），通常为命令镜像，日志级别最低
    LOG_DEBUG_S("DebugManager", "handleLogStream", unescapeMiString(text));
}

void DebugManager::handleResultRecord(const QString& line)
{
    // 简化处理：仅识别关键结果
    // ^done / ^running / ^error / ^exit
    // ^done 后可能附带 variables 列表（针对 -stack-list-variables）

    if (line.startsWith(QStringLiteral("^error"))) {
        // 提取 msg="..."
        QString msg = extractMiField(line, QStringLiteral("msg"));
        if (!msg.isEmpty()) {
            emit outputMessage(tr("[GDB 错误] ") + unescapeMiString(msg));
        }
        return;
    }

    if (line.startsWith(QStringLiteral("^exit"))) {
        setState(DebugState::Stopped);
        return;
    }

    // 处理 -stack-list-variables 的结果
    // 格式（一行或多行）：^done,variables=[{name="x",value="1",type="int"},...]
    if (line.contains(QStringLiteral("variables=["))) {
        QVariantList vars;
        int vidx = line.indexOf(QStringLiteral("variables=["));
        if (vidx >= 0) {
            // 简易解析：用正则匹配每个 {name="...",value="...",type="..."}
            static const QRegularExpression varRe(
                QStringLiteral("\\{name=\"([^\"]*)\",value=\"([^\"]*)\"(?:,type=\"([^\"]*)\")?\\}"));
            auto it = varRe.globalMatch(line);
            while (it.hasNext()) {
                QRegularExpressionMatch m = it.next();
                QVariantMap vm;
                vm.insert(QStringLiteral("name"),  unescapeMiString(m.captured(1)));
                vm.insert(QStringLiteral("value"), unescapeMiString(m.captured(2)));
                vm.insert(QStringLiteral("type"),  unescapeMiString(m.captured(3)));
                vars.append(vm);
            }
        }
        emit variablesReady(vars);
    }
}

void DebugManager::requestVariables()
{
    // 列出当前栈帧的简单变量（--simple-values: 仅打印 name + value，不展开复合类型）
    sendMiCommand(QStringLiteral("-stack-list-variables --simple-values"));
}

// ============================================================
// MI 字符串辅助
// ============================================================

QString DebugManager::extractMiField(const QString& src, const QString& fieldName)
{
    // 简易提取：匹配 fieldName="..." 模式
    // 注意：此实现不处理嵌套结构（如 frame={...}），仅扁平字段
    QString pattern = fieldName + QStringLiteral("=\"([^\"]*)\"");
    QRegularExpression re(pattern);
    QRegularExpressionMatch m = re.match(src);
    if (m.hasMatch()) {
        return m.captured(1);
    }
    return QString();
}

QString DebugManager::unescapeMiString(const QString& s)
{
    // 反转义 MI 字符串字面量（\" \n \t \\ 等）
    QString out;
    out.reserve(s.size());
    bool escape = false;
    for (const QChar& ch : s) {
        if (escape) {
            switch (ch.unicode()) {
            case 'n':  out.append(QLatin1Char('\n')); break;
            case 't':  out.append(QLatin1Char('\t')); break;
            case 'r':  out.append(QLatin1Char('\r')); break;
            case '\\': out.append(QLatin1Char('\\')); break;
            case '"':  out.append(QLatin1Char('"'));  break;
            default:   out.append(ch); break;
            }
            escape = false;
        } else if (ch == QLatin1Char('\\')) {
            escape = true;
        } else {
            out.append(ch);
        }
    }
    return out;
}
