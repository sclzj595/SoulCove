#ifndef LSPCOORDINATOR_H
#define LSPCOORDINATOR_H

#include "interfaces/lsp/ILspClient.h"
#include "interfaces/ui/ITabWidget.h"
#include "core/lsp/LspTypes.h"  // R1: LspHighlightState 枚举

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantMap>

class LspManager;
class TextCompleter;
class MyTextEdit;

/// @brief LSP 信号路由协调器
///
/// 职责：从 Widget 下沉 LSP 响应信号的路由逻辑，
/// 将 LspManager 的异步响应分发到正确的编辑器/补全器。
///
/// 设计说明：
/// - 协调器模式：收敛 LspManager ↔ 编辑器/补全器 之间的多对多路由
/// - 依赖注入：通过 setTabBar/setCompleter 注入依赖，不持有 Widget
/// - 信号转发：UI 级响应（跳转/引用/悬停）通过信号上抛给 Widget
///
/// 路由策略：
/// - completionsReady → TextCompleter（内部消化，不上抛）
/// - diagnosticsReady → 按 filePath 匹配编辑器（内部消化）
/// - symbolsReady → 编辑器 + symbolsReady 信号（侧边栏大纲更新）
/// - definitionReady/hoverReady/referencesReady → 信号上抛（UI 交互）
class LspCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit LspCoordinator(QObject* parent = nullptr);
    ~LspCoordinator() override;

    // === 依赖注入 ===
    void setTabBar(ITabWidget* tabBar);
    void setCompleter(TextCompleter* completer);

    // === LspManager 访问（门面透传） ===
    LspManager* manager() const { return m_lspManager; }

    // === 文件生命周期（透传 LspManager） ===
    bool openFile(const QString& filePath, const QString& content);
    void documentChanged(const QString& filePath, const QString& content);
    void documentSaved(const QString& filePath);
    void closeFile(const QString& filePath);

    // === 异步请求（透传 LspManager） ===
    void requestCompletion(const QString& filePath, int line, int col);
    void requestHover(const QString& filePath, int line, int col);
    void requestDefinition(const QString& filePath, int line, int col);
    void requestReferences(const QString& filePath, int line, int col);
    void requestSymbols(const QString& filePath);
    /// P0 C03: 请求跳转实现（透传 LspManager）
    void requestImplementation(const QString& filePath, int line, int col);

    // === 状态查询（透传 LspManager） ===
    bool hasServerForFile(const QString& filePath) const;
    bool isServerInitialized(const QString& filePath) const;
    QString workspaceRoot() const;
    void setWorkspaceRoot(const QString& root);

signals:
    // === UI 级响应信号（需 Widget 处理的交互） ===
    void definitionReady(const QString& filePath, const QString& uri, int line, int col);
    void hoverReady(const QString& filePath, const QString& documentation);
    void referencesReady(const QString& filePath, const QList<QVariantMap>& references);
    void symbolsReady(const QString& filePath, const QList<QVariantMap>& symbols);
    void serverError(const QString& filePath, const QString& error);
    void serverNotAvailable(const QString& langId);
    // R3: lspStateChanged 不再对外暴露 — Coordinator 内部路由到编辑器
    // Widget 无需连接此信号，消除语言匹配逻辑

private slots:
    // === 内部路由处理 ===
    void onCompletionsReady(const QString& filePath, const QList<LspCompletionItem>& items);
    void onDiagnosticsReady(const QString& filePath, const QList<LspDiagnostic>& diagnostics);
    void onDefinitionReady(const QString& filePath, const QString& uri, int line, int col);
    void onHoverReady(const QString& filePath, const QString& documentation);
    void onReferencesReady(const QString& filePath, const QList<QVariantMap>& references);
    void onSymbolsReady(const QString& filePath, const QList<QVariantMap>& symbols);
    void onServerError(const QString& filePath, const QString& error);
    /// R3: LSP 状态变化 → 遍历所有编辑器，更新匹配语言的高亮状态
    /// Coordinator 内部路由，Widget 不再参与语言匹配
    void onLspStateChanged(const QString& langId, LspHighlightState state);

private:
    /// 按 filePath 在标签页中查找对应编辑器（路径标准化比较）
    MyTextEdit* findEditorByPath(const QString& filePath) const;

private:
    LspManager*     m_lspManager;
    ITabWidget*     m_tabBar = nullptr;
    TextCompleter*  m_completer = nullptr;
};

#endif // LSPCOORDINATOR_H
