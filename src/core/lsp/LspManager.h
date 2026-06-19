#ifndef LSPMANAGER_H
#define LSPMANAGER_H

#include "interfaces/lsp/ILspClient.h"

#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QList>

/// @brief LSP 管理器 — 门面模式 + 工厂模式
///
/// 统一管理多语言 LSP 客户端的生命周期和信号路由。
/// 上层（Widget）只依赖此门面，不直接操作 ILspClient 实例。
///
/// 设计模式：
/// - 门面模式：隐藏多 LspClient 实例的管理细节
/// - 工厂模式：按语言后缀创建对应 LspClient（clangd/pylsp/ts-langserver）
/// - 观察者模式：异步结果通过信号返回给上层
/// - 单一职责：Widget 不关心 LSP 协议细节，只管"文件打开/变更/补全请求"
///
/// 解耦要点：
/// - LspManager 持有 ILspClient 指针（依赖接口，不依赖实现）
/// - 文件路径 ↔ 语言ID ↔ LspClient 三层映射，互不耦合
/// - 诊断类型转换（LspDiagnostic → 编辑器覆盖层格式）由上层负责
class LspManager : public QObject
{
    Q_OBJECT

public:
    explicit LspManager(QObject* parent = nullptr);
    ~LspManager() override;

    // === 文件生命周期管理 ===

    /// @brief 文件打开时调用 — 按语言后缀启动对应 LSP 服务器并发送 didOpen
    /// @param filePath 文件绝对路径
    /// @param content 文件内容
    /// @return 是否成功启动（不支持的语言/未配置服务器路径返回 false）
    bool openFile(const QString& filePath, const QString& content);

    /// @brief 文件内容变更 — 发送 didChange（全文同步模式）
    void documentChanged(const QString& filePath, const QString& content);

    /// @brief 文件保存 — 发送 didSave
    void documentSaved(const QString& filePath);

    /// @brief 文件关闭 — 清理映射（不停止服务器，复用给同语言的其他文件）
    void closeFile(const QString& filePath);

    // === 功能请求（异步，结果通过信号返回）===

    /// @brief 请求代码补全
    void requestCompletion(const QString& filePath, int line, int col);

    /// @brief 请求跳转定义
    void requestDefinition(const QString& filePath, int line, int col);

    /// @brief 请求悬停信息
    void requestHover(const QString& filePath, int line, int col);

    /// @brief 请求文档符号列表（用于语义高亮 / 大纲视图）
    void requestSymbols(const QString& filePath);

    /// @brief 请求引用查找
    void requestReferences(const QString& filePath, int line, int col);

    // === 状态查询 ===

    /// @brief 指定文件是否有对应的 LSP 服务器在运行
    bool hasServerForFile(const QString& filePath) const;

    /// @brief 指定文件的语言服务器是否已初始化
    bool isServerInitialized(const QString& filePath) const;

    /// @brief 获取指定文件的语言ID
    QString langIdForFile(const QString& filePath) const;

    /// @brief 设置工作区根目录（用于 clangd 的 rootUri 和 compile_commands.json 查找）
    /// @param rootPath 工作区根目录绝对路径（通常为用户打开的文件夹）
    /// @note 由 Widget 层在打开文件夹时调用，LSP 服务器据此定位项目根目录
    void setWorkspaceRoot(const QString& rootPath);

    /// @brief 获取当前工作区根目录
    QString workspaceRoot() const { return m_workspaceRoot; }

signals:
    /// 异步响应信号（观察者模式，Widget 连接这些信号更新 UI）
    void completionsReady(const QString& filePath, const QList<LspCompletionItem>& items);
    void definitionReady(const QString& filePath, const QString& uri, int line, int col);
    void hoverReady(const QString& filePath, const QString& documentation);
    void diagnosticsReady(const QString& filePath, const QList<LspDiagnostic>& diagnostics);
    void symbolsReady(const QString& filePath, const QList<QVariantMap>& symbols);
    /// L17: 引用查找结果（每个 QVariantMap 包含 uri/range/start.line/start.character）
    void referencesReady(const QString& filePath, const QList<QVariantMap>& references);
    void serverError(const QString& filePath, const QString& error);
    /// 未检测到语言服务器时发射，UI 可据此弹窗提示用户手动配置
    void serverNotAvailable(const QString& langId);

private:
    /// 按语言ID获取或创建 LspClient（工厂方法）
    ILspClient* getOrCreateClient(const QString& langId);

    /// 文件后缀 → 语言ID 映射（cpp→cpp, py→python, js→javascript）
    static QString langIdForSuffix(const QString& suffix);

    /// 语言ID → 文件后缀列表（用于 didOpen 的 langId 参数）
    static QString lspLangId(const QString& langId);

    /// 获取语言服务器命令和参数（从 ConfigManager 读取）
    QString serverCommand(const QString& langId) const;
    QStringList serverArgs(const QString& langId) const;

    /// 自动检测系统中的语言服务器路径（PATH 搜索 + 常见安装路径）
    /// 检测成功则回写配置并返回路径，失败返回空字符串
    static QString autoDetectServer(const QString& langId);

    /// 文件路径转 file:// URI
    static QString filePathToUri(const QString& filePath);

    /// file:// URI 转文件路径
    static QString uriToFilePath(const QString& uri);

    /// 从文件路径推断项目根目录（向上查找 CMakeLists.txt / .git / .clangd / compile_commands.json）
    /// @param filePath 文件路径（起点）
    /// @param workspaceRoot 工作区根目录（优先使用，为空时才执行向上查找）
    /// @return 推断的项目根目录绝对路径
    static QString inferProjectRoot(const QString& filePath, const QString& workspaceRoot);

private slots:
    // LspClient 响应信号 → 转发为带 filePath 的信号（路由层）
    void onCompletionsReady(const QList<LspCompletionItem>& items);
    void onDefinitionReady(const QString& uri, int line, int col);
    void onHoverReady(const QString& documentation, const QPoint& pos);
    void onDiagnosticsReady(const QString& uri, const QList<LspDiagnostic>& diagnostics);
    void onSymbolsReady(const QList<QVariantMap>& symbols);
    void onReferencesReady(const QList<QVariantMap>& references);
    void onServerError(const QString& error);
    /// LspClient initialize 握手完成 → flush 该语言缓存的 didOpen（修复 LSP 时序）
    void onClientInitialized();

private:
    QMap<QString, ILspClient*> m_clients;       // langId → LspClient 实例
    QMap<QString, QString>     m_fileToLangId;  // filePath → langId 映射
    QMap<QString, QString>     m_uriToFilePath; // file:// URI → filePath（用于响应路由）
    QString m_currentRequestFile;               // 当前请求的文件路径（用于响应路由）

    /// 缓存的 didOpen 信息：langId → (uri, text, langId) 列表
    /// 服务器 initialize 握手完成前缓存的 didOpen，握手后由 onClientInitialized flush
    struct PendingOpen { QString uri; QString text; QString langId; };
    QMap<QString, QList<PendingOpen>> m_pendingOpens;

    /// 自动检测结果缓存（langId → 路径），避免每次 openFile 都搜索 PATH
    mutable QMap<QString, QString> m_detectedCache;

    /// 工作区根目录（由 Widget 层设置，用于 clangd rootUri 和编译数据库查找）
    QString m_workspaceRoot;
};

#endif // LSPMANAGER_H
