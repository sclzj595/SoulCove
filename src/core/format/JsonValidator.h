#ifndef JSONVALIDATOR_H
#define JSONVALIDATOR_H

#include <QObject>
#include <QString>
#include <QList>

/// @brief JSON 校验器（静态工具类）
/// 使用 QJsonDocument 进行 JSON 语法校验和格式化
/// 用于 .json 文件的保存前校验、格式化输出等功能
class JsonValidator : public QObject
{
    Q_OBJECT

public:
    /// 校验错误信息结构体
    struct ValidationError {
        int line;       // 错误行号（从1开始）
        int column;     // 错误列号（从1开始）
        QString message; // 错误描述
    };

    /// 校验JSON文本，返回错误列表（空列表表示合法）
    static QList<ValidationError> validate(const QString& jsonText);

    /// 快速检查JSON是否合法
    static bool isValid(const QString& jsonText);

    /// 美化JSON输出（缩进2空格）
    /// @return 格式化后的JSON字符串，若输入非法则返回原文本
    static QString formatJson(const QString& jsonText);

private:
    // 纯静态工具类，禁止实例化
    JsonValidator() = delete;
};

#endif // JSONVALIDATOR_H
