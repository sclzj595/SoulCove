#include "IdleTabTracker.h"
#include "interfaces/ui/ITabWidget.h"
#include "interfaces/editor/IEditorEdit.h"
#include "Logger.hpp"

#include <QFileInfo>

// ============================================================
// IdleTabTracker — 闲置标签页追踪器实现
// ============================================================

IdleTabTracker::IdleTabTracker(ITabWidget* tabBar, QObject* parent)
    : QObject(parent)
    , m_tabBar(tabBar)
{
    // 启动定时检查
    connect(&m_idleTimer, &QTimer::timeout, this, &IdleTabTracker::onIdleCheck);
    m_idleTimer.setInterval(m_checkIntervalMs);
    m_idleTimer.start();
}

void IdleTabTracker::markAsReleased(const QString& filePath)
{
    m_releasedFiles.insert(QDir::toNativeSeparators(filePath));
}

void IdleTabTracker::markAsReactivated(const QString& filePath)
{
    m_releasedFiles.remove(QDir::toNativeSeparators(filePath));
}

bool IdleTabTracker::isReleased(const QString& filePath) const
{
    return m_releasedFiles.contains(QDir::toNativeSeparators(filePath));
}

void IdleTabTracker::onIdleCheck()
{
    if (!m_tabBar) return;

    QString currentPath = m_tabBar->currentFilePath();
    QString currentNormalized = QDir::toNativeSeparators(currentPath);
    QDateTime now = QDateTime::currentDateTime();

    // 更新当前标签的访问时间
    if (!currentPath.isEmpty()) {
        m_lastAccessMap[currentNormalized] = now;

        // 如果当前标签之前被释放过，现在切回来了 → 发射 reactivated 信号
        if (m_releasedFiles.contains(currentNormalized)) {
            m_releasedFiles.remove(currentNormalized);
            IEditorEdit* editor = m_tabBar->currentEditor();
            QString content = editor ? editor->toPlainText() : QString();
            emit fileReactivated(currentPath, content);
            LOG_DEBUG("[IdleTabTracker] 标签重新激活: " << currentPath.toStdString());
        }
    }

    // 检查所有非当前标签的闲置时间
    for (const auto& pair : m_tabBar->allEditors()) {
        const QString& filePath = pair.first;
        if (filePath.isEmpty()) continue;

        QString normalized = QDir::toNativeSeparators(filePath);
        if (normalized == currentNormalized) continue;  // 跳过当前标签
        if (m_releasedFiles.contains(normalized)) continue;  // 已释放的跳过

        // 跳过有未保存修改的文件
        IEditorEdit* editor = pair.second;
        if (editor && editor->isModified()) continue;

        // 检查闲置时间
        auto it = m_lastAccessMap.constFind(normalized);
        if (it == m_lastAccessMap.constEnd()) {
            // 新打开的文件，记录访问时间
            m_lastAccessMap[normalized] = now;
            continue;
        }

        if (it.value().msecsTo(now) >= m_idleThresholdMs) {
            // 闲置超时 → 通知释放
            m_releasedFiles.insert(normalized);
            emit fileIdle(filePath);
            LOG_DEBUG("[IdleTabTracker] 标签闲置释放: " << filePath.toStdString());
        }
    }

    // 清理已关闭文件的记录（不在 allEditors 中的）
    QSet<QString> activeFiles;
    for (const auto& pair : m_tabBar->allEditors()) {
        if (!pair.first.isEmpty()) {
            activeFiles.insert(QDir::toNativeSeparators(pair.first));
        }
    }
    for (auto it = m_lastAccessMap.begin(); it != m_lastAccessMap.end(); ) {
        if (!activeFiles.contains(it.key())) {
            it = m_lastAccessMap.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_releasedFiles.begin(); it != m_releasedFiles.end(); ) {
        if (!activeFiles.contains(*it)) {
            it = m_releasedFiles.erase(it);
        } else {
            ++it;
        }
    }
}
