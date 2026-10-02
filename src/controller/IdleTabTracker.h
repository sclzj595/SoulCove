#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QString>
#include <QDateTime>

class ITabWidget;

/// @file IdleTabTracker.h
/// @brief 闲置标签页追踪器 — 观察者模式 + 定时轮询
///
/// 设计模式：观察者模式（Observer）
/// 职责：追踪非当前标签页的闲置时间，超时后通知订阅者释放/恢复 LSP 文档
///
/// 解耦原则（SRP）：
/// - EditorTabBar 只负责 UI 展示和标签页管理，不关心 LSP 文档生命周期
/// - IdleTabTracker 只负责闲置时间追踪，不关心释放后做什么
/// - LspCoordinator 订阅信号后自行决定 didClose/didOpen
///
/// 信号流：
///   IdleTabTracker::fileIdle(filePath) → LspCoordinator::closeFile()
///   IdleTabTracker::fileReactivated(filePath) → LspCoordinator::openFile()

/// @brief 闲置标签页追踪器
class IdleTabTracker : public QObject
{
    Q_OBJECT

public:
    /// @brief 构造函数
    /// @param tabBar 被观察的标签页组件（通过 ITabWidget 接口解耦）
    /// @param parent 父对象
    explicit IdleTabTracker(ITabWidget* tabBar, QObject* parent = nullptr);

    /// @brief 设置闲置阈值（毫秒）
    /// @param ms 闲置时间阈值，默认 30000（30秒）
    void setIdleThresholdMs(int ms) { m_idleThresholdMs = ms; }

    /// @brief 设置检查间隔（毫秒）
    /// @param ms 检查间隔，默认 10000（10秒）
    void setCheckIntervalMs(int ms) { m_checkIntervalMs = ms; }

    /// @brief 标记文件为已释放（LSP didClose 已发送）
    /// 由 LspCoordinator 调用，避免重复释放
    void markAsReleased(const QString& filePath);

    /// @brief 标记文件为已恢复（LSP didOpen 已发送）
    /// 由 LspCoordinator 调用
    void markAsReactivated(const QString& filePath);

    /// @brief 检查文件是否已被释放
    bool isReleased(const QString& filePath) const;

signals:
    /// @brief 文件闲置超时（非当前标签闲置超过阈值）
    /// LspCoordinator 收到后发送 LSP didClose 释放文档内存
    void fileIdle(const QString& filePath);

    /// @brief 闲置文件被重新激活（用户切换回该标签）
    /// LspCoordinator 收到后发送 LSP didOpen 重新打开文档
    /// @param filePath 文件路径
    /// @param content 文件当前内容（从编辑器获取，避免 LspManager 读磁盘）
    void fileReactivated(const QString& filePath, const QString& content);

private slots:
    /// 定时检查闲置标签页
    void onIdleCheck();

private:
    ITabWidget* m_tabBar;
    QTimer m_idleTimer;
    int m_idleThresholdMs = 30000;   // 闲置阈值：30秒
    int m_checkIntervalMs = 10000;   // 检查间隔：10秒

    /// 文件路径 → 最后访问时间
    QHash<QString, QDateTime> m_lastAccessMap;
    /// 已被 LSP didClose 释放的文件集合
    QSet<QString> m_releasedFiles;
};
