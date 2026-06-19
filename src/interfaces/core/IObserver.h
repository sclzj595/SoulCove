#ifndef IOBSERVER_H
#define IOBSERVER_H

#include <QString>
#include <QVariant>

/// @brief 观察者模式 - 观察者接口
/// 数据变更时通知观察者更新UI，解耦数据层与视图层
class IObserver
{
public:
    virtual ~IObserver() = default;

    /// 数据变更通知回调
    virtual void onUpdate(const QString& event, const QVariant& data = QVariant()) = 0;
};

/// @brief 观察者模式 - 被观察主题接口
class ISubject
{
public:
    virtual ~ISubject() = default;

    /// 注册观察者
    virtual void attachObserver(IObserver* observer, const QString& eventFilter = QString()) = 0;

    /// 移除观察者
    virtual void detachObserver(IObserver* observer) = 0;

    /// 通知所有观察者
    virtual void notifyObservers(const QString& event, const QVariant& data = QVariant()) = 0;
};

#endif // IOBSERVER_H
