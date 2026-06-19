#ifndef ITERMINALWIDGET_H
#define ITERMINALWIDGET_H

#include <QWidget>

/// @brief 终端组件抽象接口
/// 定义内嵌终端的核心能力，预留后续集成 QTermWidget / QConsole 等
/// 上层代码只依赖此接口，不依赖具体实现
class ITerminalWidget
{
public:
    virtual ~ITerminalWidget() = default;

    /// 启动终端会话
    virtual void startSession() = 0;

    /// 终止终端会话
    virtual void terminateSession() = 0;

    /// 执行命令
    virtual void executeCommand(const QString& command) = 0;

    /// 设置工作目录
    virtual void setWorkingDirectory(const QString& dirPath) = 0;

    /// 终端是否正在运行
    virtual bool isRunning() const = 0;

    /// 获取 QWidget 指针（用于布局嵌入）
    virtual QWidget* asWidget() = 0;
};

#endif // ITERMINALWIDGET_H
