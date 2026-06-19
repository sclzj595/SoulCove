#ifndef SUBJECT_H
#define SUBJECT_H

#include "interfaces/core/IObserver.h"
#include <QMap>
#include <QList>
#include <QString>
#include <QVariant>
#include <QObject>

/// @brief 被观察主题基类实现
/// 继承QObject以支持信号槽和对象树管理
/// 实现观察者注册/移除/通知机制，支持事件过滤
class Subject : public QObject, public ISubject
{
    Q_OBJECT

public:
    explicit Subject(QObject* parent = nullptr) : QObject(parent) {}
    ~Subject() override = default;

    void attachObserver(IObserver* observer, const QString& eventFilter = QString()) override;
    void detachObserver(IObserver* observer) override;
    void notifyObservers(const QString& event, const QVariant& data = QVariant()) override;

private:
    // 事件过滤器 -> 观察者列表（支持按事件类型过滤）
    QMap<QString, QList<IObserver*>> m_observers;
};

#endif // SUBJECT_H
