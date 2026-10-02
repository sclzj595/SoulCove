#include "core/debug/PerformanceMonitor.h"
#include "core/config/ConfigManager.h"
#include "Logger.hpp"

#include <QMutexLocker>
#include <QDateTime>

// ========== 单例实现 ==========

PerformanceMonitor& PerformanceMonitor::instance()
{
    static PerformanceMonitor s_instance;
    return s_instance;
}

PerformanceMonitor::PerformanceMonitor()
{
    LOG_DEBUG_S("PerformanceMonitor", "ctor", "性能监控器初始化（默认禁用，需通过配置开启）");
}

// ========== Trace 接口 ==========

void PerformanceMonitor::startTrace(const QString& tag)
{
    // 仅在启用时记录，避免影响生产性能
    if (!ConfigManager::instance().performanceMonitorEnabled()) {
        return;
    }

    QElapsedTimer t;
    t.start();
    m_timers.insert(tag, t);
}

qint64 PerformanceMonitor::endTrace(const QString& tag)
{
    // 未启用时立即返回
    if (!ConfigManager::instance().performanceMonitorEnabled()) {
        return 0;
    }

    auto it = m_timers.find(tag);
    if (it == m_timers.end()) {
        LOG_WARN_S("PerformanceMonitor", "endTrace",
                   "找不到起始计时器，tag=" << tag.toStdString());
        return 0;
    }

    const qint64 elapsed = it->elapsed();
    m_timers.erase(it);

    // 累计到统计区（线程安全）
    QMutexLocker locker(&m_mutex);
    m_totalMs[tag]    += elapsed;
    m_callCounts[tag] += 1;

    return elapsed;
}

// ========== 统计管理 ==========

void PerformanceMonitor::reset()
{
    QMutexLocker locker(&m_mutex);
    m_totalMs.clear();
    m_callCounts.clear();
    m_timers.clear();
    LOG_DEBUG_S("PerformanceMonitor", "reset", "性能统计已清空");
}

QMap<QString, qint64> PerformanceMonitor::statistics() const
{
    QMutexLocker locker(&m_mutex);
    QMap<QString, qint64> result;

    // 计算每个 tag 的平均耗时
    for (auto it = m_totalMs.constBegin(); it != m_totalMs.constEnd(); ++it) {
        const int count = m_callCounts.value(it.key(), 0);
        if (count > 0) {
            result.insert(it.key(), it.value() / count);
        } else {
            result.insert(it.key(), 0);
        }
    }
    return result;
}

QMap<QString, int> PerformanceMonitor::callCounts() const
{
    QMutexLocker locker(&m_mutex);
    QMap<QString, int> result;
    for (auto it = m_callCounts.constBegin(); it != m_callCounts.constEnd(); ++it) {
        result.insert(it.key(), it.value());
    }
    return result;
}

QString PerformanceMonitor::summaryReport() const
{
    QMutexLocker locker(&m_mutex);

    QString report;
    report += QStringLiteral("=== Performance Monitor Report ===\n");
    report += QStringLiteral("生成时间: ") +
              QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") +
              QStringLiteral("\n");
    report += QStringLiteral("----------------------------------\n");

    if (m_totalMs.isEmpty()) {
        report += QStringLiteral("(暂无统计数据)\n");
        return report;
    }

    // 按 tag 字典序输出
    QMap<QString, qint64> sortedTotal;
    for (auto it = m_totalMs.constBegin(); it != m_totalMs.constEnd(); ++it) {
        sortedTotal.insert(it.key(), it.value());
    }
    for (auto it = sortedTotal.constBegin(); it != sortedTotal.constEnd(); ++it) {
        const QString& tag   = it.key();
        const qint64   total = it.value();
        const int      count = m_callCounts.value(tag, 0);
        const qint64   avg   = (count > 0) ? (total / count) : 0;

        report += QStringLiteral("[%1]  calls=%2  avg=%3ms  total=%4ms\n")
                  .arg(tag)
                  .arg(count)
                  .arg(avg)
                  .arg(total);
    }

    report += QStringLiteral("==================================\n");
    return report;
}
