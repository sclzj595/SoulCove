#ifndef ICONFIGMANAGER_H
#define ICONFIGMANAGER_H

#include <QString>
#include <QVariant>

/// @brief 配置管理抽象接口
/// 定义参数读写、状态保存、默认配置加载纯虚函数
class IConfigManager
{
public:
    virtual ~IConfigManager() = default;

    /// 读取配置值
    virtual QVariant getValue(const QString& key, const QVariant& defaultValue = QVariant()) const = 0;

    /// 写入配置值
    virtual void setValue(const QString& key, const QVariant& value) = 0;

    /// 立即同步到磁盘
    virtual void sync() = 0;

    /// 加载所有配置
    virtual void loadAll() = 0;

    /// 保存所有配置
    virtual void saveAll() = 0;

    /// 检查配置键是否存在
    virtual bool contains(const QString& key) const = 0;
};

#endif // ICONFIGMANAGER_H
