#include "core/workspace/WorkspaceManager.h"
#include "Logger.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>

// ========== 单例实现 ==========

WorkspaceManager& WorkspaceManager::instance()
{
    static WorkspaceManager s_instance;
    return s_instance;
}

WorkspaceManager::WorkspaceManager()
    : QObject(nullptr)
{
}

// ========== 当前工作区状态 ==========

Workspace WorkspaceManager::current() const
{
    return m_current;
}

// ========== 文件序列化 ==========
// JSON 格式：
// {
//   "name": "<工作区名称>",
//   "folders": ["<绝对路径>", ...],
//   "openFiles": ["<绝对路径>", ...],
//   "activeFile": "<绝对路径>",
//   "splitterState": "<base64>",
//   "sidebarState": "<base64>"
// }

void WorkspaceManager::saveToFile(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        LOG_WARN_S("WorkspaceManager", "saveToFile", "目标文件路径为空，已忽略");
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("name"), m_current.name);
    root.insert(QStringLiteral("activeFile"), m_current.activeFile);

    QJsonArray foldersArr;
    for (const QString& f : m_current.folders) {
        foldersArr.append(f);
    }
    root.insert(QStringLiteral("folders"), foldersArr);

    QJsonArray openFilesArr;
    for (const QString& f : m_current.openFiles) {
        openFilesArr.append(f);
    }
    root.insert(QStringLiteral("openFiles"), openFilesArr);

    // QByteArray 以 base64 字符串形式存储（二进制安全）
    root.insert(QStringLiteral("splitterState"),
                QString::fromLatin1(m_current.splitterState.toBase64()));
    root.insert(QStringLiteral("sidebarState"),
                QString::fromLatin1(m_current.sidebarState.toBase64()));

    QJsonDocument doc(root);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_ERROR_S("WorkspaceManager", "saveToFile",
                    "无法写入工作区文件:" << filePath);
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    LOG_INFO_S("WorkspaceManager", "saveToFile",
               "工作区已保存:" << filePath << " | folders=" << m_current.folders.size());
}

bool WorkspaceManager::loadFromFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        LOG_WARN_S("WorkspaceManager", "loadFromFile", "目标文件路径为空，已忽略");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARN_S("WorkspaceManager", "loadFromFile",
                   "无法读取工作区文件:" << filePath);
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_ERROR_S("WorkspaceManager", "loadFromFile",
                    "解析工作区 JSON 失败:" << err.errorString().toStdString()
                    << " | file=" << filePath);
        return false;
    }

    const QJsonObject root = doc.object();
    Workspace ws;
    ws.name = root.value(QStringLiteral("name")).toString();
    ws.activeFile = root.value(QStringLiteral("activeFile")).toString();

    const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
    for (const QJsonValue& v : foldersArr) {
        if (v.isString()) {
            QString f = v.toString();
            if (!f.isEmpty() && !ws.folders.contains(f)) {
                ws.folders.append(f);
            }
        }
    }

    const QJsonArray openFilesArr = root.value(QStringLiteral("openFiles")).toArray();
    for (const QJsonValue& v : openFilesArr) {
        if (v.isString()) {
            QString f = v.toString();
            if (!f.isEmpty() && !ws.openFiles.contains(f)) {
                ws.openFiles.append(f);
            }
        }
    }

    QString splitterB64 = root.value(QStringLiteral("splitterState")).toString();
    if (!splitterB64.isEmpty()) {
        ws.splitterState = QByteArray::fromBase64(splitterB64.toLatin1());
    }
    QString sidebarB64 = root.value(QStringLiteral("sidebarState")).toString();
    if (!sidebarB64.isEmpty()) {
        ws.sidebarState = QByteArray::fromBase64(sidebarB64.toLatin1());
    }

    m_current = ws;
    m_workspaceFile = filePath;

    LOG_INFO_S("WorkspaceManager", "loadFromFile",
               "工作区已加载:" << filePath << " | folders=" << ws.folders.size()
               << " openFiles=" << ws.openFiles.size());
    return true;
}

// ========== 工作区文件路径 ==========

void WorkspaceManager::setWorkspaceFile(const QString& filePath)
{
    m_workspaceFile = filePath;
}

QString WorkspaceManager::workspaceFile() const
{
    return m_workspaceFile;
}

// ========== 文件夹管理 ==========

void WorkspaceManager::addFolder(const QString& folder)
{
    if (folder.isEmpty()) return;
    QString abs = QDir(folder).absolutePath();
    if (!m_current.folders.contains(abs)) {
        m_current.folders.append(abs);
        LOG_DEBUG_S("WorkspaceManager", "addFolder", "添加文件夹:" << abs);
    }
}

void WorkspaceManager::removeFolder(const QString& folder)
{
    if (folder.isEmpty()) return;
    QString abs = QDir(folder).absolutePath();
    if (m_current.folders.removeAll(abs) > 0) {
        LOG_DEBUG_S("WorkspaceManager", "removeFolder", "移除文件夹:" << abs);
    }
}

// ========== 打开文件记录 ==========

void WorkspaceManager::recordOpenFile(const QString& filePath)
{
    if (filePath.isEmpty()) return;
    QString abs = QFileInfo(filePath).absoluteFilePath();
    if (!m_current.openFiles.contains(abs)) {
        m_current.openFiles.append(abs);
    }
}

void WorkspaceManager::recordActiveFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        m_current.activeFile.clear();
        return;
    }
    QString abs = QFileInfo(filePath).absoluteFilePath();
    m_current.activeFile = abs;
    if (!m_current.openFiles.contains(abs)) {
        m_current.openFiles.append(abs);
    }
}

// ========== 布局状态 ==========

void WorkspaceManager::setSplitterState(const QByteArray& state)
{
    m_current.splitterState = state;
}

void WorkspaceManager::setSidebarState(const QByteArray& state)
{
    m_current.sidebarState = state;
}

void WorkspaceManager::setName(const QString& name)
{
    m_current.name = name;
}
