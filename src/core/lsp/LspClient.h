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
    void changeDocument(const QString& uri, const QString& fullText) override;
    void didSave(const QString& uri) override;

    // 功能请求（异步，结果通过信号返回）
    void requestCompletion(const QString& uri, int line, int col) override;
    void requestDefinition(const QString& uri, int line, int col) override;
    void requestHover(const QString& uri, int line, int col) override;
    void requestReferences(const QString& uri, int line, int col) override;
    void requestSymbols(const QString& uri) override;
    void requestDiagnostics(const QString& uri) override;

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
    QByteArray m_buffer;                       // stdout 数据缓冲区
    bool m_initialized = false;
    QString m_serverNameStr;

    /// @brief 安全创建服务器进程（RAII包装）
    std::unique_ptr<QProcess> createServerProcess();

    /// @brief 安全销毁服务器进程（先断开信号再释放）
    void destroyServerProcess();

    // JSON-RPC
    /// @brief 构建 JSON-RPC 2.0 请求消息
    QByteArray createRequest(const QString& method, const QJsonObject& params);

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
