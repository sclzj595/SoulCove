#include "core/base/Subject.h"

void Subject::attachObserver(IObserver* observer, const QString& eventFilter)
{
    if (!observer) return;
    QString key = eventFilter.isEmpty() ? QStringLiteral("*") : eventFilter;
    m_observers[key].append(observer);
}

void Subject::detachObserver(IObserver* observer)
{
    if (!observer) return;
    for (auto it = m_observers.begin(); it != m_observers.end(); ++it) {
        it->removeAll(observer);
    }
}

void Subject::notifyObservers(const QString& event, const QVariant& data)
{
    // 通知全局观察者（key为"*"）
    auto globalIt = m_observers.find(QStringLiteral("*"));
    if (globalIt != m_observers.end()) {
        for (IObserver* obs : *globalIt) {
            obs->onUpdate(event, data);
        }
    }

    // 通知特定事件的观察者
    auto eventIt = m_observers.find(event);
    if (eventIt != m_observers.end()) {
        for (IObserver* obs : *eventIt) {
            obs->onUpdate(event, data);
        }
    }
}
