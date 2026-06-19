#include "core/format/JsonValidator.h"
#include "Logger.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>

// ========== 校验 ==========

QList<JsonValidator::ValidationError> JsonValidator::validate(const QString& jsonText)
{
    QList<ValidationError> errors;

    QJsonParseError parseError;
    QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        ValidationError err;
        // QJsonParseError 的 offset 是字符偏移，需要转换为行号和列号
        int offset = parseError.offset;
        QString prefix = jsonText.left(offset);
        int line = prefix.count(QLatin1Char('\n')) + 1;
        int lastNewline = prefix.lastIndexOf(QLatin1Char('\n'));
        int column = (lastNewline < 0) ? (offset + 1) : (offset - lastNewline);

        err.line = line;
        err.column = column;
        err.message = parseError.errorString();
        errors.append(err);
    }

    return errors;
}

bool JsonValidator::isValid(const QString& jsonText)
{
    QJsonParseError error;
    QJsonDocument::fromJson(jsonText.toUtf8(), &error);
    return error.error == QJsonParseError::NoError;
}

// ========== 格式化 ==========

QString JsonValidator::formatJson(const QString& jsonText)
{
    if (!isValid(jsonText)) {
        LOG_DEBUG_S("JsonValidator", "formatJson", "JSON格式非法，无法美化");
        return jsonText;  // 返回原文本
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}
