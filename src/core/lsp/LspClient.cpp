#include "core/lsp/LspClient.h"
#include "Logger.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QCoreApplication>

// P0 C04-1: LSP CompletionItemKind 整数 → 可读字符串
// 参考 LSP 规范 https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#completionItemKind
static QString lspCompletionKindToString(int kind)
{
    switch (kind) {
        case 1:  return QStringLiteral("Text");
        case 2:  return QStringLiteral("Method");
        case 3:  return QStringLiteral("Function");
        case 4:  return QStringLiteral("Constructor");
        case 5:  return QStringLiteral("Field");
        case 6:  return QStringLiteral("Variable");
        case 7:  return QStringLiteral("Class");
        case 8:  return QStringLiteral("Interface");
        case 9:  return QStringLiteral("Module");
        case 10: return QStringLiteral("Property");
        case 11: return QStringLiteral("Unit");
        case 12: return QStringLiteral("Value");
        case 13: return QStringLiteral("Enum");
        case 14: return QStringLiteral("Keyword");
        case 15: return QStringLiteral("Snippet");
        case 16: return QStringLiteral("Color");
        case 17: return QStringLiteral("File");
        case 18: return QStringLiteral("Reference");
        case 19: return QStringLiteral("Folder");
        case 20: return QStringLiteral("EnumMember");
        case 21: return QStringLiteral("Constant");
        case 22: return QStringLiteral("Struct");
        case 23: return QStringLiteral("Event");
        case 24: return QStringLiteral("Operator");
        case 25: return QStringLiteral("TypeParameter");
        default: return QString();
    }
}

// ============================================================
// 构造 / 析构 — RAII 资源管理
// ============================================================

LspClient::LspClient(QObject* parent)
    : ILspClient(parent)
    , m_serverProcess(nullptr)  // RAII：延迟初始化
{
}

LspClient::~LspClient()
{
    // RAII：析构时自动停止服务器并清理资源
    stopServer();
}

std::unique_ptr<QProcess> LspClient::createServerProcess()
{
    // RAII：工厂方法创建进程对象
    auto process = std::make_unique<QProcess>(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    return process;
}

void LspClient::destroyServerProcess()
{
    // RAII：安全销毁（先断开信号再释放）
    if (!m_serverProcess) return;

    // 先断开所有信号连接
    m_serverProcess->disconnect();

    // 智能指针自动释放（RAII保证）
    m_serverProcess.reset();
}

// ============================================================
// 连接管理
// ============================================================

bool LspClient::startServer(const QString& command, const QStringList& args,
                            const QString& workingDir)
{
    // 如果已有运行中的服务器，先停止
    if (isRunning()) {
        stopServer();
    }

    // RAII：使用工厂方法创建新进程
    m_serverProcess = createServerProcess();

    // 设置工作目录
    if (!workingDir.isEmpty()) {
        m_serverProcess->setWorkingDirectory(workingDir);
    }

    // 连接信号槽（使用智能指针的get()获取原始指针）
    connect(m_serverProcess.get(), &QProcess::readyReadStandardOutput,
            this, &LspClient::onServerOutput);
    connect(m_serverProcess.get(), &QProcess::readyReadStandardError,
            this, &LspClient::onServerError);
    connect(m_serverProcess.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LspClient::onServerFinished);

    // 启动进程
    m_serverProcess->start(command, args);

    if (!m_serverProcess->waitForStarted(5000)) {
        QString error = tr("LSP 服务器启动失败: %1").arg(m_serverProcess->errorString());
        emit serverError(error);
        destroyServerProcess();  // RAII：安全清理
        return false;
    }

    m_initialized = false;
    m_buffer.clear();
    m_pendingRequests.clear();
    m_symbolRequestUri.clear();  // V2.1 C1: 同步清理请求跟踪表
    m_timeoutTimers.clear();     // P0 C01: 清理超时定时器
    m_lastHoverRequestId = -1;   // P0 C01: 重置 stale 跟踪
    m_lastCompletionRequestId = -1;
    m_lastDefinitionRequestId = -1;
    m_lastReferencesRequestId = -1;
    m_lastImplementationRequestId = -1;
    m_requestId = 0;

    LOG_DEBUG_S("LspClient", "startServer", "语言服务器已启动:" << command << args);
    emit serverStarted();
    return true;
}

void LspClient::stopServer()
{
    if (!m_serverProcess) return;

    // 先尝试优雅退出
    if (m_serverProcess->state() == QProcess::Running) {
        m_serverProcess->terminate();
        if (!m_serverProcess->waitForFinished(3000)) {
            m_serverProcess->kill();
            m_serverProcess->waitForFinished(2000);
        }
    }

    // RAII：安全销毁（自动disconnect + 释放内存）
    destroyServerProcess();

    m_initialized = false;
    m_pendingRequests.clear();
    m_symbolRequestUri.clear();  // V2.1 C1: 同步清理请求跟踪表
    m_timeoutTimers.clear();     // P0 C01: 清理超时定时器
    m_lastHoverRequestId = -1;   // P0 C01: 重置 stale 跟踪
    m_lastCompletionRequestId = -1;
    m_lastDefinitionRequestId = -1;
    m_lastReferencesRequestId = -1;
    m_lastImplementationRequestId = -1;

    LOG_DEBUG_S("LspClient", "stopServer", "语言服务器已停止");
    emit serverStopped();
}

bool LspClient::isRunning() const noexcept
{
    return m_serverProcess && m_serverProcess->state() == QProcess::Running;
}

// ============================================================
// JSON-RPC 消息构建
// ============================================================

QByteArray LspClient::createRequest(const QString& method, const QJsonObject& params, qint64* outId)
{
    qint64 id = ++m_requestId;
    if (outId) *outId = id;
    QJsonObject root;
    root[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    root[QStringLiteral("id")] = id;
    root[QStringLiteral("method")] = method;
    if (!params.isEmpty()) {
        root[QStringLiteral("params")] = params;
    }

    // 记录待处理请求
    m_pendingRequests[id] = method;

    // P0 C01: 通用请求超时机制 — 5s 后若仍未收到响应，从待处理表移除并清理 stale 跟踪
    auto timer = QSharedPointer<QTimer>::create();
    timer->setSingleShot(true);
    connect(timer.data(), &QTimer::timeout, this, [this, id, method]() {
        // 超时后若请求仍在待处理表中，说明服务器未响应
        if (m_pendingRequests.contains(id)) {
            m_pendingRequests.remove(id);
            m_symbolRequestUri.remove(id);
            // 清理 stale 跟踪变量（避免后续响应误判）
            if (m_lastHoverRequestId == id) m_lastHoverRequestId = -1;
            if (m_lastCompletionRequestId == id) m_lastCompletionRequestId = -1;
            if (m_lastDefinitionRequestId == id) m_lastDefinitionRequestId = -1;
            if (m_lastReferencesRequestId == id) m_lastReferencesRequestId = -1;
            if (m_lastImplementationRequestId == id) m_lastImplementationRequestId = -1;
            LOG_WARN_S("LspClient", "createRequest", "请求超时 [" << method << "] id=" << id);
        }
        m_timeoutTimers.remove(id);
    });
    m_timeoutTimers[id] = timer;
    timer->start(kRequestTimeoutMs);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray LspClient::createNotification(const QString& method, const QJsonObject& params)
{
    QJsonObject root;
    root[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    root[QStringLiteral("method")] = method;
    if (!params.isEmpty()) {
        root[QStringLiteral("params")] = params;
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void LspClient::sendRawMessage(const QByteArray& message)
{
    if (!isRunning()) return;

    // Content-Length 头部格式: "Content-Length: xxx\r\n\r\n{json}"
    QByteArray header;
    header = "Content-Length: " + QByteArray::number(message.size()) + "\r\n\r\n";

    m_serverProcess->write(header + message);

    LOG_DEBUG_S("LspClient", "sendRawMessage", "发送消息 (" << message.size() << " 字节)");
}

// ============================================================
// LSP 协议方法实现
// ============================================================

void LspClient::initialize(const QString& rootUri)
{
    QJsonObject params;
    params[QStringLiteral("processId")] = QCoreApplication::applicationPid();
    params[QStringLiteral("rootUri")] = rootUri;
    params[QStringLiteral("rootPath")] = QUrl(rootUri).toLocalFile();

    // Client capabilities（基础能力声明）
    QJsonObject capabilities;
    QJsonObject textDocument;
    QJsonObject completion;
    completion[QStringLiteral("completionItem")] = QJsonObject{{QStringLiteral("snippetSupport"), true}};
    textDocument[QStringLiteral("completion")] = completion;
    textDocument[QStringLiteral("hover")] = QJsonObject{{QStringLiteral("contentFormat"), QJsonArray{"markdown", "plaintext"}}};

    // V2.1: 声明支持层级 documentSymbol（返回嵌套 DocumentSymbol[] 而非扁平 SymbolInformation[]）
    // clangd 收到此声明后会返回带 children 字段的嵌套结构，支持大纲树形层级展示
    QJsonObject documentSymbol;
    documentSymbol[QStringLiteral("hierarchicalDocumentSymbolSupport")] = true;
    textDocument[QStringLiteral("documentSymbol")] = documentSymbol;

    capabilities[QStringLiteral("textDocument")] = textDocument;
    params[QStringLiteral("capabilities")] = capabilities;

    QByteArray msg = createRequest(QStringLiteral("initialize"), params);
    sendRawMessage(msg);
}

void LspClient::openDocument(const QString& uri, const QString& text, const QString& langId)
{
    // V2.0: 重置文档版本号（didOpen version=1，后续 didChange 从 2 开始递增）
    m_docVersion = 1;

    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;
    textDoc[QStringLiteral("languageId")] = langId;
    textDoc[QStringLiteral("version")] = m_docVersion;
    textDoc[QStringLiteral("text")] = text;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;

    QByteArray msg = createNotification(QStringLiteral("textDocument/didOpen"), params);
    sendRawMessage(msg);
}

// L3: 发送 textDocument/didClose — 关闭文档，避免闲置标签重新激活时重复 didOpen
// clangd 收到 didClose 后释放该文档的 preamble 缓存，重新 didOpen 时才重建
void LspClient::closeDocument(const QString& uri)
{
    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;

    QByteArray msg = createNotification(QStringLiteral("textDocument/didClose"), params);
    sendRawMessage(msg);
}

void LspClient::changeDocument(const QString& uri, const QString& fullText)
{
    // 使用 Full 文本同步模式
    QJsonObject contentChanges;
    contentChanges[QStringLiteral("text")] = fullText;

    QJsonArray changesArray;
    changesArray.append(contentChanges);

    // V2.0: 版本号递增（LSP 规范要求每次 didChange 版本号严格递增）
    ++m_docVersion;

    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;
    textDoc[QStringLiteral("version")] = m_docVersion;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;
    params[QStringLiteral("contentChanges")] = changesArray;

    QByteArray msg = createNotification(QStringLiteral("textDocument/didChange"), params);
    sendRawMessage(msg);
}

void LspClient::didSave(const QString& uri)
{
    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;
    params[QStringLiteral("text")] = QString();  // 不包含文本内容（节省带宽）

    QByteArray msg = createNotification(QStringLiteral("textDocument/didSave"), params);
    sendRawMessage(msg);
}

// ========== 功能请求 ==========

void LspClient::requestCompletion(const QString& uri, int line, int col)
{
    QJsonObject position;
    position[QStringLiteral("line")] = line;
    position[QStringLiteral("character")] = col;

    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;
    params[QStringLiteral("position")] = position;

    // P0 C01: 记录最新 completion 请求 ID，用于丢弃 stale 响应
    qint64 reqId = -1;
    QByteArray msg = createRequest(QStringLiteral("textDocument/completion"), params, &reqId);
    m_lastCompletionRequestId = reqId;
    sendRawMessage(msg);
}

void LspClient::requestDefinition(const QString& uri, int line, int col)
{
    QJsonObject position;
    position[QStringLiteral("line")] = line;
    position[QStringLiteral("character")] = col;

    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;
    params[QStringLiteral("position")] = position;

    // P0 C01: 记录最新 definition 请求 ID，用于丢弃 stale 响应
    qint64 reqId = -1;
    QByteArray msg = createRequest(QStringLiteral("textDocument/definition"), params, &reqId);
    m_lastDefinitionRequestId = reqId;
    sendRawMessage(msg);
}

void LspClient::requestHover(const QString& uri, int line, int col)
{
    QJsonObject position;
    position[QStringLiteral("line")] = line;
    position[QStringLiteral("character")] = col;

    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;
    params[QStringLiteral("position")] = position;

    // F4: 记录本次 hover 请求 ID，用于丢弃 stale 响应
    qint64 reqId = -1;
    QByteArray msg = createRequest(QStringLiteral("textDocument/hover"), params, &reqId);
    m_lastHoverRequestId = reqId;
    sendRawMessage(msg);
}

void LspClient::requestReferences(const QString& uri, int line, int col)
{
    QJsonObject position;
    position[QStringLiteral("line")] = line;
    position[QStringLiteral("character")] = col;

    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;

    QJsonObject context;
    context[QStringLiteral("includeDeclaration")] = true;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;
    params[QStringLiteral("position")] = position;
    params[QStringLiteral("context")] = context;

    // P0 C01: 记录最新 references 请求 ID，用于丢弃 stale 响应
    qint64 reqId = -1;
    QByteArray msg = createRequest(QStringLiteral("textDocument/references"), params, &reqId);
    m_lastReferencesRequestId = reqId;
    sendRawMessage(msg);
}

void LspClient::requestImplementation(const QString& uri, int line, int col)
{
    // P0 C03: textDocument/implementation — 请求跳转到实现位置
    // 响应格式与 definition 相同（Location/Location[]），复用 definitionReady 信号
    QJsonObject position;
    position[QStringLiteral("line")] = line;
    position[QStringLiteral("character")] = col;

    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;
    params[QStringLiteral("position")] = position;

    qint64 reqId = -1;
    QByteArray msg = createRequest(QStringLiteral("textDocument/implementation"), params, &reqId);
    m_lastImplementationRequestId = reqId;
    sendRawMessage(msg);
}

void LspClient::requestSymbols(const QString& uri)
{
    QJsonObject textDoc;
    textDoc[QStringLiteral("uri")] = uri;

    QJsonObject params;
    params[QStringLiteral("textDocument")] = textDoc;

    // V2.1 C1 修复：记录 requestId → uri，异步响应时精确路由
    qint64 reqId = -1;
    QByteArray msg = createRequest(QStringLiteral("textDocument/documentSymbol"), params, &reqId);
    if (reqId >= 0) {
        m_symbolRequestUri[reqId] = uri;
    }
    sendRawMessage(msg);
}

void LspClient::requestDiagnostics(const QString& /*uri*/)
{
    // LSP 中诊断信息由服务端主动推送 (textDocument/publishDiagnostics)
    // 此方法保留作为未来扩展接口，当前通过 onServerOutput 自动接收诊断推送
    LOG_DEBUG_S("LspClient", "requestDiagnostics", "诊断信息由服务端自动推送，无需手动请求");
}

// ============================================================
// 状态查询
// ============================================================

bool LspClient::isInitialized() const noexcept
{
    return m_initialized;
}

QString LspClient::serverName() const noexcept
{
    return m_serverNameStr;
}

// ============================================================
// 数据读取与解析
// ============================================================

void LspClient::onServerOutput()
{
    if (!m_serverProcess) return;

    // 将新数据追加到缓冲区
    m_buffer.append(m_serverProcess->readAllStandardOutput());

    // 按 Content-Length 协议分帧解析
    while (!m_buffer.isEmpty()) {
        // 查找头部结束标记 \r\n\r\n
        int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) break;  // 头部不完整，等待更多数据

        // 解析 Content-Length
        QByteArray header = m_buffer.left(headerEnd);
        int lengthPos = header.indexOf("Content-Length:");
        if (lengthPos < 0) {
            LOG_WARN_S("LspClient", "onServerOutput", "无效的消息头（缺少 Content-Length）");
            m_buffer.remove(0, headerEnd + 4);  // 跳过无效头部
            continue;
        }

        // 提取长度值
        QByteArray lengthStr = header.mid(lengthPos + 16).trimmed();  // "Content-Length:" 长度 16
        bool ok = false;
        int contentLength = lengthStr.toInt(&ok);
        if (!ok || contentLength <= 0) {
            LOG_WARN_S("LspClient", "onServerOutput", "无效的 Content-Length 值:" << lengthStr);
            m_buffer.remove(0, headerEnd + 4);
            continue;
        }

        // 计算消息体起始位置
        int bodyStart = headerEnd + 4;  // \r\n\r\n 长度为 4

        // 检查消息体是否完整
        if (m_buffer.size() < bodyStart + contentLength) break;  // 等待更多数据

        // 提取完整的 JSON 消息体
        QByteArray jsonBody = m_buffer.mid(bodyStart, contentLength);
        m_buffer.remove(0, bodyStart + contentLength);

        // 解析 JSON 并分发
        parseResponse(jsonBody);
    }
}

void LspClient::onServerError()
{
    if (!m_serverProcess) return;
    QByteArray errData = m_serverProcess->readAllStandardError();
    if (!errData.isEmpty()) {
        QString errMsg = QString::fromUtf8(errData).trimmed();
        // clangd 等 LSP 服务器将正常日志（索引进度、include 检索、编译数据库警告）
        // 写入 stderr，这是 LSP 协议的标准行为，并非真实错误。
        // 降级为 DEBUG 日志，且不再 emit serverError（避免上层以 ERROR 重复记录）。
        // 仅进程异常退出（onServerFinished exitCode!=0）才 emit serverError。
        LOG_DEBUG_S("LspClient", "onServerError", "[clangd stderr] " << errMsg);
    }
}

void LspClient::onServerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)

    m_initialized = false;
    m_pendingRequests.clear();
    m_symbolRequestUri.clear();  // V2.1 C1: 进程退出时清理请求跟踪表
    m_timeoutTimers.clear();     // P0 C01: 清理超时定时器
    m_lastHoverRequestId = -1;   // P0 C01: 重置 stale 跟踪
    m_lastCompletionRequestId = -1;
    m_lastDefinitionRequestId = -1;
    m_lastReferencesRequestId = -1;
    m_lastImplementationRequestId = -1;

    if (exitCode != 0) {
        emit serverError(tr("语言服务器异常退出，退出码: %1").arg(exitCode));
    }

    LOG_DEBUG_S("LspClient", "onServerFinished", "服务器进程已结束，退出码:" << exitCode);
    emit serverStopped();
}

// ============================================================
// 响应解析与信号分发
// ============================================================

void LspClient::parseResponse(const QByteArray& data)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN_S("LspClient", "parseResponse", "JSON 解析失败:" << error.errorString());
        return;
    }

    handleMessage(doc.object());
}

void LspClient::handleMessage(const QJsonObject& msg)
{
    // 判断消息类型：通知（无 id）或 响应（有 id）
    if (msg.contains(QStringLiteral("id"))) {
        // === 响应消息 ===
        qint64 id = msg[QStringLiteral("id")].toVariant().toLongLong();
        QString method = m_pendingRequests.take(id);

        // P0 C01: 收到响应，停止对应的超时定时器
        auto timerIt = m_timeoutTimers.find(id);
        if (timerIt != m_timeoutTimers.end()) {
            timerIt.value()->stop();
            m_timeoutTimers.erase(timerIt);
        }

        // 检查错误响应
        if (msg.contains(QStringLiteral("error"))) {
            QJsonObject errObj = msg[QStringLiteral("error")].toObject();
            int code = errObj[QStringLiteral("code")].toInt(-1);
            QString errMsg = errObj[QStringLiteral("message")].toString();
            LOG_WARN_S("LspClient", "handleMessage", "错误响应 [" << method << "] code=" << code << ":" << errMsg);
            emit serverError(tr("LSP 错误 [%1]: %2").arg(method, errMsg));
            return;
        }

        QJsonValue result = msg[QStringLiteral("result")];

        // --- initialize 响应 ---
        if (method == QStringLiteral("initialize")) {
            m_initialized = true;
            QJsonObject resultObj = result.toObject();
            QJsonObject caps = resultObj.value(QStringLiteral("capabilities")).toObject();
            m_serverNameStr = resultObj.value(QStringLiteral("serverInfo"))
                                  .toObject()
                                  .value(QStringLiteral("name"))
                                  .toString(QStringLiteral("unknown"));

            LOG_DEBUG_S("LspClient", "handleMessage", "初始化完成，服务器:" << m_serverNameStr);

            // 发送 initialized 通知（LSP 握手第二步）
            QByteArray notif = createNotification(QStringLiteral("initialized"), QJsonObject());
            sendRawMessage(notif);

            // 通知上层握手完成，可发送缓存的 didOpen（LspManager 据此 flush pending 队列）
            emit initialized();
            return;
        }

        // --- textDocument/completion 响应 ---
        if (method == QStringLiteral("textDocument/completion")) {
            // P0 C01: stale 响应检测 — 丢弃非最新补全请求的响应
            // （用户已继续输入，旧请求的补全结果不再需要，避免闪烁/覆盖）
            if (id != m_lastCompletionRequestId) {
                LOG_DEBUG_S("LspClient", "handleMessage", "丢弃 stale completion 响应: id=" << id
                          << " 最新=" << m_lastCompletionRequestId);
                return;
            }

            QList<LspCompletionItem> items;

            // 兼容两种格式: CompletionList 或 CompletionItem[]
            QJsonObject resultObj = result.toObject();
            if (resultObj.contains(QStringLiteral("items"))) {
                QJsonArray itemList = resultObj[QStringLiteral("items")].toArray();
                for (const QJsonValue& val : itemList) {
                    QJsonObject item = val.toObject();
                    LspCompletionItem ci;
                    ci.label = item[QStringLiteral("label")].toString();
                    // P0 C04-1: LSP kind 是整数（1-25），需转换为可读字符串
                    QJsonValue kindVal = item[QStringLiteral("kind")];
                    if (kindVal.isDouble()) {
                        ci.kind = lspCompletionKindToString(kindVal.toInt());
                    } else {
                        ci.kind = kindVal.toString();
                    }
                    ci.detail = item[QStringLiteral("detail")].toString();

                    // documentation 可能是字符串或对象
                    QJsonValue docVal = item[QStringLiteral("documentation")];
                    if (docVal.isString()) {
                        ci.documentation = docVal.toString();
                    } else if (docVal.isObject()) {
                        ci.documentation = docVal.toObject()[QStringLiteral("value")].toString();
                    }

                    ci.insertText = item[QStringLiteral("insertText")].toString();
                    ci.sortText = item[QStringLiteral("sortText")].toString();
                    items.append(ci);
                }
            }

            LOG_DEBUG_S("LspClient", "handleMessage", "收到补全结果:" << items.size() << "项");
            emit completionsReady(items);
            return;
        }

        // --- textDocument/definition 响应 ---
        if (method == QStringLiteral("textDocument/definition")) {
            // P0 C01: stale 响应检测 — 丢弃非最新定义请求的响应
            if (id != m_lastDefinitionRequestId) {
                LOG_DEBUG_S("LspClient", "handleMessage", "丢弃 stale definition 响应: id=" << id
                          << " 最新=" << m_lastDefinitionRequestId);
                return;
            }

            // 结果可能是单个 Location 或 Location[]
            QJsonObject resultObj = result.toObject();
            if (resultObj.contains(QStringLiteral("uri"))) {
                // 单个位置
                QString uri = resultObj[QStringLiteral("uri")].toString();
                QJsonObject range = resultObj[QStringLiteral("range")].toObject();
                QJsonObject start = range[QStringLiteral("start")].toObject();
                emit definitionReady(uri,
                                     start[QStringLiteral("line")].toInt(),
                                     start[QStringLiteral("character")].toInt());
            } else if (result.isArray()) {
                // 位置数组（取第一个）
                QJsonArray locs = result.toArray();
                if (!locs.isEmpty()) {
                    QJsonObject first = locs.first().toObject();
                    QString uri = first[QStringLiteral("uri")].toString();
                    QJsonObject range = first[QStringLiteral("range")].toObject();
                    QJsonObject start = range[QStringLiteral("start")].toObject();
                    emit definitionReady(uri,
                                         start[QStringLiteral("line")].toInt(),
                                         start[QStringLiteral("character")].toInt());
                }
            }
            return;
        }

        // --- textDocument/implementation 响应 (P0 C03) ---
        // 响应格式与 definition 相同（Location/Location[]），复用 definitionReady 信号
        if (method == QStringLiteral("textDocument/implementation")) {
            if (id != m_lastImplementationRequestId) {
                LOG_DEBUG_S("LspClient", "handleMessage", "丢弃 stale implementation 响应: id=" << id
                          << " 最新=" << m_lastImplementationRequestId);
                return;
            }

            QJsonObject resultObj = result.toObject();
            if (resultObj.contains(QStringLiteral("uri"))) {
                QString uri = resultObj[QStringLiteral("uri")].toString();
                QJsonObject range = resultObj[QStringLiteral("range")].toObject();
                QJsonObject start = range[QStringLiteral("start")].toObject();
                emit definitionReady(uri,
                                     start[QStringLiteral("line")].toInt(),
                                     start[QStringLiteral("character")].toInt());
            } else if (result.isArray()) {
                QJsonArray locs = result.toArray();
                if (!locs.isEmpty()) {
                    QJsonObject first = locs.first().toObject();
                    QString uri = first[QStringLiteral("uri")].toString();
                    QJsonObject range = first[QStringLiteral("range")].toObject();
                    QJsonObject start = range[QStringLiteral("start")].toObject();
                    emit definitionReady(uri,
                                         start[QStringLiteral("line")].toInt(),
                                         start[QStringLiteral("character")].toInt());
                }
            }
            return;
        }

        // --- textDocument/hover 响应 ---
        if (method == QStringLiteral("textDocument/hover")) {
            // F4: stale 响应检测 — 如果这不是最新的 hover 请求的响应，丢弃
            // （鼠标已移动到新位置，旧请求的响应不再需要）
            if (id != m_lastHoverRequestId) {
                LOG_DEBUG("[LspClient] 丢弃 stale hover 响应: id=" << id
                          << " 最新=" << m_lastHoverRequestId);
                return;
            }

            QJsonValue contentsVal = result.toObject().value(QStringLiteral("contents"));
            QString docText;
            if (contentsVal.isObject()) {
                QJsonObject contents = contentsVal.toObject();
                docText = contents.value(QStringLiteral("value")).toString();
            } else if (contentsVal.isString()) {
                docText = contentsVal.toString();
            }

            QPoint pos;  // hover 位置（可从请求参数中获取）
            emit hoverReady(docText, pos);
            return;
        }

        // --- textDocument/references 响应 ---
        if (method == QStringLiteral("textDocument/references")) {
            // P0 C01: stale 响应检测 — 丢弃非最新引用请求的响应
            if (id != m_lastReferencesRequestId) {
                LOG_DEBUG_S("LspClient", "handleMessage", "丢弃 stale references 响应: id=" << id
                          << " 最新=" << m_lastReferencesRequestId);
                return;
            }

            // 引用列表 → 发射 referencesReady 信号（L17 查找引用）
            QList<QVariantMap> references;
            QJsonArray refArray = result.isArray() ? result.toArray() : QJsonArray{result};
            for (const QJsonValue& val : refArray) {
                references.append(val.toVariant().toMap());
            }
            LOG_DEBUG_S("LspClient", "handleMessage", "收到引用结果:"
                     << references.size() << "处引用");
            emit referencesReady(references);
            return;
        }

        // --- textDocument/documentSymbol 响应 ---
        if (method == QStringLiteral("textDocument/documentSymbol")) {
            QList<QVariantMap> symbols;
            QJsonArray symArray = result.isArray() ? result.toArray() : QJsonArray{result};
            for (const QJsonValue& val : symArray) {
                symbols.append(val.toVariant().toMap());
            }
            // V2.1 C1 修复：按 requestId 精确路由 uri，避免 m_currentRequestFile 被覆盖导致归属错误
            QString uri = m_symbolRequestUri.take(id);
            emit symbolsReady(uri, symbols);
            return;
        }

    } else {
        // === 通知 / 服务端推送 ===
        QString method = msg[QStringLiteral("method")].toString();
        QJsonObject params = msg[QStringLiteral("params")].toObject();

        // --- textDocument/publishDiagnostics 推送 ---
        if (method == QStringLiteral("textDocument/publishDiagnostics")) {
            QString diagUri = params[QStringLiteral("uri")].toString();
            QList<LspDiagnostic> diagnostics;
            QJsonArray diagArray = params[QStringLiteral("diagnostics")].toArray();

            for (const QJsonValue& dv : diagArray) {
                QJsonObject d = dv.toObject();
                LspDiagnostic ld;

                QJsonObject range = d[QStringLiteral("range")].toObject();
                QJsonObject start = range[QStringLiteral("start")].toObject();
                QJsonObject end = range[QStringLiteral("end")].toObject();

                ld.line = start[QStringLiteral("line")].toInt();
                ld.column = start[QStringLiteral("character")].toInt();
                ld.endLine = end[QStringLiteral("line")].toInt();
                ld.endColumn = end[QStringLiteral("character")].toInt();

                int severity = d[QStringLiteral("severity")].toInt(1);  // 默认 Error
                switch (severity) {
                case 1: ld.severity = LspDiagnostic::Error; break;
                case 2: ld.severity = LspDiagnostic::Warning; break;
                case 3: ld.severity = LspDiagnostic::Information; break;
                case 4: ld.severity = LspDiagnostic::Hint; break;
                default: ld.severity = LspDiagnostic::Error; break;
                }

                ld.message = d[QStringLiteral("message")].toString();
                ld.code = d[QStringLiteral("code")].toString();
                ld.source = d[QStringLiteral("source")].toString();

                diagnostics.append(ld);
            }

            emit diagnosticsReady(diagUri, diagnostics);
            return;
        }

        // --- window/logMessage 服务端日志 ---
        if (method == QStringLiteral("window/logMessage")) {
            int type = params[QStringLiteral("type")].toInt();
            QString msgText = params[QStringLiteral("message")].toString();
            LOG_DEBUG_S("LspClient", "handleMessage", "服务端日志 [type=" << type << "]:" << msgText);
            return;
        }

        // 其他未处理的通知
        LOG_DEBUG_S("LspClient", "handleMessage", "未处理的通知:" << method);
    }
}
