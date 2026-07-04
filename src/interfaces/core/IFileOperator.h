#ifndef IFILEOPERATOR_H
#define IFILEOPERATOR_H

#include <QString>
#include <functional>

class IObserver;

/// @brief 文件操作抽象接口
/// 定义新建、打开、保存、编码解析纯虚函数
class IFileOperator
{
public:
    virtual ~IFileOperator() = default;

    /// 打开文件
    virtual bool openFile(const QString& filePath) = 0;

    /// 保存文件
    virtual bool saveFile(const QString& filePath = QString()) = 0;

    /// 另存为
    virtual bool saveAsFile(const QString& filePath) = 0;

    /// 新建文件（清空编辑区）
    virtual void newFile() = 0;

    /// 当前是否有打开的文件
    virtual bool hasOpenFile() const = 0;

    /// 获取当前文件路径
    virtual QString currentFilePath() const = 0;

    /// 设置编码格式
    virtual void setEncoding(const QString& encodingName) = 0;

    /// 获取当前编码格式
    virtual QString encoding() const = 0;

    /// 文件是否被修改过
    virtual bool isModified() const = 0;

    /// 外部设置修改状态（由编辑器变更通知驱动）
    virtual void setModified(bool modified) = 0;

    /// 关闭当前文件
    virtual void closeFile() = 0;

    /// 注册观察者（用于文件状态变更通知）
    virtual void attachObserver(IObserver* observer) = 0;

    /// 设置内容读取回调
    virtual void setContentReader(std::function<QString()> reader) = 0;

    /// 设置内容写入回调
    virtual void setContentWriter(std::function<void(const QString&)> writer) = 0;
};

#endif // IFILEOPERATOR_H
