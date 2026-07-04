#ifndef LSPCLIENT_H
#define LSPCLIENT_H

#include "interfaces/lsp/ILspClient.h"

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QStringList>
#include <QList>
#include <QPoint>
#include <QTimer>
#include <QSharedPointer>
#include <memory>  // RAII: std::unique_ptr

/// @brief LSP (Language Server Protocol) 客户端实现
///
/// 基于 JSON-RPC 2.0 通信协议，通过 QProcess 管理语言服务器进程生命周期。
/// 实现 ILspClient 接口，为上层提供 pylsp / clangd / typescript-language-server 等语言服务接入能力。
///
/// 消息格式: Content-Length: xxx\r\n\r\n{json}
///
/// RAII保证：
/// - 析构时自动停止服务器进程并清理资源
/// - 智能指针管理进程对象生命周期
/// - 异常安全的资源释放
///
/// 设计模式：
/// - 实现 ILspClient 接口（依赖倒置原则）
/// - 观察者模式：异步结果通过信号返回
/// - RAII：智能指针管理 QProcess 生命周期
class LspClient : public ILspClient
{
    Q_OBJECT

public:
    explicit LspClient(QObject* parent = nullptr);
    ~LspClient() override;

    // 禁用拷贝（含唯一资源）
    LspClient(const LspClient&) = delete;
    LspClient& operator=(const LspClient&) = delete;

    // === ILspClient 接口实现 ===

    // 连接管理
    bool startServer(const QString& command, const QStringList& args,
                     const QString& workingDir) override;
    void stopServer() override;
    bool isRunning() const noexcept override;

    // LSP 协议方法
    void initialize(const QString& rootUri) override;
    void openDocument(const QString& uri, const QString& text, const QString& langId) override;
    void closeDocument(const QString& uri) override;  // L3: didClose
    void changeDocument(const QString& uri, const QString& fullText) override;
    void didSave(const QString& uri) override;

    // 功能请求（异步，结果通过信号返回）
    void requestCompletion(const QString& uri, int line, int col) override;
    void requestDefinition(const QString& uri, int line, int col) override;
    void requestHover(const QString& uri, int line, int col) override;
    void requestReferences(const QString& uri, int line, int col) override;
    void requestSymbols(const QString& uri) override;
    void requestDiagnostics(const QString& uri) override;

    /// P0 C03: 请求跳转实现（textDocument/implementation）
    /// 响应格式与 definition 相同（Location/Location[]），复用 definitionReady 信号
    void requestImplementation(const QString& uri, int line, int col) override;

    // 状态查询
    bool isInitialized() const noexcept override;
    QString serverName() const noexcept override;

    // 注：信号已在 ILspClient 中声明，此处不重复定义
    // serverStarted/serverStopped/serverError/completionsReady/definitionReady/
    // hoverReady/diagnosticsReady/symbolsReady 均继承自 ILspClient

private:
    std::unique_ptr<QProcess> m_serverProcess;  // RAII：智能指针管理
    qint64 m_requestId = 0;
    QMap<qint64, QString> m_pendingRequests;  // requestId → method
    /// V2.1 C1 修复：documentSymbol 请求的 requestId → uri 映射
    /// 用于异步响应到达时精确路由到发起请求的文件，避免 m_currentRequestFile 被覆盖
    QMap<qint64, QString> m_symbolRequestUri;
    QByteArray m_buffer;                       // stdout 数据缓冲区
    bool m_initialized = false;
    QString m_serverNameStr;

    // V2.0: 文档版本号（LSP 规范要求 didChange 时递增，原硬编码为 2 不符合规范）
    int m_docVersion = 0;

    // F4: hover 请求 ID 跟踪 — 用于丢弃 stale 响应（鼠标已移动到新位置时旧响应不弹出）
    qint64 m_lastHoverRequestId = -1;
    // P0 C01: completion/definition/references 请求 ID 跟踪 — 推广 Hover stale 丢弃模式
    // 避免并发请求时旧响应覆盖新请求结果（如快速连续触发补全/跳转）
    qint64 m_lastCompletionRequestId = -1;
    qint64 m_lastDefinitionRequestId = -1;
    qint64 m_lastReferencesRequestId = -1;
    qint64 m_lastImplementationRequestId = -1;  // P0 C03: implementation stale 跟踪

    // P0 C01: 通用请求超时机制（默认 5s）
    static constexpr int kRequestTimeoutMs = 5000;
    /// 请求超时定时器（按 requestId 索引，超时后从 m_pendingRequests 移除并清理 stale 跟踪）
    QMap<qint64, QSharedPointer<QTimer>> m_timeoutTimers;

    /// @brief 安全创建服务器进程（RAII包装）
    std::unique_ptr<QProcess> createServerProcess();

    /// @brief 安全销毁服务器进程（先断开信号再释放）
    void destroyServerProcess();

    // JSON-RPC
    /// @brief 构建 JSON-RPC 2.0 请求消息
    QByteArray createRequest(const QString& method, const QJsonObject& params, qint64* outId = nullptr);

    /// @brief 构建 JSON-RPC 2.0 通知消息（无 id，无响应）
    QByteArray createNotification(const QString& method, const QJsonObject& params);

    /// @brief 解析服务端响应数据
    void parseResponse(const QByteArray& data);

    /// @brief 发送原始消息（添加 Content-Length 头部）
    void sendRawMessage(const QByteArray& message);

    /// @brief 处理单个完整的 JSON-RPC 消息
    void handleMessage(const QJsonObject& msg);

private slots:
    void onServerOutput();           // 读取 stdout
    void onServerError();            // 读取 stderr
    void onServerFinished(int exitCode, QProcess::ExitStatus exitStatus);
};

#endif // LSPCLIENT_H
