#ifndef TASKSJSONPARSER_H
#define TASKSJSONPARSER_H

#include "core/task/TaskManager.h"  // TaskItem 定义
#include <QString>
#include <QList>

/// @brief VSCode tasks.json 解析器（P3-M04 子项2）
///
/// 职责：将 VSCode tasks.json v2.0.0 格式转换为内部 TaskItem 列表。
/// 解析支持的字段：label/type/command/args/group/problemMatcher/presentation/options
///
/// 与 TaskManager::loadTasksJson 的差异：
/// - TasksJsonParser 仅负责「解析」，不写入 TaskManager 单例
/// - 调用方可在解析后选择合并 / 替换 / 预览
class TasksJsonParser
{
public:
    /// @brief 解析指定 tasks.json 文件，返回 TaskItem 列表
    /// @param filePath tasks.json 文件绝对/相对路径
    /// @return 解析失败返回空列表（同时日志告警），成功返回非空列表
    static QList<TaskItem> parseFile(const QString& filePath);

    /// @brief 解析 JSON 字符串（用于测试 / 网络/剪贴板导入）
    /// @param jsonText tasks.json 文本内容
    /// @param errMsg  解析错误信息输出（可选）
    static QList<TaskItem> parseText(const QString& jsonText, QString* errMsg = nullptr);

    /// @brief 将 TaskItem 列表序列化为 VSCode tasks.json v2.0.0 文本
    /// @param tasks 要导出的任务列表
    /// @return 缩进格式 JSON 文本
    static QString toJson(const QList<TaskItem>& tasks);
};

#endif // TASKSJSONPARSER_H
