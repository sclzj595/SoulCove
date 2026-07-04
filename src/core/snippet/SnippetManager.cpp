#include "core/snippet/SnippetManager.h"
#include "Logger.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QUuid>
#include <QCoreApplication>
#include <QRegularExpression>

// ========== 单例 ==========

SnippetManager::SnippetManager()
    : QObject(nullptr)
{
    m_storagePath = QCoreApplication::applicationDirPath()
                    + QStringLiteral("/config/snippets.json");

    // 确保配置目录存在
    QDir dir(QFileInfo(m_storagePath).absolutePath());
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    // 加载片段（先尝试从文件，若不存在则加载默认）
    loadFromFile();
}

SnippetManager& SnippetManager::instance()
{
    static SnippetManager inst;
    return inst;
}

// ========== ID 生成 ==========

QString SnippetManager::generateId()
{
    return QStringLiteral("snip_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// ========== CRUD ==========

QList<CodeSnippet> SnippetManager::allSnippets() const
{
    return m_snippets.values();
}

QList<CodeSnippet> SnippetManager::snippetsForLanguage(const QString& lang) const
{
    QList<CodeSnippet> result;
    for (auto it = m_snippets.constBegin(); it != m_snippets.constEnd(); ++it) {
        if (it.value().language == lang || it.value().language == QStringLiteral("all"))
            result.append(it.value());
    }
    return result;
}

CodeSnippet SnippetManager::snippet(const QString& id) const
{
    return m_snippets.value(id);
}

void SnippetManager::addSnippet(const CodeSnippet& snippet)
{
    CodeSnippet s = snippet;
    if (s.id.isEmpty())
        s.id = generateId();
    if (s.createdTime.isNull())
        s.createdTime = QDateTime::currentDateTime();
    s.modifiedTime = QDateTime::currentDateTime();

    m_snippets[s.id] = s;
    saveToFile();
}

void SnippetManager::removeSnippet(const QString& id)
{
    m_snippets.remove(id);
    saveToFile();
}

void SnippetManager::updateSnippet(const CodeSnippet& snippet)
{
    if (!m_snippets.contains(snippet.id)) return;

    CodeSnippet s = snippet;
    s.modifiedTime = QDateTime::currentDateTime();
    m_snippets[s.id] = s;
    saveToFile();
}

// ========== 搜索 ==========

QList<CodeSnippet> SnippetManager::search(const QString& keyword) const
{
    if (keyword.isEmpty()) return allSnippets();

    QList<CodeSnippet> result;
    QString lowerKeyword = keyword.toLower();

    for (auto it = m_snippets.constBegin(); it != m_snippets.constEnd(); ++it) {
        const CodeSnippet& s = it.value();
        if (s.name.toLower().contains(lowerKeyword) ||
            s.prefix.toLower().contains(lowerKeyword) ||
            s.description.toLower().contains(lowerKeyword) ||
            s.language.toLower().contains(lowerKeyword)) {
            result.append(s);
        }
    }
    return result;
}

// ========== 触发检测 ==========

CodeSnippet SnippetManager::findTrigger(const QString& prefix) const
{
    QString trimmedPrefix = prefix.trimmed().toLower();

    // 精确匹配前缀
    for (auto it = m_snippets.constBegin(); it != m_snippets.constEnd(); ++it) {
        if (it.value().prefix.toLower() == trimmedPrefix)
            return it.value();
    }

    // 前缀匹配（用户可能还在输入）
    for (auto it = m_snippets.constBegin(); it != m_snippets.constEnd(); ++it) {
        if (it.value().prefix.toLower().startsWith(trimmedPrefix))
            return it.value();
    }

    return CodeSnippet();  // 空结构表示未找到
}

// ========== 展开 snippet ==========

QString SnippetManager::expandSnippet(const CodeSnippet& snippet, const QString& selection)
{
    QString body = snippet.body;

    // P2-H02 子项2: $SELECTION 变量替换为编辑器选中文本（为空则替换为空串）
    // 注意: 必须在 $N 占位符替换之前处理，避免 $SELECTION 误匹配 $N 规则
    body.replace(QStringLiteral("$SELECTION"), selection);

    // 将 ${1:default} 格式的占位符替换为 <|1|> 标记
    // 将 $1, $2 格式替换为 <|1|>, <|2|> 标记
    // <|N|> 是自定义的光标跳转位置标记，由 MyTextEdit 处理

    // 先处理 ${N:default} 格式
    QRegularExpression placeholderRegex(QStringLiteral("\\$\\{(\\d+)(?::([^}]*))?\\}"));
    body.replace(placeholderRegex, QStringLiteral("<|\\1|>"));

    // 再处理 $N 格式（未被上面捕获的简单数字引用）
    QRegularExpression simpleRefRegex(QStringLiteral("\\$(\\d+)"));
    body.replace(simpleRefRegex, QStringLiteral("<|\\1|>"));

    // $0 表示最终光标位置
    body.replace(QStringLiteral("$0"), QStringLiteral("<|0|>"));

    return body;
}

// ========== 持久化（JSON）==========

// ========== VSCode 格式兼容（P2-H02 子项3）==========
// VSCode snippet 格式: { "名称": { "prefix","body":["行1","行2"],"description" } }
// 内部 body 为带 \n 的字符串，VSCode 为字符串数组，转换时正确拆分/合并。

bool SnippetManager::importFromVscodeJson(const QString& filePath, const QString& language)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_DEBUG_S("SnippetManager", "importFromVscodeJson", "无法读取文件:" << filePath << file.errorString());
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_DEBUG_S("SnippetManager", "importFromVscodeJson", "JSON 解析失败:" << parseError.errorString());
        return false;
    }

    // 导入语言：传入为空时使用 "all"
    QString lang = language.isEmpty() ? QStringLiteral("all") : language;
    QDateTime now = QDateTime::currentDateTime();
    int imported = 0;

    QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        QString name = it.key();
        QJsonObject obj = it.value().toObject();
        if (obj.isEmpty()) continue;

        CodeSnippet s;
        s.id = generateId();
        s.name = name;
        s.prefix = obj.value(QStringLiteral("prefix")).toString();
        s.description = obj.value(QStringLiteral("description")).toString();
        s.language = lang;
        s.shortcut.clear();

        // body 字段：VSCode 为字符串数组，合并为带 \n 的字符串
        QJsonValue bodyVal = obj.value(QStringLiteral("body"));
        if (bodyVal.isArray()) {
            QJsonArray bodyArr = bodyVal.toArray();
            QStringList lines;
            lines.reserve(bodyArr.size());
            for (const QJsonValue& line : bodyArr)
                lines.append(line.toString());
            s.body = lines.join(QStringLiteral("\n"));
        } else {
            s.body = bodyVal.toString();
        }

        s.createdTime = now;
        s.modifiedTime = now;

        if (!s.name.isEmpty()) {
            m_snippets[s.id] = s;
            ++imported;
        }
    }

    if (imported > 0)
        saveToFile();

    LOG_DEBUG_S("SnippetManager", "importFromVscodeJson", "从 VSCode 格式导入" << imported << "个片段 (language=" << lang << ")");
    return imported > 0;
}

bool SnippetManager::exportToVscodeJson(const QString& filePath, const QString& language) const
{
    QJsonObject root;
    bool filterByLang = !language.isEmpty();
    int exported = 0;

    for (auto it = m_snippets.constBegin(); it != m_snippets.constEnd(); ++it) {
        const CodeSnippet& s = it.value();
        // 按语言过滤：指定语言时仅导出该语言 + "all" 通用片段
        if (filterByLang && s.language != language && s.language != QStringLiteral("all"))
            continue;

        QJsonObject obj;
        obj[QStringLiteral("prefix")] = s.prefix;
        // body 字段：内部字符串按 \n 拆分为 VSCode 字符串数组
        QJsonArray bodyArr;
        const QStringList lines = s.body.split(QStringLiteral("\n"));
        for (const QString& line : lines)
            bodyArr.append(line);
        obj[QStringLiteral("body")] = bodyArr;
        obj[QStringLiteral("description")] = s.description;

        // VSCode 以片段名为 key；同名时追加语言后缀避免覆盖
        QString key = s.name;
        if (root.contains(key))
            key = QStringLiteral("%1 (%2)").arg(s.name, s.language);
        root[key] = obj;
        ++exported;
    }

    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_DEBUG_S("SnippetManager", "exportToVscodeJson", "无法写入文件:" << filePath << file.errorString());
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    LOG_DEBUG_S("SnippetManager", "exportToVscodeJson", "已导出" << exported << "个片段到 VSCode 格式" << filePath);
    return exported > 0;
}

void SnippetManager::saveToFile()
{
    QJsonArray arr;
    for (auto it = m_snippets.constBegin(); it != m_snippets.constEnd(); ++it) {
        const CodeSnippet& s = it.value();
        QJsonObject obj;
        obj[QStringLiteral("id")]           = s.id;
        obj[QStringLiteral("name")]         = s.name;
        obj[QStringLiteral("description")]  = s.description;
        obj[QStringLiteral("language")]     = s.language;
        obj[QStringLiteral("prefix")]       = s.prefix;
        obj[QStringLiteral("body")]         = s.body;
        obj[QStringLiteral("shortcut")]     = s.shortcut;
        obj[QStringLiteral("createdTime")]  = s.createdTime.toString(Qt::ISODate);
        obj[QStringLiteral("modifiedTime")] = s.modifiedTime.toString(Qt::ISODate);
        arr.append(obj);
    }

    QFile file(m_storagePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(arr);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        LOG_DEBUG_S("SnippetManager", "saveToFile", "已保存" << m_snippets.size() << "个片段到" << m_storagePath);
    } else {
        LOG_DEBUG_S("SnippetManager", "saveToFile", "无法保存文件:" << m_storagePath << file.errorString());
    }
}

void SnippetManager::loadFromFile()
{
    QFile file(m_storagePath);
    if (!file.exists()) {
        LOG_DEBUG_S("SnippetManager", "loadFromFile", "片段文件不存在，加载默认片段");
        loadDefaultSnippets();
        saveToFile();
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_DEBUG_S("SnippetManager", "loadFromFile", "无法读取文件:" << m_storagePath << file.errorString());
        loadDefaultSnippets();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        LOG_DEBUG_S("SnippetManager", "loadFromFile", "JSON 解析失败:" << parseError.errorString());
        loadDefaultSnippets();
        return;
    }

    m_snippets.clear();
    QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        CodeSnippet s;
        s.id           = obj[QStringLiteral("id")].toString();
        s.name         = obj[QStringLiteral("name")].toString();
        s.description  = obj[QStringLiteral("description")].toString();
        s.language     = obj[QStringLiteral("language")].toString();
        s.prefix       = obj[QStringLiteral("prefix")].toString();
        s.body         = obj[QStringLiteral("body")].toString();
        s.shortcut     = obj[QStringLiteral("shortcut")].toString();
        s.createdTime  = QDateTime::fromString(obj[QStringLiteral("createdTime")].toString(), Qt::ISODate);
        s.modifiedTime = QDateTime::fromString(obj[QStringLiteral("modifiedTime")].toString(), Qt::ISODate);

        if (!s.id.isEmpty()) {
            m_snippets[s.id] = s;
        }
    }

    LOG_DEBUG_S("SnippetManager", "loadFromFile", "已加载" << m_snippets.size() << "个片段");
}

// ========== 内置默认 snippets ==========

void SnippetManager::loadDefaultSnippets()
{
    m_snippets.clear();
    auto now = QDateTime::currentDateTime();

    // C++ 类定义
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("C++ Class");
        s.description = tr("C++ 类模板");
        s.language = QStringLiteral("cpp");
        s.prefix = QStringLiteral("class");
        s.body = QStringLiteral("class $1 {\npublic:\n\t$1();\n\t~$1();\n\nprivate:\n\n};");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // C++ main 函数
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("C++ Main");
        s.description = tr("C++ 主函数模板");
        s.language = QStringLiteral("cpp");
        s.prefix = QStringLiteral("main");
        s.body = QStringLiteral("int main(int argc, char* argv[]) {\n\t$1\n\treturn 0;\n}");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // Python 函数
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Python Function");
        s.description = tr("Python 函数定义模板");
        s.language = QStringLiteral("python");
        s.prefix = QStringLiteral("def");
        s.body = QStringLiteral("def $1($2):\n\t\"\"\"$3\"\"\"\n\t$0");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // Python 类
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Python Class");
        s.description = tr("Python 类定义模板");
        s.language = QStringLiteral("python");
        s.prefix = QStringLiteral("pclass");
        s.body = QStringLiteral("class $1:\n\tdef __init__(self$2):\n\t\t$0");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // JS 箭头函数
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("JS Arrow Function");
        s.description = tr("JavaScript 箭头函数模板");
        s.language = QStringLiteral("javascript");
        s.prefix = QStringLiteral("=>");
        s.body = QStringLiteral("const $1 = ($2) => {\n\t$0\n};");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // HTML 模板
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("HTML Template");
        s.description = tr("HTML5 文档模板");
        s.language = QStringLiteral("html");
        s.prefix = QStringLiteral("html");
        s.body = QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n\t<meta charset=\"UTF-8\">\n\t<title>$1</title>\n</head>\n<body>\n\t$0\n</body>\n</html>");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // For 循环（通用）
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("For Loop");
        s.description = tr("for 循环模板");
        s.language = QStringLiteral("all");
        s.prefix = QStringLiteral("for");
        s.body = QStringLiteral("for (int $1 = 0; $1 < $2; $1++) {\n\t$0\n}");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // If 语句
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("If Statement");
        s.description = tr("if 条件判断模板");
        s.language = QStringLiteral("all");
        s.prefix = QStringLiteral("if");
        s.body = QStringLiteral("if ($1) {\n\t$0\n}");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // Try-Catch
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Try-Catch");
        s.description = tr("异常处理模板");
        s.language = QStringLiteral("all");
        s.prefix = QStringLiteral("try");
        s.body = QStringLiteral("try {\n\t$1\n} catch ($2) {\n\t$0\n}");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // Qt Signal-Slot 连接
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Qt Signal Slot");
        s.description = tr("Qt 信号槽连接模板");
        s.language = QStringLiteral("cpp");
        s.prefix = QStringLiteral("signal");
        s.body = QStringLiteral("connect($1, &$2::$3, this, &$4::$5);");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // Print Debug (Python)
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Print Debug");
        s.description = tr("Python 调试打印模板");
        s.language = QStringLiteral("python");
        s.prefix = QStringLiteral("pp");
        s.body = QStringLiteral("print(f\"$1={$1}\")");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // Log Info (Qt)
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Log Info");
        s.description = tr("Qt 调试日志模板");
        s.language = QStringLiteral("all");
        s.prefix = QStringLiteral("log");
        s.body = QStringLiteral("qDebug() << \"$1\";");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // Markdown 表格
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Markdown Table");
        s.description = tr("Markdown 表格模板");
        s.language = QStringLiteral("markdown");
        s.prefix = QStringLiteral("table");
        s.body = QStringLiteral("| $1 |\n|--|--|\n|$2 | |$3 |");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // While 循环
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("While Loop");
        s.description = tr("while 循环模板");
        s.language = QStringLiteral("all");
        s.prefix = QStringLiteral("while");
        s.body = QStringLiteral("while ($1) {\n\t$0\n}");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // Switch-Case
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Switch Case");
        s.description = tr("switch-case 分支模板");
        s.language = QStringLiteral("all");
        s.prefix = QStringLiteral("switch");
        s.body = QStringLiteral("switch ($1) {\ncase $2:\n\t$0\n\tbreak;\ndefault:\n\tbreak;\n}");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // C++ 头文件保护
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Header Guard");
        s.description = tr("C++ 头文件保护宏模板");
        s.language = QStringLiteral("cpp");
        s.prefix = QStringLiteral("guard");
        s.body = QStringLiteral("#ifndef $1_H\n#define $1_H\n\n$0\n\n#endif // $1_H");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // C++ namespace
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Namespace");
        s.description = tr("C++ 命名空间模板");
        s.language = QStringLiteral("cpp");
        s.prefix = QStringLiteral("ns");
        s.body = QStringLiteral("namespace $1 {\n\n$0\n\n} // namespace $1");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // JS async 函数
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Async Function");
        s.description = tr("JavaScript 异步函数模板");
        s.language = QStringLiteral("javascript");
        s.prefix = QStringLiteral("async");
        s.body = QStringLiteral("async function $1($2) {\n\t$0\n}");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    // CSS 媒体查询
    {
        CodeSnippet s;
        s.id = generateId();
        s.name = tr("Media Query");
        s.description = tr("CSS 响应式媒体查询模板");
        s.language = QStringLiteral("css");
        s.prefix = QStringLiteral("@media");
        s.body = QStringLiteral("@media ($1) {\n\t$0\n}");
        s.createdTime = now;
        m_snippets[s.id] = s;
    }

    LOG_DEBUG_S("SnippetManager", "loadDefaultSnippets", "已加载" << m_snippets.size() << "个默认片段");
}
