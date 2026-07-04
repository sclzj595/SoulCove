#ifndef ILSPCLIENT_H
#define ILSPCLIENT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPoint>
#include <QVariantMap>

/// @brief LSP 诊断信息结构
/// 定义在接口层，上层代码（MyTextEdit 诊断覆盖层）只依赖此结构，不依赖 LspClient 实现
struct LspDiagnostic {
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
    enum Severity { Error, Warning, Information, Hint } severity = Error;
    QString message;
    QString code;        // 错误码
    QString source;      // 来源 "pylsp" / "clangd"
};

/// @brief LSP 补全项结构
/// 定义在接口层，TextCompleter 等上层模块只依赖此结构
struct LspCompletionItem {
    QString label;          // 显示文本
    QString kind;           // 类型: Function/Variable/Class/...
    QString detail;         // 详情（函数签名等）
    QString documentation;  // 文档
    QString insertText;     // 要插入的文本
    QString sortText;       // 排序文本
};

/// @brief LSP 文档符号结构（用于语义高亮 / 大纲视图）
struct LspDocumentSymbol {
    QString name;           // 符号名称
    QString kind;           // 符号类型: Function/Class/Variable/...
    int startLine = 0;
    int startCol = 0;
    int endLine = 0;
    int endCol = 0;
    QList<LspDocumentSymbol> children;  // 子符号（嵌套作用域）
};

/// @brief LSP 客户端抽象接口
///
/// 遵循依赖倒置原则（DIP）：上层代码（Widget/TextCompleter）只依赖此接口，
/// 不依赖 QProcess/JSON-RPC 具体实现。
///
/// 设计模式：
/// - 策略模式：可替换为本地 LspClient 或远程 SSH 隧道 LspClient
/// - 观察者模式：异步结果通过信号返回
/// - 工厂模式：由 LspClientFactory 按语言创建对应实例
///
/// 参考 ISshClient 接口模式：纯虚接口 + 信号声明 + Q_OBJECT
class ILspClient : public QObject
{
    Q_OBJECT

public:
    explicit ILspClient(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ILspClient() = default;

    // === 连接管理 ===
    /// @brief 启动语言服务器进程
    /// @param command 可执行文件路径（如 "pylsp" 或 "clangd"）
    /// @param args 命令行参数
    /// @param workingDir 工作目录（通常为项目根目录）
    /// @return 是否成功启动
    virtual bool startServer(const QString& command, const QStringList& args,
                             const QString& workingDir) = 0;

    /// @brief 停止语言服务器进程
    virtual void stopServer() = 0;

    /// @brief 查询服务器是否正在运行
    virtual bool isRunning() const = 0;

    // === LSP 协议方法 ===

    /// @brief 发送 initialize 请求（LSP 握手第一步）
    virtual void initialize(const QString& rootUri) = 0;

    /// @brief 发送 textDocument/didOpen 通知（打开文档）
    virtual void openDocument(const QString& uri, const QString& text, const QString& langId) = 0;

    /// @brief L3: 发送 textDocument/didClose 通知（关闭文档）
    /// 避免闲置标签重新激活时重复 didOpen 导致 clangd 重建 preamble
    virtual void closeDocument(const QString& uri) = 0;

    /// @brief 发送 textDocument/didChange 通知（全文变更）
    virtual void changeDocument(const QString& uri, const QString& fullText) = 0;

    /// @brief 发送 textDocument/didSave 通知（保存文档）
    virtual void didSave(const QString& uri) = 0;

    // === 功能请求（异步，结果通过信号返回）===

    /// @brief 请求代码补全
    virtual void requestCompletion(const QString& uri, int line, int col) = 0;

    /// @brief 请求跳转定义
    virtual void requestDefinition(const QString& uri, int line, int col) = 0;

    /// @brief 请求悬停信息
    virtual void requestHover(const QString& uri, int line, int col) = 0;

    /// @brief 请求引用查找
    virtual void requestReferences(const QString& uri, int line, int col) = 0;

    /// @brief P0 C03: 请求跳转实现（textDocument/implementation）
    /// 响应格式与 definition 相同，复用 definitionReady 信号
    virtual void requestImplementation(const QString& uri, int line, int col) = 0;

    /// @brief 请求文档符号列表（用于语义高亮 / 大纲视图）
    virtual void requestSymbols(const QString& uri) = 0;

    /// @brief 请求诊断信息（主动拉取，部分服务器需要）
    virtual void requestDiagnostics(const QString& uri) = 0;

    // === 状态查询 ===
    /// @brief 是否已完成 initialize 握手
    virtual bool isInitialized() const = 0;

    /// @brief 返回服务器名称（来自 initialize 响应）
    virtual QString serverName() const = 0;

signals:
    /// 服务器生命周期
    void serverStarted();
    void serverStopped();
    void serverError(const QString& error);
    /// initialize 握手完成（收到 initialize 响应并发送 initialized 通知后发射）
    /// 上层（LspManager）据此发送缓存的 didOpen，确保 LSP 协议时序正确
    void initialized();

    /// 异步响应信号（观察者模式）
    void completionsReady(const QList<LspCompletionItem>& items);
    void definitionReady(const QString& uri, int line, int col);
    void hoverReady(const QString& documentation, const QPoint& pos);
    void diagnosticsReady(const QString& uri, const QList<LspDiagnostic>& diagnostics);
    /// V2.1 C1 修复：增加 uri 参数，按 requestId 精确路由响应文件
    /// 避免共享 m_currentRequestFile 导致跨请求类型响应归属错误
    void symbolsReady(const QString& uri, const QList<QVariantMap>& symbols);
    /// L17: 引用查找结果（每个 QVariantMap 包含 uri/range，与 LSP Location 结构一致）
    void referencesReady(const QList<QVariantMap>& references);
};

#endif // ILSPCLIENT_H
