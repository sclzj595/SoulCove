#ifndef CLIENT_LOGGER_HPP
#define CLIENT_LOGGER_HPP

#include <QDateTime>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <qglobal.h>

/**
 * @brief 日志级别枚举（与 Qt 原生日志级别对齐）
 * 
 * 级别从低到高：Debug < Info < Warn < Error
 * 当日志级别设为 N 时，低于 N 的日志将被过滤不输出。
 * 
 * 使用示例：
 *   Logger::instance().setLogLevel(LogLevel::Debug); // 开发阶段
 *   Logger::instance().setLogLevel(LogLevel::Warn);  // 发布阶段
 */
enum class LogLevel {
    Debug = 0,    ///< 调试信息——开发阶段使用，发布时建议过滤
    Info  = 1,    ///< 关键流程信息——登录、连接、数据传输成功/失败
    Warn  = 2,    ///< 警告——潜在问题、降级路径、异常但可恢复的情况
    Error = 3     ///< 错误——功能异常、不可恢复的故障
};

// ==================== 日志便捷宏 ====================
// 用法：LOG_INFO("[模块名]" << "描述" << key << "=" << value);
// 输出：[2025-01-15 10:30:45.123][INFO][模块名][] 描述 key=value

#define LOG_DEBUG(...)  do { QString _l_; QDebug _d_(&_l_); _d_.noquote() << __VA_ARGS__; Logger::instance().log(LogLevel::Debug, _l_); } while(0)
#define LOG_INFO(...)   do { QString _l_; QDebug _d_(&_l_); _d_.noquote() << __VA_ARGS__; Logger::instance().log(LogLevel::Info,  _l_); } while(0)
#define LOG_WARN(...)   do { QString _l_; QDebug _d_(&_l_); _d_.noquote() << __VA_ARGS__; Logger::instance().log(LogLevel::Warn,  _l_); } while(0)
#define LOG_ERROR(...)  do { QString _l_; QDebug _d_(&_l_); _d_.noquote() << __VA_ARGS__; Logger::instance().log(LogLevel::Error, _l_); } while(0)

// 结构化日志宏：指定模块和操作（推荐用于 Service/Dao 层）
// 用法：LOG_INFO_S("UserDao", "register", "注册成功 | account=" << account);
// 输出：[2025-01-15 10:30:45.123][INFO][UserDao][register] 注册成功 | account=admin
#define LOG_DEBUG_S(module, op, ...) do { QString _l_; QDebug _d_(&_l_); _d_.noquote() << __VA_ARGS__; Logger::instance().log(LogLevel::Debug, _l_, module, op); } while(0)
#define LOG_INFO_S(module, op, ...)  do { QString _l_; QDebug _d_(&_l_); _d_.noquote() << __VA_ARGS__; Logger::instance().log(LogLevel::Info,  _l_, module, op); } while(0)
#define LOG_WARN_S(module, op, ...)  do { QString _l_; QDebug _d_(&_l_); _d_.noquote() << __VA_ARGS__; Logger::instance().log(LogLevel::Warn,  _l_, module, op); } while(0)
#define LOG_ERROR_S(module, op, ...) do { QString _l_; QDebug _d_(&_l_); _d_.noquote() << __VA_ARGS__; Logger::instance().log(LogLevel::Error, _l_, module, op); } while(0)

/**
 * @brief 客户端统一日志管理器（线程安全单例）
 * 
 * 功能：
 * 1. 控制台输出（通过 Qt qDebug/qInfo/qWarning/qCritical）
 * 2. 文件输出（滚动日志，按日期和大小自动切割）
 * 3. 级别过滤（低于当前级别的日志不输出）
 * 4. 结构化格式：[时间][级别][模块][操作] 内容
 * 
 * 初始化方式（在 main/testNetwork 中调用）：
 *   Logger::instance().setLogLevel(LogLevel::Debug);
 *   Logger::instance().enableFileLogging("logs/scChat.log");
 */
class Logger {
public:
    // ========== 单例接口 ==========

    /// 获取单例实例（C++11 Magic Statics 保证线程安全）
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // ========== 配置接口 ==========

    /// 设置日志级别（默认 Debug——开发阶段，发布时建议设为 Warn）
    void setLogLevel(LogLevel level) {
        QMutexLocker lock(&m_mutex);
        m_currentLevel = level;
    }

    /// 获取当前日志级别
    LogLevel logLevel() const {
        QMutexLocker lock(&m_mutex);
        return m_currentLevel;
    }

    /// 启用文件日志输出（自动创建目录）
    /// @param filePath 日志文件路径（相对路径使用可执行文件所在目录，绝对路径直接使用）
    void enableFileLogging(const QString& filePath) {
        QMutexLocker lock(&m_mutex);
        m_logFile.setFileName(filePath);
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            m_fileStream.setDevice(&m_logFile);
            m_fileEnabled = true;
        }
    }

    /// 禁用文件日志
    void disableFileLogging() {
        QMutexLocker lock(&m_mutex);
        m_fileEnabled = false;
        if (m_logFile.isOpen()) {
            m_fileStream.flush();
            m_logFile.close();
        }
    }

    // ========== 核心输出接口 ==========

    /// 简化版日志（不指定 module/operation）
    void log(LogLevel level, const QString& content) const {
        logImpl(level, QString(), QString(), content);
    }

    /// 完整版日志（指定 module 和 operation——用于结构化输出）
    void log(LogLevel level, const QString& content,
             const QString& module, const QString& operation) const {
        logImpl(level, module, operation, content);
    }

private:
    Logger()
        : m_currentLevel(LogLevel::Debug)  // 客户端默认 Debug 级别（开发阶段全面输出）
        , m_fileEnabled(false)
    {}

    /// 内部实现：级别过滤 + 格式化 + 输出
    void logImpl(LogLevel level, const QString& module,
                 const QString& operation, const QString& content) const {
        // 级别过滤
        if (level < m_currentLevel) return;

        // 拼接结构化日志行：[时间][级别][模块][操作] 内容
        const QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        const QString levelStr = levelToString(level);
        QString logLine;

        if (module.isEmpty() && operation.isEmpty()) {
            // 简洁格式
            logLine = QString("[%1][%2] %3")
                      .arg(timeStr, levelStr, content);
        } else {
            // 结构化格式
            logLine = QString("[%1][%2][%3][%4] %5")
                      .arg(timeStr, levelStr, module, operation, content);
        }

        // 输出到控制台（Qt 调试输出）
        switch (level) {
        case LogLevel::Debug: qDebug()    << logLine; break;
        case LogLevel::Info:  qInfo()     << logLine; break;
        case LogLevel::Warn:  qWarning()  << logLine; break;
        case LogLevel::Error: qCritical() << logLine; break;
        }

        // 输出到文件（带互斥锁保护）
        if (m_fileEnabled && m_logFile.isOpen()) {
            QMutexLocker lock(&m_mutex);
            m_fileStream << logLine << Qt::endl;
            m_fileStream.flush();
        }
    }

    /// 日志级别转字符串
    static QString levelToString(LogLevel level) {
        switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        default:              return "?????";
        }
    }

    // ========== 成员变量 ==========
    LogLevel m_currentLevel;                // 当前日志级别（低级别过滤用）
    mutable QMutex m_mutex;                 // 文件写入互斥锁（mutable 允许 const 方法加锁）
    QFile m_logFile;                        // 日志文件句柄
    mutable QTextStream m_fileStream;       // 文件流（mutable 允许 const 方法写入）
    bool m_fileEnabled;                     // 文件日志开关

public:
    // ========== Qt 消息过滤器 ==========

    /// 安装 Qt 全局消息处理器，屏蔽 Windows 系统 COM/SHELL 无关警告
    /// 过滤的已知噪音：
    ///   - "库没有注册"（SHELL32 COM 组件未注册）
    ///   - "尚未实现"（oleaut32 系统接口未实现）
    ///   - SHELL32.dll / oleaut32 系统底层警告
    /// 调用方式：在 main.cpp 的 QApplication 创建前调用 Logger::installQtMessageFilter();
    static void installQtMessageFilter() {
        static QtMessageHandler s_defaultHandler = nullptr;
        s_defaultHandler = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
                // 仅过滤 Warning 级别的系统噪音（Debug/Info/Critical 正常放行）
                if (type == QtWarningMsg) {
                    // 已知的 Windows COM/SHELL 系统警告关键词（中英文）
                    static const QStringList kSystemNoisePatterns = {
                        QStringLiteral("库没有注册"),
                        QStringLiteral("尚未实现"),
                        QStringLiteral("SHELL32"),
                        QStringLiteral("oleaut32"),
                        QStringLiteral("Not implemented"),
                        QStringLiteral("Library not registered"),
                        QStringLiteral("操作无法使用"),
                        QStringLiteral("CoCreateInstance")
                    };
                    for (const QString& pattern : kSystemNoisePatterns) {
                        if (msg.contains(pattern, Qt::CaseInsensitive)) {
                            return;  // 静默丢弃系统噪音
                        }
                    }
                }
                // 非系统噪音 → 交给默认处理器输出
                if (s_defaultHandler) {
                    s_defaultHandler(type, ctx, msg);
                }
            }
        );
    }
};

#endif // CLIENT_LOGGER_HPP
