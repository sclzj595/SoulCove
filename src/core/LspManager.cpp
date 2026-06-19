#include "LspManager.h"
#include "LspClient.h"
#include "ConfigManager.h"
#include "Logger.hpp"

#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QProcessEnvironment>

// ============================================================
// 构造 / 析构
// ============================================================

LspManager::LspManager(QObject* parent)
    : QObject(parent)
{
}

LspManager::~LspManager()
{
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
        QStringList args = serverArgs(langId);
        // 修复 P0-2: 使用工作区根目录而非文件所在目录作为 clangd 的工作目录
        // 否则 clangd 会把文件所在目录（如 src/）当作项目根目录，找不到 CMakeLists.txt
        // 和头文件路径，导致全屏爆红
        QString workingDir = inferProjectRoot(filePath, m_workspaceRoot);
        if (workingDir.isEmpty()) {
            // 最终 fallback: 文件所在目录
            workingDir = QFileInfo(filePath).absolutePath();
            LOG_WARN("[LspManager] 无法推断项目根目录，使用文件所在目录作为 fallback: "
                      << workingDir.toStdString());
        }

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

    // 发送 didClose（LSP 协议要求）
    ILspClient* client = m_clients.value(it.value());
    if (client && client->isInitialized()) {
        // LspClient 暂无 didClose 方法，通过 changeDocument 空内容近似处理
        // 后续可在 ILspClient 添加 closeDocument 纯虚方法
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

    m_currentRequestFile = filePath;  // 记录当前请求文件（响应路由用）
    client->requestCompletion(filePathToUri(filePath), line, col);
}

void LspManager::requestDefinition(const QString& filePath, int line, int col)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;

    ILspClient* client = m_clients.value(it.value());
    if (!client) {
        LOG_WARN("[LspManager] requestDefinition: 无对应客户端 (" << filePath.toStdString() << ")");
        return;
    }
    if (!client->isInitialized()) {
        LOG_WARN("[LspManager] requestDefinition: 服务器未完成握手，请求被丢弃 (" << filePath.toStdString() << ")");
        return;
    }

    m_currentRequestFile = filePath;
    client->requestDefinition(filePathToUri(filePath), line, col);
}

void LspManager::requestHover(const QString& filePath, int line, int col)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;

    ILspClient* client = m_clients.value(it.value());
    if (!client) {
        LOG_WARN("[LspManager] requestHover: 无对应客户端 (" << filePath.toStdString() << ")");
        return;
    }
    if (!client->isInitialized()) {
        LOG_WARN("[LspManager] requestHover: 服务器未完成握手，请求被丢弃 (" << filePath.toStdString() << ")");
        return;
    }

    m_currentRequestFile = filePath;
    client->requestHover(filePathToUri(filePath), line, col);
}

void LspManager::requestSymbols(const QString& filePath)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;

    ILspClient* client = m_clients.value(it.value());
    if (!client || !client->isInitialized()) return;

    m_currentRequestFile = filePath;
    client->requestSymbols(filePathToUri(filePath));
}

void LspManager::requestReferences(const QString& filePath, int line, int col)
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return;

    ILspClient* client = m_clients.value(it.value());
    if (!client) {
        LOG_WARN("[LspManager] requestReferences: 无对应客户端 (" << filePath.toStdString() << ")");
        return;
    }
    if (!client->isInitialized()) {
        LOG_WARN("[LspManager] requestReferences: 服务器未完成握手，请求被丢弃 (" << filePath.toStdString() << ")");
        return;
    }

    m_currentRequestFile = filePath;
    client->requestReferences(filePathToUri(filePath), line, col);
}

// ============================================================
// 状态查询
// ============================================================

bool LspManager::hasServerForFile(const QString& filePath) const
{
    auto it = m_fileToLangId.find(filePath);
    if (it == m_fileToLangId.end()) return false;

    ILspClient* client = m_clients.value(it.value());
    return client && client->isRunning();
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
    // 握手完成 → flush 缓存的 didOpen（修复 LSP 时序）
    connect(client, &ILspClient::initialized,
            this, &LspManager::onClientInitialized);

    m_clients[langId] = client;
    return client;
}

QString LspManager::langIdForSuffix(const QString& suffix)
{
    // 文件后缀 → 内部语言ID
    if (suffix == "cpp" || suffix == "c" || suffix == "h" ||
        suffix == "hpp" || suffix == "cc" || suffix == "cxx" ||
        suffix == "hxx" || suffix == "inl")
        return QStringLiteral("cpp");
    if (suffix == "py" || suffix == "pyw")
        return QStringLiteral("python");
    if (suffix == "js" || suffix == "jsx" || suffix == "mjs")
        return QStringLiteral("javascript");
    if (suffix == "ts" || suffix == "tsx")
        return QStringLiteral("typescript");
    if (suffix == "go")
        return QStringLiteral("go");
    if (suffix == "java")
        return QStringLiteral("java");
    if (suffix == "rs")
        return QStringLiteral("rust");
    return QString();  // 不支持的语言
}

QString LspManager::lspLangId(const QString& langId)
{
    // 内部语言ID → LSP 协议 languageId
    return langId;  // LSP languageId 与内部 langId 一致
}

QString LspManager::serverCommand(const QString& langId) const
{
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

QStringList LspManager::serverArgs(const QString& langId) const
{
    // 各语言服务器的默认启动参数
    if (langId == "cpp" || langId == "c") {
        QStringList args;
        args << QStringLiteral("--background-index");
        // P1-1: 添加 --query-driver 允许 clangd 查询编译器系统头文件路径
        // Windows + MinGW/Qt 工具链下，clangd 默认不知道系统头文件位置，
        // 需要通过 query-driver 授权 clangd 调用编译器获取系统头文件路径
        args << QStringLiteral("--query-driver=**");
        // P1-2: 移除默认的 --clang-tidy（在未配置项目中会产生大量额外噪音诊断，
        // 加剧"爆红"。用户可在项目根目录的 .clangd 配置文件中按需启用 clang-tidy）
        // P0-3: 如果存在 compile_commands.json，传递编译数据库目录
        if (!m_workspaceRoot.isEmpty()) {
            if (QFileInfo::exists(m_workspaceRoot + QStringLiteral("/compile_commands.json"))) {
                args << QStringLiteral("--compile-commands-dir=") + m_workspaceRoot;
                LOG_INFO("[LspManager] 检测到 compile_commands.json，目录: "
                          << m_workspaceRoot.toStdString());
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
    // 补全响应 → 使用 m_currentRequestFile 路由
    emit completionsReady(m_currentRequestFile, items);
}

void LspManager::onDefinitionReady(const QString& uri, int line, int col)
{
    // 跳转定义响应 → 目标 URI 可能是另一个文件
    QString filePath = m_uriToFilePath.value(uri, uriToFilePath(uri));
    emit definitionReady(m_currentRequestFile, uri, line, col);
}

void LspManager::onHoverReady(const QString& documentation, const QPoint& pos)
{
    emit hoverReady(m_currentRequestFile, documentation);
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

void LspManager::onSymbolsReady(const QList<QVariantMap>& symbols)
{
    emit symbolsReady(m_currentRequestFile, symbols);
}

void LspManager::onReferencesReady(const QList<QVariantMap>& references)
{
    emit referencesReady(m_currentRequestFile, references);
}

void LspManager::onServerError(const QString& error)
{
    emit serverError(m_currentRequestFile, error);
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
