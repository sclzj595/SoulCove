#include "core/task/TasksJsonParser.h"
#include "Logger.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>

// ============================================================
// P3-M04 子项2: VSCode tasks.json 解析/序列化
// ============================================================

QList<TaskItem> TasksJsonParser::parseFile(const QString& filePath)
{
    QList<TaskItem> result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_WARN_S("TasksJsonParser", "parseFile", "无法打开 tasks.json:" << filePath);
        return result;
    }

    QString jsonText = QString::fromUtf8(file.readAll());
    file.close();

    QString errMsg;
    result = parseText(jsonText, &errMsg);
    if (!errMsg.isEmpty()) {
        LOG_WARN_S("TasksJsonParser", "parseFile",
                   "解析 tasks.json 失败:" << filePath << "错误:" << errMsg);
    } else {
        LOG_DEBUG_S("TasksJsonParser", "parseFile",
                    "解析 tasks.json 成功:" << filePath << "任务数:" << result.size());
    }
    return result;
}

QList<TaskItem> TasksJsonParser::parseText(const QString& jsonText, QString* errMsg)
{
    QList<TaskItem> result;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errMsg) *errMsg = parseError.errorString();
        return result;
    }
    if (!doc.isObject()) {
        if (errMsg) *errMsg = QStringLiteral("根节点必须是 JSON 对象");
        return result;
    }

    QJsonObject root = doc.object();

    // version 字段（兼容性记录，不强制校验）
    QString version = root.value(QStringLiteral("version")).toString(QStringLiteral("2.0.0"));
    LOG_DEBUG_S("TasksJsonParser", "parseText", "tasks.json version:" << version);

    // 兼容两种结构：
    //   1) 标准: { "tasks": [ {label, command, ...}, ... ] }
    //   2) 简化: [ {label, command, ...}, ... ]  (直接是数组)
    QJsonArray tasksArray;
    if (root.contains(QStringLiteral("tasks"))) {
        tasksArray = root.value(QStringLiteral("tasks")).toArray();
    } else if (doc.isArray()) {
        tasksArray = doc.array();
    } else {
        if (errMsg) *errMsg = QStringLiteral("缺少 tasks 字段");
        return result;
    }

    for (const QJsonValue& val : tasksArray) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();

        TaskItem t;
        // label 是唯一标识（VSCode 必填，缺失则尝试 taskName / 继承的命令字符串）
        t.label = obj.value(QStringLiteral("label")).toString();
        if (t.label.isEmpty()) {
            // 兼容 fallback：使用 taskName 或 command 字段
            t.label = obj.value(QStringLiteral("taskName")).toString();
            if (t.label.isEmpty()) {
                t.label = obj.value(QStringLiteral("command")).toString();
            }
            if (t.label.isEmpty()) continue;  // 仍无标识，跳过
        }

        // type: "shell" / "process"  (默认 shell)
        t.type = obj.value(QStringLiteral("type")).toString(QStringLiteral("shell"));

        // command & args
        // 兼容 windows/linux/osx 平台特定字段
        QJsonObject platformObj;
#ifdef Q_OS_WIN
        platformObj = obj.value(QStringLiteral("windows")).toObject();
#elif defined(Q_OS_MACOS)
        platformObj = obj.value(QStringLiteral("osx")).toObject();
#else
        platformObj = obj.value(QStringLiteral("linux")).toObject();
#endif
        t.command = platformObj.value(QStringLiteral("command")).toString(
            obj.value(QStringLiteral("command")).toString());

        // args 可能为数组或字符串
        auto extractArgs = [](const QJsonValue& v) -> QStringList {
            QStringList out;
            if (v.isArray()) {
                const QJsonArray arr = v.toArray();
                for (const QJsonValue& a : arr) {
                    if (a.isString()) out.append(a.toString());
                }
            } else if (v.isString()) {
                // 字符串形式按空格拆分（简易处理）
                out.append(v.toString());
            }
            return out;
        };
        t.args = extractArgs(platformObj.value(QStringLiteral("args")).isArray()
                             ? platformObj.value(QStringLiteral("args"))
                             : obj.value(QStringLiteral("args")));

        // group: 可能是字符串或对象 { "kind": "build", "isDefault": true }
        QJsonValue groupVal = obj.value(QStringLiteral("group"));
        if (groupVal.isObject()) {
            QJsonObject groupObj = groupVal.toObject();
            t.group = groupObj.value(QStringLiteral("kind")).toString(QStringLiteral("build"));
        } else if (groupVal.isString()) {
            t.group = groupVal.toString(QStringLiteral("build"));
        } else {
            t.group = QStringLiteral("build");
        }

        // problemMatcher: 可能是字符串或字符串数组，统一取第一个非空值
        QJsonValue pmVal = obj.value(QStringLiteral("problemMatcher"));
        if (pmVal.isString()) {
            t.problemMatcher = pmVal.toString();
        } else if (pmVal.isArray()) {
            const QJsonArray pmArr = pmVal.toArray();
            if (!pmArr.isEmpty() && pmArr.first().isString()) {
                t.problemMatcher = pmArr.first().toString();
            }
        }

        // presentation 对象中的 reveal 字段
        QJsonObject presObj = obj.value(QStringLiteral("presentation")).toObject();
        t.presentation = presObj.value(QStringLiteral("reveal")).toString();

        // isBackground
        t.isBackground = obj.value(QStringLiteral("isBackground")).toBool(false);

        result.append(t);
    }

    return result;
}

QString TasksJsonParser::toJson(const QList<TaskItem>& tasks)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), QStringLiteral("2.0.0"));

    QJsonArray tasksArray;
    for (const TaskItem& t : tasks) {
        QJsonObject obj;
        obj.insert(QStringLiteral("label"), t.label);
        obj.insert(QStringLiteral("type"), t.type.isEmpty() ? QStringLiteral("shell") : t.type);
        obj.insert(QStringLiteral("command"), t.command);

        QJsonArray argsArr;
        for (const QString& a : t.args) {
            argsArr.append(a);
        }
        obj.insert(QStringLiteral("args"), argsArr);

        // group 输出为对象（与 VSCode 标准格式一致）
        QJsonObject groupObj;
        groupObj.insert(QStringLiteral("kind"), t.group.isEmpty() ? QStringLiteral("build") : t.group);
        groupObj.insert(QStringLiteral("isDefault"), t.group == QStringLiteral("build"));
        obj.insert(QStringLiteral("group"), groupObj);

        if (t.isBackground) {
            obj.insert(QStringLiteral("isBackground"), true);
        }
        if (!t.problemMatcher.isEmpty()) {
            obj.insert(QStringLiteral("problemMatcher"), t.problemMatcher);
        }
        if (!t.presentation.isEmpty()) {
            QJsonObject presObj;
            presObj.insert(QStringLiteral("reveal"), t.presentation);
            obj.insert(QStringLiteral("presentation"), presObj);
        }

        tasksArray.append(obj);
    }
    root.insert(QStringLiteral("tasks"), tasksArray);

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}
