#include "controller/LspCoordinator.h"
#include "core/lsp/LspManager.h"
#include "core/lsp/LanguageRegistry.h"  // R3: 语言匹配（单一数据源）
#include "ui/editor/TextCompleter.h"
#include "ui/editor/MyTextEdit.h"
#include "Logger.hpp"

#include <QDir>

// ========== LspDiagnosticOverlay 类型转换（内部消化） ==========
// LspDiagnostic::Error=0 → Overlay::Error=1, Warning=1→2, Information=2→3, Hint=3→4

LspCoordinator::LspCoordinator(QObject* parent)
    : QObject(parent)
    , m_lspManager(new LspManager(this))
{
    // 连接 LspManager 信号 → 内部路由处理
    connect(m_lspManager, &LspManager::completionsReady,
            this, &LspCoordinator::onCompletionsReady);
    connect(m_lspManager, &LspManager::diagnosticsReady,
            this, &LspCoordinator::onDiagnosticsReady);
    connect(m_lspManager, &LspManager::definitionReady,
            this, &LspCoordinator::onDefinitionReady);
    connect(m_lspManager, &LspManager::hoverReady,
            this, &LspCoordinator::onHoverReady);
    connect(m_lspManager, &LspManager::referencesReady,
            this, &LspCoordinator::onReferencesReady);
    connect(m_lspManager, &LspManager::symbolsReady,
            this, &LspCoordinator::onSymbolsReady);
    connect(m_lspManager, &LspManager::serverError,
            this, &LspCoordinator::onServerError);
    connect(m_lspManager, &LspManager::serverNotAvailable,
            this, &LspCoordinator::serverNotAvailable);
    // R3: LSP 状态变化 → Coordinator 内部路由到匹配语言的编辑器
    // 不再转发给 Widget，消除 Widget 中的语言匹配逻辑（开闭原则）
    connect(m_lspManager, &LspManager::lspStateChanged,
            this, &LspCoordinator::onLspStateChanged);
}

LspCoordinator::~LspCoordinator() = default;

// ========== 依赖注入 ==========

void LspCoordinator::setTabBar(ITabWidget* tabBar) { m_tabBar = tabBar; }
void LspCoordinator::setCompleter(TextCompleter* completer) { m_completer = completer; }

// ========== LspManager 透传 ==========

bool LspCoordinator::openFile(const QString& filePath, const QString& content)
{ return m_lspManager->openFile(filePath, content); }

void LspCoordinator::documentChanged(const QString& filePath, const QString& content)
{ m_lspManager->documentChanged(filePath, content); }

void LspCoordinator::documentSaved(const QString& filePath)
{ m_lspManager->documentSaved(filePath); }

void LspCoordinator::closeFile(const QString& filePath)
{ m_lspManager->closeFile(filePath); }

void LspCoordinator::requestCompletion(const QString& filePath, int line, int col)
{ m_lspManager->requestCompletion(filePath, line, col); }

void LspCoordinator::requestHover(const QString& filePath, int line, int col)
{ m_lspManager->requestHover(filePath, line, col); }

void LspCoordinator::requestDefinition(const QString& filePath, int line, int col)
{ m_lspManager->requestDefinition(filePath, line, col); }

void LspCoordinator::requestImplementation(const QString& filePath, int line, int col)
{ m_lspManager->requestImplementation(filePath, line, col); }

void LspCoordinator::requestReferences(const QString& filePath, int line, int col)
{ m_lspManager->requestReferences(filePath, line, col); }

void LspCoordinator::requestSymbols(const QString& filePath)
{ m_lspManager->requestSymbols(filePath); }

bool LspCoordinator::hasServerForFile(const QString& filePath) const
{ return m_lspManager->hasServerForFile(filePath); }

bool LspCoordinator::isServerInitialized(const QString& filePath) const
{ return m_lspManager->isServerInitialized(filePath); }

QString LspCoordinator::workspaceRoot() const
{ return m_lspManager->workspaceRoot(); }

void LspCoordinator::setWorkspaceRoot(const QString& root)
{ m_lspManager->setWorkspaceRoot(root); }

// ========== 内部路由处理 ==========

void LspCoordinator::onCompletionsReady(const QString& filePath,
                                        const QList<LspCompletionItem>& items)
{
    // LSP 补全结果 → 注入 TextCompleter 候选列表
    LOG_DEBUG("[LspCoordinator] 补全结果: " << items.size()
              << " 项, file=" << filePath.toStdString());
    if (m_completer) {
        m_completer->setLspCompletionItems(items);
    }
}

void LspCoordinator::onDiagnosticsReady(const QString& filePath,
                                        const QList<LspDiagnostic>& diagnostics)
{
    // LSP 诊断 → 转换为编辑器覆盖层格式 → 分发到匹配 filePath 的编辑器
    LOG_DEBUG("[LspCoordinator] 诊断: " << diagnostics.size()
              << " 条, file=" << filePath.toStdString());
    if (!m_tabBar) return;

    // 类型转换：LspDiagnostic → LspDiagnosticOverlay
    QList<LspDiagnosticOverlay> overlays;
    overlays.reserve(diagnostics.size());
    for (const LspDiagnostic& d : diagnostics) {
        LspDiagnosticOverlay o;
        o.startLine = d.line;
        o.startCol = d.column;
        o.endLine = d.endLine;
        o.endCol = d.endColumn;
        o.severity = static_cast<LspDiagnosticOverlay::Severity>(static_cast<int>(d.severity) + 1);
        o.message = d.message;
        if (!d.source.isEmpty())
            o.message = QStringLiteral("[%1] %2").arg(d.source, d.message);
        overlays.append(o);
    }

    // P2-1: 遍历所有标签页，将诊断分发到匹配 filePath 的编辑器
    // clangd 后台索引会推送非当前标签页的诊断
    MyTextEdit* ed = findEditorByPath(filePath);
    if (ed) {
        ed->setDiagnostics(overlays);
    }
    // 未找到匹配的标签页（可能是头文件等未打开的诊断推送），静默忽略
}

void LspCoordinator::onDefinitionReady(const QString& filePath,
                                       const QString& uri, int line, int col)
{
    // 跳转定义 → 信号上抛给 Widget（涉及文件打开 + 光标定位 UI 交互）
    LOG_DEBUG("[LspCoordinator] 跳转定义: uri=" << uri.toStdString()
              << " line=" << line << " col=" << col);
    emit definitionReady(filePath, uri, line, col);
}

void LspCoordinator::onHoverReady(const QString& filePath,
                                  const QString& documentation)
{
    // 悬停文档 → 信号上抛给 Widget（涉及 QToolTip 显示）
    LOG_DEBUG("[LspCoordinator] 悬停文档: "
              << documentation.left(80).toStdString()
              << " file=" << filePath.toStdString());
    emit hoverReady(filePath, documentation);
}

void LspCoordinator::onReferencesReady(const QString& filePath,
                                       const QList<QVariantMap>& references)
{
    // 引用查找结果 → 信号上抛给 Widget（涉及对话框显示）
    LOG_DEBUG("[LspCoordinator] 引用结果: " << references.size()
              << " 处, file=" << filePath.toStdString());
    emit referencesReady(filePath, references);
}

void LspCoordinator::onSymbolsReady(const QString& filePath,
                                    const QList<QVariantMap>& symbols)
{
    // 文档符号 → 转发给当前编辑器的高亮器 + 信号上抛给侧边栏大纲
    LOG_DEBUG("[LspCoordinator] 文档符号: " << symbols.size()
              << " 个, file=" << filePath.toStdString());

    // 只更新当前标签页对应的编辑器（如果文件路径匹配）
    if (m_tabBar) {
        QString currentPath = m_tabBar->currentFilePath();
        if (currentPath == filePath) {
            MyTextEdit* ed = findEditorByPath(filePath);
            if (ed) {
                ed->setSemanticSymbols(symbols);
            }
        }
    }

    // 信号上抛：侧边栏大纲面板更新
    emit symbolsReady(filePath, symbols);
}

void LspCoordinator::onServerError(const QString& filePath, const QString& error)
{
    LOG_ERROR("[LspCoordinator] 服务器错误: " << error.toStdString()
              << " file=" << filePath.toStdString());
    emit serverError(filePath, error);
}

// ========== 辅助方法 ==========

MyTextEdit* LspCoordinator::findEditorByPath(const QString& filePath) const
{
    if (!m_tabBar || filePath.isEmpty()) return nullptr;

    // R3: 使用 ITabWidget::allEditors() 接口遍历，消除 dynamic_cast 向下转型
    for (const auto& pair : m_tabBar->allEditors()) {
        if (QDir::toNativeSeparators(pair.first) ==
            QDir::toNativeSeparators(filePath)) {
            // IEditorEdit 实现类是 MyTextEdit，用 dynamic_cast 安全转换
            return dynamic_cast<MyTextEdit*>(pair.second);
        }
    }
    return nullptr;
}

// R3: LSP 状态变化 → 遍历所有编辑器，更新匹配语言的高亮状态
// Coordinator 内部路由，Widget 不再参与语言匹配（开闭原则）
void LspCoordinator::onLspStateChanged(const QString& langId, LspHighlightState state)
{
    if (!m_tabBar) return;

    // 遍历所有编辑器，通过 LanguageRegistry 匹配语言
    for (const auto& pair : m_tabBar->allEditors()) {
        const QString& filePath = pair.first;
        if (filePath.isEmpty()) continue;

        // R3: 使用 LanguageRegistry 单一数据源匹配语言（消除硬编码 if-else）
        if (!LanguageRegistry::instance().matches(langId, filePath)) continue;

        // R3: 通过 IEditorEdit 接口直接调用，无需向下转型到 MyTextEdit
        if (pair.second) {
            pair.second->setLspHighlightState(state);
        }
    }
}
