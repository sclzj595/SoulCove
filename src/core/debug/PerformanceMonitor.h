#ifndef PERFORMANCEMONITOR_H
#define PERFORMANCEMONITOR_H

#include <QObject>
#include <QElapsedTimer>
#include <QMap>
#include <QHash>
#include <QMutex>
#include <QString>

/// @brief 性能监控器（调试模式）— C02-4
///
/// 设计目标：
///   1. 提供轻量级的操作耗时统计（基于 QElapsedTimer）
///   2. 仅在 performanceMonitorEnabled() == true 时记录 trace，
///      否则 startTrace/endTrace 立即返回，避免影响生产性能
///   3. 线程安全单例（C++11 Magic Statics + QMutex 保护统计区）
///
/// 使用方式：
///   PerformanceMonitor::instance().startTrace("onLspGotoDefinition");
///   // ... 待测量代码 ...
///   qint64 ms = PerformanceMonitor::instance().endTrace("onLspGotoDefinition");
///
///   // 弹出统计报告
///   QString report = PerformanceMonitor::instance().summaryReport();
class PerformanceMonitor : public QObject
{
    Q_OBJECT

public:
    /// 获取全局单例实例（C++11 Magic Statics 保证线程安全）
    static PerformanceMonitor& instance();

    // 禁止拷贝和赋值
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    /// @brief 开始计时（仅当启用时记录）
    /// @param tag 操作标签（如 "onLspGotoDefinition"）
    /// @note 未启用时立即返回，避免影响生产性能
    void startTrace(const QString& tag);

    /// @brief 结束计时并返回耗时（毫秒）
    /// @param tag 操作标签（必须与 startTrace 配对）
    /// @return 耗时（ms）；未启用或找不到起始时间返回 0
    qint64 endTrace(const QString& tag);

    /// @brief 清空所有统计（不影响启用状态）
    void reset();

    /// @brief 返回 tag→平均耗时(ms)
    QMap<QString, qint64> statistics() const;

    /// @brief 返回 tag→调用次数
    QMap<QString, int> callCounts() const;

    /// @brief 生成可读的统计报告字符串
    /// 格式：
    ///   === Performance Monitor Report ===
    ///   [tag]  calls=N  avg=Xms  total=Yms
    ///   ...
    QString summaryReport() const;

private:
    PerformanceMonitor();
    ~PerformanceMonitor() override = default;

    // 每个 tag 对应一个独立的计时器（支持嵌套调用同一 tag 的少见场景）
    QHash<QString, QElapsedTimer> m_timers;

    // 累计统计（受 m_mutex 保护）
    QHash<QString, qint64> m_totalMs;     // tag → 累计耗时
    QHash<QString, int>    m_callCounts;  // tag → 调用次数

    mutable QMutex m_mutex;
};

#endif // PERFORMANCEMONITOR_H
