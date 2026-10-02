#include "core/lsp/LspManager.h"
#include "core/lsp/LspClient.h"
#include "core/lsp/LanguageRegistry.h"  // R2: 语言注册表（单一数据源）
#include "core/config/ConfigManager.h"
#include "Logger.hpp"

#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QProcessEnvironment>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>

// ============================================================
// 构造 / 析构
// ============================================================

LspManager::LspManager(QObject* parent)
    : QObject(parent)
{
}

LspManager::~LspManager()
{
    // P0-2: 设置关闭标志，阻止 serverStopped 触发自动重连
    m_shuttingDown = true;
    // RAII：析构时停止所有语言服务器
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value()) {
            it.value()->stopServer();
            it.value()->deleteLater();
        }
    }
    m_clients.clear();
}

// ============================================================
// 文件生命周期管理
// ============================================================

bool LspManager::openFile(const QString& filePath, const QString& content)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();
    QString langId = langIdForSuffix(suffix);

    // L3: 无扩展名文件（如 C++ 标准库头文件 <vector> <algorithm>）默认按 cpp 处理
    // 这类文件通常位于 MinGW/MSVC 的 include 目录中，跳转定义打开后需要 LSP 支持
    if (langId.isEmpty() && suffix.isEmpty()) {
        langId = QStringLiteral("cpp");
        LOG_DEBUG("[LspManager] 无扩展名文件按 cpp 处理: " << filePath.toStdString());
    }

    // 不支持的语言 — 静默跳过（不是错误）
    if (langId.isEmpty()) {
        LOG_DEBUG("[LspManager] 不支持的语言后缀: " << suffix.toStdString());
        return false;
    }

    // 检查是否配置了语言服务器路径（含自动检测兜底）
    QString cmd = serverCommand(langId);
    if (cmd.isEmpty()) {
        LOG_INFO("[LspManager] 未检测到 " << langId.toStdString()
                  << " 语言服务器，LSP 功能（跳转/悬停/引用）将不可用");
        emit serverNotAvailable(langId);
        return false;
    }

    // 获取或创建 LspClient（工厂模式：按语言复用）
    ILspClient* client = getOrCreateClient(langId);
    if (!client) {
        LOG_ERROR("[LspManager] 无法创建 LspClient for " << langId.toStdString());
        return false;
    }

    // 记录文件 → 语言映射
    m_fileToLangId[filePath] = langId;
    QString uri = filePathToUri(filePath);
    m_uriToFilePath[uri] = filePath;

    // 服务器未运行 → 启动 + 初始化
    if (!client->isRunning()) {
        // L1: 先推断项目根目录，再传给 serverArgs() 用于查找 compile_commands.json
        // 修复根因：原代码 serverArgs() 依赖 m_workspaceRoot，但用户可能未通过"打开文件夹"设置
        // 导致 clangd 找不到 compile_commands.json，项目内 include 跳转失效
        QString workingDir = inferProjectRoot(filePath, m_workspaceRoot);
        if (workingDir.isEmpty()) {
            // 最终 fallback: 文件所在目录
            workingDir = QFileInfo(filePath).absolutePath();
            LOG_WARN("[LspManager] 无法推断项目根目录，使用文件所在目录作为 fallback: "
                      << workingDir.toStdString());
        }

        QStringList args = serverArgs(langId, workingDir);
        LOG_INFO("[LspManager] 启动 " << langId.toStdString() << " 语言服务器: " << cmd.toStdString());
        if (!client->startServer(cmd, args, workingDir)) {
            LOG_ERROR("[LspManager] 启动失败: " << cmd.toStdString());
            return false;
        }

        // 等待服务器就绪后初始化（异步，initialize 响应到达后标记 m_initialized）
        QString rootUri = filePathToUri(workingDir);
        client->initialize(rootUri);

        // 修复 LSP 时序：didOpen 必须在 initialized 通知之后发送。
        // 此处服务器尚未完成握手，将 didOpen 缓存，等 initialized 信号到达后由
        // onClientInitialized flush（避免 clangd 在握手前收到 didOpen 而丢弃）
        QString lspLang = lspLangId(langId);
        m_pendingOpens[langId].append({uri, content, lspLang});
        LOG_DEBUG("[LspManager] 缓存 didOpen（等待握手完成）: " << filePath.toStdString());
    } else {
        // 服务器已运行：若已初始化则直接发 didOpen，否则缓存
        QString lspLang = lspLangId(langId);
        if (client->isInitialized()) {
            client->openDocument(uri, content, lspLang);
        } else {
            m_pendingOpens[langId].append({uri, content, lspLang});
            LOG_DEBUG("[LspManager] 缓存 didOpen（服务器运行中但未完成握手）: " << filePath.toStdString());
        }
    }

    LOG_DEBUG("[LspManager] 已打开文档: " << filePath.toStdString() << " (langId=" << lspLangId(langId).toStdString() << ")");
    return true;
}

void LspManager::documentChanged(const QString& filePath, const QString& content)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;  // 未打开 / 不支持的语言

    ILspClient* client = m_clients.value(it.value());
    if (!client || !client->isInitialized()) return;

    client->changeDocument(filePathToUri(filePath), content);
}

void LspManager::documentSaved(const QString& filePath)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;

    ILspClient* client = m_clients.value(it.value());
    if (!client || !client->isInitialized()) return;

    client->didSave(filePathToUri(filePath));
}

void LspManager::closeFile(const QString& filePath)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;

    // L3: 发送 didClose（LSP 协议要求），避免闲置标签重新激活时重复 didOpen
    // clangd 收到 didClose 后释放 preamble 缓存，重新 didOpen 时才重建
    ILspClient* client = m_clients.value(it.value());
    if (client && client->isInitialized()) {
        client->closeDocument(filePathToUri(filePath));
    }

    m_fileToLangId.erase(it);
    QString uri = filePathToUri(filePath);
    m_uriToFilePath.remove(uri);
}

// ============================================================
// 功能请求
// ============================================================

void LspManager::requestCompletion(const QString& filePath, int line, int col)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;

    ILspClient* client = m_clients.value(it.value());
    if (!client || !client->isInitialized()) return;

    m_currentCompletionFile = filePath;  // P0 C01: 按类型独立跟踪（响应路由用）
    client->requestCompletion(filePathToUri(filePath), line, col);
}

ILspClient* LspManager::resolveInitializedClient(const QString& filePath)
{
    // Bug2: 统一处理文件→客户端解析，未注册时按后缀回退注册，避免请求方法静默返回
    QString langId;
    auto it = m_fileToLangId.find(filePath);
    if (it != m_fileToLangId.end()) {
        langId = it.value();
    } else {
        // 后缀回退：推断语言 ID（无扩展名文件默认 cpp）
        QString suffix = QFileInfo(filePath).suffix().toLower();
        langId = langIdForSuffix(suffix);
        if (langId.isEmpty() && suffix.isEmpty()) {
            langId = QStringLiteral("cpp");
        }
        if (langId.isEmpty()) {
            LOG_DEBUG("[LspManager] 文件语言不支持: " << filePath.toStdString());
            return nullptr;
        }
        // 注册文件映射（不发送 didOpen，clangd 可处理未 didOpen 文件的请求）
        m_fileToLangId[filePath] = langId;
        QString uri = filePathToUri(filePath);
        m_uriToFilePath[uri] = filePath;
        LOG_DEBUG("[LspManager] 按后缀回退注册文件 " << filePath.toStdString()
                  << " → " << langId.toStdString());
    }

    ILspClient* client = m_clients.value(langId);
    if (!client) {
        LOG_WARN("[LspManager] 无对应客户端 (" << filePath.toStdString() << ")");
        return nullptr;
    }
    if (!client->isInitialized()) {
        LOG_WARN("[LspManager] 服务器未完成握手，请求被丢弃 (" << filePath.toStdString() << ")");
        return nullptr;
    }
    return client;
}

void LspManager::requestDefinition(const QString& filePath, int line, int col)
{
    // Bug2: 使用 resolveInitializedClient 处理未注册文件的回退注册，避免静默返回
    ILspClient* client = resolveInitializedClient(filePath);
    if (!client) return;

    m_currentDefinitionFile = filePath;  // P0 C01: 按类型独立跟踪
    client->requestDefinition(filePathToUri(filePath), line, col);
    LOG_DEBUG("[LspManager] requestDefinition 已发送: " << filePath.toStdString()
              << " line=" << line << " col=" << col);
}

void LspManager::requestImplementation(const QString& filePath, int line, int col)
{
    // P0 C03: 请求跳转实现 — 复用 definition 的路由和响应处理
    // Bug2: 使用 resolveInitializedClient 处理未注册文件的回退注册
    ILspClient* client = resolveInitializedClient(filePath);
    if (!client) return;

    // implementation 响应复用 definitionReady 信号，使用 m_currentDefinitionFile 路由
    m_currentDefinitionFile = filePath;
    client->requestImplementation(filePathToUri(filePath), line, col);
}

void LspManager::requestHover(const QString& filePath, int line, int col)
{
    // Bug2: 使用 resolveInitializedClient 处理未注册文件的回退注册，避免静默返回
    ILspClient* client = resolveInitializedClient(filePath);
    if (!client) return;

    // F4: hover 请求文件独立跟踪，不与 completion/definition 等共享 m_currentRequestFile
    m_currentHoverFile = filePath;
    client->requestHover(filePathToUri(filePath), line, col);
}

void LspManager::requestSymbols(const QString& filePath)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;

    ILspClient* client = m_clients.value(it.value());
    if (!client || !client->isInitialized()) return;

    // documentSymbol 已通过 requestId → uri 精确路由（LspClient::m_symbolRequestUri），无需 m_currentRequestFile
    client->requestSymbols(filePathToUri(filePath));
}

void LspManager::requestReferences(const QString& filePath, int line, int col)
{
    // Bug2: 使用 resolveInitializedClient 处理未注册文件的回退注册，避免静默返回
    ILspClient* client = resolveInitializedClient(filePath);
    if (!client) return;

    m_currentReferencesFile = filePath;  // P0 C01: 按类型独立跟踪
    client->requestReferences(filePathToUri(filePath), line, col);
}

// ============================================================
// 状态查询
// ============================================================

bool LspManager::hasServerForFile(const QString& filePath) const
{
    // 1. 检查文件是否已注册
    auto it = m_fileToLangId.find(filePath);
    if (it != m_fileToLangId.end()) {
        ILspClient* client = m_clients.value(it.value());
        return client && client->isRunning();
    }

    // L3: 文件未注册，检查同语言是否有运行中的服务器
    // 场景：从 .cpp 跳转到标准库头文件（无扩展名），头文件可能尚未 didOpen
    // 但 cpp 服务器已在运行，可以服务该文件
    QString suffix = QFileInfo(filePath).suffix().toLower();
    QString langId = langIdForSuffix(suffix);
    // 无扩展名文件（标准库头文件）默认按 cpp 处理
    if (langId.isEmpty() && suffix.isEmpty()) {
        langId = QStringLiteral("cpp");
    }
    if (!langId.isEmpty()) {
        ILspClient* client = m_clients.value(langId);
        return client && client->isRunning();
    }

    return false;
}

bool LspManager::isServerInitialized(const QString& filePath) const
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return false;

    ILspClient* client = m_clients.value(it.value());
    return client && client->isInitialized();
}

QString LspManager::langIdForFile(const QString& filePath) const
{
    return m_fileToLangId.value(filePath);
}

// ============================================================
// 私有方法 — 工厂 / 映射 / 转换
// ============================================================

ILspClient* LspManager::getOrCreateClient(const QString& langId)
{
    // 工厂模式：按语言ID复用 LspClient 实例
    auto it = m_clients.find(langId);
    if (it != m_clients.end() && it.value()) {
        return it.value();
    }

    // 创建新实例（依赖 ILspClient 接口，实际类型为 LspClient）
    ILspClient* client = new LspClient(this);
    if (!client) return nullptr;

    // 连接响应信号 → LspManager 路由槽
    connect(client, &ILspClient::completionsReady,
            this, &LspManager::onCompletionsReady);
    connect(client, &ILspClient::definitionReady,
            this, &LspManager::onDefinitionReady);
    connect(client, &ILspClient::hoverReady,
            this, &LspManager::onHoverReady);
    connect(client, &ILspClient::diagnosticsReady,
            this, &LspManager::onDiagnosticsReady);
    connect(client, &ILspClient::symbolsReady,
            this, &LspManager::onSymbolsReady);
    connect(client, &ILspClient::referencesReady,
            this, &LspManager::onReferencesReady);
    connect(client, &ILspClient::serverError,
            this, &LspManager::onServerError);
    // P0-2: 进程崩溃/异常退出 → 自动重连
    connect(client, &ILspClient::serverStopped,
            this, [this, langId]() { onServerStopped(langId); });
    // 握手完成 → flush 缓存的 didOpen（修复 LSP 时序）
    connect(client, &ILspClient::initialized,
            this, &LspManager::onClientInitialized);

    m_clients[langId] = client;

    // P1-3: 服务器启动 → 状态变为 Initializing（高亮器启用启发式兜底）
    emit lspStateChanged(langId, LspHighlightState::Initializing);
    return client;
}

QString LspManager::langIdForSuffix(const QString& suffix)
{
    // R2: 委托给 LanguageRegistry（单一数据源，消除重复映射）
    return LanguageRegistry::instance().langIdForSuffix(suffix);
}

QString LspManager::lspLangId(const QString& langId)
{
    // R2: 委托给 LanguageRegistry
    return LanguageRegistry::instance().lspLanguageId(langId);
}

QString LspManager::serverCommand(const QString& langId) const
{
    // P3-M01 子项3: 远程 LSP 模式 — cpp/c 语言使用远程 clangd 路径
    if (isRemoteLspMode() && (langId == "cpp" || langId == "c")) {
        if (!m_remoteClangdPath.isEmpty()) {
            return m_remoteClangdPath;
        }
        // 远程路径为空时回退到 ConfigManager 中持久化的 remoteClangdPath
        if (!m_remoteSessionName.isEmpty()) {
            QString persisted = ConfigManager::instance().getValue(
                QStringLiteral("SSH/%1/remoteClangdPath").arg(m_remoteSessionName)).toString();
            if (!persisted.isEmpty()) {
                return persisted;
            }
        }
        // 远程路径缺失 → 继续走本地检测流程（降级）
        LOG_WARN("[LspManager] 远程 clangd 路径为空，回退本地检测");
    }

    // 1. 优先从 ConfigManager 读取用户手动配置的路径
    ConfigManager& cfg = ConfigManager::instance();
    QString configured;
    if (langId == "cpp" || langId == "c")
        configured = cfg.lspCppPath();
    else if (langId == "python")
        configured = cfg.lspPythonPath();
    else if (langId == "javascript" || langId == "typescript")
        configured = cfg.lspJsPath();
    else
        return QString();

    if (!configured.isEmpty())
        return configured;

    // 2. 配置为空 → 查询自动检测缓存
    auto it = m_detectedCache.constFind(langId);
    if (it != m_detectedCache.constEnd())
        return it.value();

    // 3. 缓存未命中 → 执行自动检测（PATH 搜索 + 常见安装路径）
    QString detected = autoDetectServer(langId);
    if (!detected.isEmpty()) {
        m_detectedCache[langId] = detected;
        LOG_INFO("[LspManager] 自动检测到 " << langId.toStdString()
                  << " 语言服务器: " << detected.toStdString());
        // 回写配置，避免后续重复检测
        if (langId == "cpp" || langId == "c")
            cfg.setValue("LSP/cppServer", detected);
        else if (langId == "python")
            cfg.setValue("LSP/pythonServer", detected);
        else if (langId == "javascript" || langId == "typescript")
            cfg.setValue("LSP/jsServer", detected);
    } else {
        // 缓存空结果，避免同一会话内反复搜索 PATH
        m_detectedCache[langId] = QString();
    }
    return detected;
}

QString LspManager::autoDetectServer(const QString& langId)
{
    // 确定要搜索的可执行文件名
    QStringList candidateNames;
    if (langId == "cpp" || langId == "c") {
        candidateNames << QStringLiteral("clangd") << QStringLiteral("clangd.exe");
    } else if (langId == "python") {
        candidateNames << QStringLiteral("pylsp") << QStringLiteral("pylsp.exe")
                       << QStringLiteral("python-language-server") << QStringLiteral("python-language-server.exe");
    } else if (langId == "javascript" || langId == "typescript") {
        candidateNames << QStringLiteral("typescript-language-server")
                       << QStringLiteral("typescript-language-server.cmd")
                       << QStringLiteral("typescript-language-server.exe");
    } else {
        return QString();
    }

    // 1. 通过 QStandardPaths::findExecutable 搜索系统 PATH
    for (const QString& name : candidateNames) {
        QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty() && QFileInfo(found).isExecutable()) {
            LOG_DEBUG("[LspManager] PATH 搜索命中: " << found.toStdString());
            return found;
        }
    }

    // 2. 搜索常见安装路径（Windows 特有）
#ifdef Q_OS_WIN
    QStringList commonDirs;
    // LLVM 官方安装
    commonDirs << QStringLiteral("C:/Program Files/LLVM/bin")
               << QStringLiteral("C:/Program Files (x86)/LLVM/bin")
               << QStringLiteral("C:/Program Files/clangd/bin")
               << QStringLiteral("C:/llvm/bin");
    // Qt Creator 自带 clangd（Qt Creator 捆绑发行，路径固定为 <QT>/Tools/QtCreator/bin/clang/bin）
    // 常见 Qt 安装前缀（含 IDE 集成环境如 QT-IDE.2 等非标准路径）
    QStringList qtPrefixes;
    qtPrefixes << QStringLiteral("C:/Qt")
               << QStringLiteral("D:/Qt")
               << QStringLiteral("F:/Qt")
               << QStringLiteral("C:/IDE") << QStringLiteral("D:/IDE") << QStringLiteral("F:/IDE");
    QString userProfile = QProcessEnvironment::systemEnvironment().value(QStringLiteral("USERPROFILE"));
    if (!userProfile.isEmpty()) {
        qtPrefixes << (userProfile + QStringLiteral("/Qt"));
        // 扫描用户盘符下的 IDE.* 目录（匹配 F:\IDE.2\QT 这类路径）
        for (const char drive : {'C', 'D', 'E', 'F', 'G'}) {
            QDir driveDir(QString::fromLatin1("%1:/IDE", drive));
            if (driveDir.exists()) {
                for (const auto& entry : driveDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    // 检查 entry 下是否有 Tools/QtCreator 或直接有 QT 子目录
                    QString maybeQt = entry.absoluteFilePath() + QStringLiteral("/QT");
                    if (QFileInfo(maybeQt).isDir()) qtPrefixes << maybeQt;
                    // 也检查 entry 本身是否是 Qt 安装目录
                    if (QFileInfo(entry.absoluteFilePath() + QStringLiteral("/Tools")).isDir())
                        qtPrefixes << entry.absoluteFilePath();
                }
            }
        }
    }
    // 从环境变量 / 注册表推测更多 Qt 安装路径
    QString qtdir = QProcessEnvironment::systemEnvironment().value(QStringLiteral("QTDIR"));
    if (!qtdir.isEmpty()) qtPrefixes << qtdir;
    for (const QString& prefix : qtPrefixes) {
        QDir toolsDir(prefix + QStringLiteral("/Tools/QtCreator"));
        if (toolsDir.exists()) {
            // 遍历所有 QtCreator 版本目录（如 14.0.0、15.0.0 等）
            for (const auto& entry : toolsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                QString clangPath = entry.absoluteFilePath() + QStringLiteral("/bin/clang/bin");
                if (QFileInfo(clangPath).isDir()) {
                    commonDirs << clangPath;
                }
            }
            // 也检查无版本号的直装路径
            commonDirs << (prefix + QStringLiteral("/Tools/QtCreator/bin/clang/bin"));
        }
    }
    // 用户级 scoop / chocolatey 安装路径
    if (!userProfile.isEmpty()) {
        commonDirs << (userProfile + QStringLiteral("/scoop/apps/llvm/current/bin"))
                   << (userProfile + QStringLiteral("/scoop/apps/clangd/current/bin"))
                   << (userProfile + QStringLiteral("/scoop/shims"));
    }
    QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    if (!localAppData.isEmpty()) {
        commonDirs << (localAppData + QStringLiteral("/Programs/clangd/bin"));
    }
    for (const QString& dir : commonDirs) {
        for (const QString& name : candidateNames) {
            QString candidate = dir + QStringLiteral("/") + name;
            if (QFileInfo::exists(candidate)) {
                LOG_DEBUG("[LspManager] 常见路径命中: " << candidate.toStdString());
                return candidate;
            }
        }
    }
#else
    // Unix 系统常见路径
    QStringList commonDirs;
    commonDirs << QStringLiteral("/usr/bin") << QStringLiteral("/usr/local/bin")
               << QStringLiteral("/opt/homebrew/bin") << QStringLiteral("/usr/local/opt/llvm/bin");
    QString home = QProcessEnvironment::systemEnvironment().value(QStringLiteral("HOME"));
    if (!home.isEmpty()) {
        commonDirs << (home + QStringLiteral("/.local/bin"))
                   << (home + QStringLiteral("/.cargo/bin"));
    }
    for (const QString& dir : commonDirs) {
        for (const QString& name : candidateNames) {
            QString candidate = dir + QStringLiteral("/") + name;
            if (QFileInfo(candidate).isExecutable()) {
                LOG_DEBUG("[LspManager] 常见路径命中: " << candidate.toStdString());
                return candidate;
            }
        }
    }
#endif

    LOG_DEBUG("[LspManager] 自动检测未找到 " << langId.toStdString() << " 语言服务器");
    return QString();
}

QStringList LspManager::serverArgs(const QString& langId, const QString& projectRoot) const
{
    // 各语言服务器的默认启动参数
    if (langId == "cpp" || langId == "c") {
        QStringList args;
        args << QStringLiteral("--background-index");
        // P0-1: --query-driver 精确指定 MinGW 编译器路径，消除 "driver clang not found in PATH" 警告
        //       原通配符 ** 不稳定，改为检测实际编译器路径
        // L2: detectCompilerDriver 结果缓存在 m_cachedDriverPath，避免重复读取 compile_commands.json
        QString driverPath = detectCompilerDriver(projectRoot);
        if (!driverPath.isEmpty()) {
            args << QStringLiteral("--query-driver=") + driverPath;
            LOG_INFO("[LspManager] clangd query-driver: " << driverPath.toStdString());
        } else {
            // 兜底：通配符（有警告但功能可用）
            args << QStringLiteral("--query-driver=**");
            LOG_WARN("[LspManager] 未检测到编译器路径，使用通配符 query-driver");
        }
        // P0-1: 移除 --pch-storage=memory（导致 15MB+ PCH 驻留内存，内存占用持续走高）
        //       默认磁盘存储，PCH 缓存在 .cache/clangd/ 下，内存占用可控
        // P4: --log=info 降低 clangd 自身日志冗余（默认 verbose 会刷屏 stderr）
        args << QStringLiteral("--log=info");
        // P0-1: 限制补全/悬停结果数量，避免超大响应阻塞主线程
        args << QStringLiteral("--limit-results=20");
        // P0-1: 禁用自动头文件插入（未配置项目下会误插入，干扰编码）
        args << QStringLiteral("--header-insertion=never");
        // P0-3: 如果存在 compile_commands.json，传递编译数据库目录
        // L1: 使用 projectRoot（来自 inferProjectRoot）而非 m_workspaceRoot
        // 修复根因：用户未通过"打开文件夹"设置 m_workspaceRoot 时，仍能找到 compile_commands.json
        if (!projectRoot.isEmpty()) {
            const QString rootCc = projectRoot + QStringLiteral("/compile_commands.json");
            const QString buildCc = projectRoot + QStringLiteral("/build/compile_commands.json");
            if (QFileInfo::exists(rootCc)) {
                args << QStringLiteral("--compile-commands-dir=") + projectRoot;
                LOG_INFO("[LspManager] 检测到 compile_commands.json (root): "
                          << projectRoot.toStdString());
            } else if (QFileInfo::exists(buildCc)) {
                args << QStringLiteral("--compile-commands-dir=") + projectRoot + QStringLiteral("/build");
                LOG_INFO("[LspManager] 检测到 compile_commands.json (build/): "
                          << (projectRoot + "/build").toStdString());
            } else {
                LOG_INFO("[LspManager] 未找到 compile_commands.json，clangd 将使用降级 fallback 解析");
            }
        }
        return args;
    }
    if (langId == "python")
        return QStringList{};
    if (langId == "javascript" || langId == "typescript")
        return QStringList{ QStringLiteral("--stdio") };
    return QStringList{};
}

/// L2: 检测 MinGW 编译器路径，用于 clangd --query-driver
/// 优先级：compile_commands.json 中的编译器 > Qt Tools MinGW > 系统 PATH
/// L1: projectRoot 用于查找 compile_commands.json（不依赖 m_workspaceRoot）
/// L2: 结果缓存在 m_cachedDriverPath，避免每次 serverArgs 都重新读取 compile_commands.json
QString LspManager::detectCompilerDriver(const QString& projectRoot) const
{
    // L2: 缓存命中 — 避免每次启动都重新读取 compile_commands.json 和遍历候选路径
    if (!m_cachedDriverPath.isEmpty()) {
        return m_cachedDriverPath;
    }

    // 1. 从 compile_commands.json 提取编译器路径（最准确）
    // L1: 使用 projectRoot 而非 m_workspaceRoot
    if (!projectRoot.isEmpty()) {
        const QString buildCc = projectRoot + QStringLiteral("/build/compile_commands.json");
        const QString rootCc = projectRoot + QStringLiteral("/compile_commands.json");
        for (const QString& ccPath : {buildCc, rootCc}) {
            if (QFileInfo::exists(ccPath)) {
                QFile f(ccPath);
                if (f.open(QIODevice::ReadOnly)) {
                    QJsonParseError err;
                    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
                    if (err.error == QJsonParseError::NoError && doc.isArray()) {
                        QJsonArray arr = doc.array();
                        if (!arr.isEmpty()) {
                            QString cmd = arr.at(0).toObject().value("command").toString();
                            // command 格式: "F:/path/to/g++.exe  -std=c++17 ..."
                            int spaceIdx = cmd.indexOf(' ');
                            QString compiler = (spaceIdx > 0) ? cmd.left(spaceIdx) : cmd;
                            compiler = compiler.replace('\\', '/').trimmed();
                            // 去除可能的引号
                            if (compiler.startsWith('"') && compiler.endsWith('"'))
                                compiler = compiler.mid(1, compiler.length() - 2);
                            if (QFileInfo::exists(compiler)) {
                                m_cachedDriverPath = compiler;  // L2: 缓存结果
                                return compiler;
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. 检测 Qt Tools 下的 MinGW（常见安装路径）
    static const QStringList kMingwCandidates = {
        QStringLiteral("F:/IDE.2/QT/Tools/mingw1120_64/bin/g++.exe"),
        QStringLiteral("F:/IDE.2/QT/Tools/mingw900_64/bin/g++.exe"),
        QStringLiteral("F:/IDE.2/QT/Tools/mingw810_64/bin/g++.exe"),
        QStringLiteral("C:/Qt/Tools/mingw1120_64/bin/g++.exe"),
        QStringLiteral("C:/Qt/Tools/mingw900_64/bin/g++.exe"),
    };
    for (const QString& path : kMingwCandidates) {
        if (QFileInfo::exists(path)) {
            m_cachedDriverPath = path;  // L2: 缓存结果
            return path;
        }
    }

    // 3. 从 PATH 查找 g++
    QString pathGpp = QStandardPaths::findExecutable(QStringLiteral("g++"));
    if (!pathGpp.isEmpty()) {
        m_cachedDriverPath = pathGpp;  // L2: 缓存结果
        return pathGpp;
    }

    return QString();
}

QString LspManager::filePathToUri(const QString& filePath)
{
    return QUrl::fromLocalFile(filePath).toString();
}

QString LspManager::uriToFilePath(const QString& uri)
{
    return QUrl(uri).toLocalFile();
}

void LspManager::setWorkspaceRoot(const QString& rootPath)
{
    m_workspaceRoot = rootPath;
    LOG_INFO("[LspManager] 工作区根目录已设置: " << rootPath.toStdString());
}

// ============================================================
// P3-M01 子项3: 远程 LSP 模式
// ============================================================

void LspManager::setRemoteLspMode(const QString& sessionName, const QString& remoteClangdPath,
                                  ISshClient* sshClient)
{
    m_remoteSessionName = sessionName;
    m_remoteClangdPath = remoteClangdPath;
    m_remoteSshClient = sshClient;

    // 远程模式下清空本地 clangd 检测缓存，强制下次 serverCommand 走远程路径
    m_detectedCache.remove(QStringLiteral("cpp"));
    m_detectedCache.remove(QStringLiteral("c"));

    LOG_INFO("[LspManager] 远程 LSP 模式已启用: session=" << sessionName.toStdString()
             << " clangd=" << remoteClangdPath.toStdString()
             << " sshClient=" << (sshClient ? "yes" : "no"));
}

void LspManager::clearRemoteLspMode()
{
    LOG_INFO("[LspManager] 远程 LSP 模式已退出");
    m_remoteSessionName.clear();
    m_remoteClangdPath.clear();
    m_remoteSshClient = nullptr;

    // 清空本地检测缓存，使后续 openFile 重新走本地路径检测
    m_detectedCache.remove(QStringLiteral("cpp"));
    m_detectedCache.remove(QStringLiteral("c"));
}

QString LspManager::inferProjectRoot(const QString& filePath, const QString& workspaceRoot)
{
    // 1. 优先使用 Widget 层设置的工作区根目录
    if (!workspaceRoot.isEmpty() && QDir(workspaceRoot).exists()) {
        return QDir(workspaceRoot).absolutePath();
    }

    // 2. 从文件路径向上查找项目根标志文件
    // 标志文件优先级：compile_commands.json > CMakeLists.txt > .clangd > .git
    static const QStringList markers = {
        QStringLiteral("compile_commands.json"),
        QStringLiteral("CMakeLists.txt"),
        QStringLiteral(".clangd"),
        QStringLiteral(".git")
    };

    QDir dir = QFileInfo(filePath).absoluteDir();
    // 限制向上查找层数，避免一直查到根目录
    for (int i = 0; i < 10 && dir.exists(); ++i) {
        for (const QString& marker : markers) {
            if (QFileInfo::exists(dir.absoluteFilePath(marker))) {
                LOG_DEBUG("[LspManager] 推断项目根目录: " << dir.absolutePath().toStdString()
                          << " (标志: " << marker.toStdString() << ")");
                return dir.absolutePath();
            }
        }
        if (!dir.cdUp()) break;
    }

    // 3. 查找失败，返回空（调用方使用文件所在目录作为 fallback）
    return QString();
}

// ============================================================
// 私有槽 — LspClient 响应路由
// ============================================================

void LspManager::onCompletionsReady(const QList<LspCompletionItem>& items)
{
    // P0 C01: 使用独立的补全文件路径，不再共享 m_currentRequestFile
    emit completionsReady(m_currentCompletionFile, items);
}

void LspManager::onDefinitionReady(const QString& uri, int line, int col)
{
    // 跳转定义响应 → 目标 URI 可能是另一个文件
    // P0 C01: 使用独立的定义文件路径
    QString filePath = m_uriToFilePath.value(uri, uriToFilePath(uri));
    emit definitionReady(m_currentDefinitionFile, uri, line, col);
}

void LspManager::onHoverReady(const QString& documentation, const QPoint& pos)
{
    // F4: 使用独立的 hover 文件路径，不与 completion 等请求共享
    emit hoverReady(m_currentHoverFile, documentation);
}

void LspManager::onDiagnosticsReady(const QString& uri, const QList<LspDiagnostic>& diagnostics)
{
    // 诊断是服务器主动推送 → 通过 URI 反查文件路径
    QString filePath = m_uriToFilePath.value(uri);
    if (filePath.isEmpty()) {
        filePath = uriToFilePath(uri);
    }
    emit diagnosticsReady(filePath, diagnostics);
}

void LspManager::onSymbolsReady(const QString& uri, const QList<QVariantMap>& symbols)
{
    // V2.1 C1 修复：按 requestId 精确路由的 uri 反查文件路径，
    // 不再依赖 m_currentRequestFile（会被 completion/definition 等请求覆盖）
    QString filePath = m_uriToFilePath.value(uri);
    if (filePath.isEmpty()) {
        filePath = uriToFilePath(uri);
    }
    emit symbolsReady(filePath, symbols);
}

void LspManager::onReferencesReady(const QList<QVariantMap>& references)
{
    // P0 C01: 使用独立的引用文件路径
    emit referencesReady(m_currentReferencesFile, references);
}

void LspManager::onServerError(const QString& error)
{
    // P0 C01: 服务器错误无法确定具体文件，使用最近的请求文件兜底
    emit serverError(m_currentCompletionFile.isEmpty() ? m_currentDefinitionFile : m_currentCompletionFile, error);
}

void LspManager::onServerStopped(const QString& langId)
{
    // P0-2: clangd 崩溃/异常退出时自动重连
    if (m_shuttingDown) {
        LOG_INFO("[LspManager] 正在关闭，不自动重连 (langId=" << langId.toStdString() << ")");
        return;
    }

    // P1-3: 服务器停止 → 状态变为 Disconnected（高亮器启用启发式兜底）
    emit lspStateChanged(langId, LspHighlightState::Disconnected);

    // 指数退避：第1次 2s，第2次 4s，第3次 8s，最多 30s
    int& restartCount = m_restartCount[langId];
    int delayMs = qMin(2000 * (1 << restartCount), 30000);
    restartCount++;

    LOG_WARN("[LspManager] LSP 服务器异常停止 (langId=" << langId.toStdString()
             << ")，" << delayMs << "ms 后自动重连（第 " << restartCount << " 次）");

    // 延迟重启
    QTimer::singleShot(delayMs, this, [this, langId]() {
        restartServer(langId);
    });
}

void LspManager::restartServer(const QString& langId)
{
    LOG_INFO("[LspManager] 正在重启 LSP 服务器 (langId=" << langId.toStdString() << ")");

    // 移除旧客户端（已停止）
    auto it = m_clients.find(langId);
    if (it != m_clients.end()) {
        delete it.value();
        m_clients.erase(it);
    }

    // 重新收集该语言的所有打开文件
    QStringList filesToReopen;
    for (auto fit = m_fileToLangId.begin(); fit != m_fileToLangId.end(); ++fit) {
        if (fit.value() == langId) {
            filesToReopen.append(fit.key());
        }
    }

    // 清理 URI 映射中属于该语言的条目
    for (auto uit = m_uriToFilePath.begin(); uit != m_uriToFilePath.end(); ) {
        if (m_fileToLangId.value(uit.value()) == langId) {
            uit = m_uriToFilePath.erase(uit);
        } else {
            ++uit;
        }
    }

    // 重新打开所有文件（触发 getOrCreateClient → startServer → didOpen）
    for (const QString& filePath : filesToReopen) {
        m_fileToLangId.remove(filePath);  // 先移除，openFile 会重新添加
        // 从磁盘读取内容（若编辑器有未保存内容，Widget 层会通过 documentChanged 同步）
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(f.readAll());
            openFile(filePath, content);
        }
    }

    LOG_INFO("[LspManager] LSP 服务器重启完成，重新打开 " << filesToReopen.size()
             << " 个文件 (langId=" << langId.toStdString() << ")");
}

void LspManager::onClientInitialized()
{
    // sender() 是发射 initialized() 信号的 LspClient
    ILspClient* client = qobject_cast<ILspClient*>(sender());
    if (!client) return;

    // 反查 langId
    QString langId;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value() == client) {
            langId = it.key();
            break;
        }
    }
    if (langId.isEmpty()) return;

    // P1-3: 握手完成 → 状态变为 Ready（高亮器禁用启发式兜底，使用语义高亮）
    // 重启计数清零
    m_restartCount.remove(langId);
    emit lspStateChanged(langId, LspHighlightState::Ready);

    // flush 该语言缓存的 didOpen（握手前缓存的，现在握手完成可安全发送）
    auto pendIt = m_pendingOpens.find(langId);
    if (pendIt == m_pendingOpens.end() || pendIt.value().isEmpty()) return;

    LOG_INFO("[LspManager] 握手完成，flush " << pendIt.value().size()
             << " 个缓存的 didOpen (langId=" << langId.toStdString() << ")");
    for (const auto& po : pendIt.value()) {
        client->openDocument(po.uri, po.text, po.langId);
    }
    pendIt.value().clear();
}
