#include "core/markdown/MermaidRenderer.h"
#include "core/config/ThemeManager.h"
#include "Logger.hpp"

#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QTemporaryFile>

// === 静态成员初始化 ===
bool          MermaidRenderer::s_availableChecked = false;
bool          MermaidRenderer::s_available        = false;
QHash<QString, QByteArray> MermaidRenderer::s_cache;
QRecursiveMutex MermaidRenderer::s_mutex;
QString       MermaidRenderer::s_mmdcPath;
int           MermaidRenderer::s_cacheHits        = 0;

// 缓存上限（避免长时间运行后内存膨胀）
static constexpr int kMaxCacheEntries = 100;

// ============================================================
// 公共 API
// ============================================================

bool MermaidRenderer::isAvailable()
{
    QMutexLocker locker(&s_mutex);

    if (s_availableChecked) {
        return s_available;
    }

    // 1. 查找 mmdc 可执行文件
    s_mmdcPath = findMmdcExecutable();
    if (s_mmdcPath.isEmpty()) {
        LOG_DEBUG_S("MermaidRenderer", "isAvailable",
                    "未找到 mmdc 可执行文件（请安装 @mermaid-js/mermaid-cli 或设置 MERMAID_CLI_PATH 环境变量）");
        s_available = false;
        s_availableChecked = true;
        return false;
    }

    // 2. 验证 mmdc --version 能成功执行（区分"路径存在"与"实际可运行"）
    QProcess process;
    process.setProgram(s_mmdcPath);
    process.setArguments({QStringLiteral("--version")});
    process.start();
    if (!process.waitForStarted(3000)) {
        LOG_DEBUG_S("MermaidRenderer", "isAvailable",
                    "mmdc 进程启动失败: " << s_mmdcPath.toStdString());
        s_available = false;
        s_availableChecked = true;
        return false;
    }
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(2000);
        LOG_DEBUG_S("MermaidRenderer", "isAvailable", "mmdc --version 超时");
        s_available = false;
        s_availableChecked = true;
        return false;
    }

    s_available = (process.exitCode() == 0);
    s_availableChecked = true;

    if (s_available) {
        QString ver = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        LOG_DEBUG_S("MermaidRenderer", "isAvailable",
                    "mmdc 可用: " << s_mmdcPath.toStdString()
                    << " 版本: " << ver.toStdString());
    } else {
        QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        LOG_DEBUG_S("MermaidRenderer", "isAvailable",
                    "mmdc --version 失败 exitCode=" << process.exitCode()
                    << " stderr: " << err.toStdString());
    }
    return s_available;
}

QByteArray MermaidRenderer::renderToSvg(const QString& mermaidCode)
{
    if (mermaidCode.trimmed().isEmpty()) {
        return QByteArray();
    }

    QMutexLocker locker(&s_mutex);

    // 1. 检查缓存命中
    QString key = cacheKey(mermaidCode);
    auto it = s_cache.constFind(key);
    if (it != s_cache.constEnd()) {
        ++s_cacheHits;
        return it.value();
    }

    // 2. 缓存未命中 → 调用 mmdc 渲染
    // 注意：renderViaMmdc 内部不再获取锁（已由调用方持有）
    QByteArray svg = renderViaMmdc(mermaidCode);

    if (!svg.isEmpty()) {
        // 写入缓存
        // FIFO 淘汰：超过上限时移除最早插入的条目
        if (s_cache.size() >= kMaxCacheEntries && !s_cache.isEmpty()) {
            s_cache.erase(s_cache.begin());
        }
        s_cache.insert(key, svg);
    }
    return svg;
}

void MermaidRenderer::clearCache()
{
    QMutexLocker locker(&s_mutex);
    s_cache.clear();
    LOG_DEBUG_S("MermaidRenderer", "clearCache",
                "已清空缓存（命中次数: " << s_cacheHits << "）");
    s_cacheHits = 0;
}

// ============================================================
// 私有辅助
// ============================================================

QString MermaidRenderer::cacheKey(const QString& mermaidCode)
{
    // SHA-1 哈希 hex（40 字符）
    // 包含主题信息，确保主题切换后缓存键不同（避免暗色 SVG 在浅色主题下显示）
    QString theme = currentMmdcTheme();
    QByteArray data = (mermaidCode + QStringLiteral("||") + theme).toUtf8();
    return QString::fromUtf8(
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString MermaidRenderer::currentMmdcTheme()
{
    // 与 MarkdownMode/MaddyParser 一致：bgEditor.lightness() > 128 → 浅色
    const auto& p = ThemeManager::instance().currentPalette();
    bool isLight = p.bgEditor.lightness() > 128;
    // mmdc 主题参数：dark / default / forest
    // dark: 深色背景 + 浅色文字（适配暗色主题预览）
    // default: 浅色背景 + 深色文字（适配浅色主题预览）
    return isLight ? QStringLiteral("default") : QStringLiteral("dark");
}

QString MermaidRenderer::writeTempInput(const QString& content)
{
    // 使用 QTemporaryFile 自动管理生命周期（避免残留）
    // 模板：mermaid_XXXXXX.mmd
    QString tpl = QDir::tempPath() + QStringLiteral("/mermaid_XXXXXX.mmd");
    QTemporaryFile* tmp = new QTemporaryFile(tpl);
    tmp->setAutoRemove(false);  // mmdc 需要读取，渲染后再删除
    if (!tmp->open()) {
        LOG_DEBUG_S("MermaidRenderer", "writeTempInput",
                    "无法创建临时输入文件: " << tpl.toStdString());
        delete tmp;
        return QString();
    }
    tmp->write(content.toUtf8());
    tmp->close();
    QString path = tmp->fileName();
    delete tmp;  // 关闭文件句柄后即可删除对象，文件保留在磁盘
    return path;
}

QString MermaidRenderer::tempOutputPath(const QString& cacheKey)
{
    // 输出路径：临时目录 + 缓存键前 16 字符 + .svg
    // 用缓存键避免多次渲染同名文件冲突
    return QDir::tempPath() + QStringLiteral("/mermaid_out_%1.svg")
           .arg(cacheKey.left(16));
}

QString MermaidRenderer::findMmdcExecutable()
{
    // 1. 环境变量 MERMAID_CLI_PATH（用户自定义路径，最高优先级）
    QString envPath = QString::fromUtf8(qgetenv("MERMAID_CLI_PATH"));
    if (!envPath.isEmpty() && QFileInfo(envPath).isExecutable()) {
        return envPath;
    }

    // 2. PATH 中的 mmdc（Windows 上 QProcess 会自动查找 .cmd/.bat 后缀）
    //    直接返回 "mmdc"，由 QProcess 解析 PATH
    //    通过 which/where 验证存在性
    QProcess which;
#ifdef Q_OS_WIN
    which.setProgram(QStringLiteral("where"));
#else
    which.setProgram(QStringLiteral("which"));
#endif
    which.setArguments({QStringLiteral("mmdc")});
    which.start();
    if (which.waitForStarted(2000) && which.waitForFinished(3000)) {
        if (which.exitCode() == 0) {
            QString out = QString::fromUtf8(which.readAllStandardOutput()).trimmed();
            // where 在 Windows 上可能返回多行（多个匹配），取第一个
            QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (!lines.isEmpty()) {
                QString first = lines.first().trimmed();
                if (!first.isEmpty() && QFileInfo(first).isExecutable()) {
                    return first;
                }
            }
        }
    }

    // 3. 兜底：直接返回 "mmdc"，让 QProcess 尝试 PATH 解析
    //    （某些环境下 which/where 可能不可用，但 QProcess 仍可执行）
    return QStringLiteral("mmdc");
}

QByteArray MermaidRenderer::renderViaMmdc(const QString& mermaidCode)
{
    // mmdc 调用约定：mmdc -i <input.mmd> -o <output.svg> -t <theme> -b transparent
    //   -i  输入文件
    //   -o  输出文件
    //   -t  主题 (dark/default/forest)
    //   -b  背景色 (transparent 透明，让 CSS 控制)

    // 1. 检查 mmdc 是否可用（首次调用触发检测）
    if (!isAvailable()) {
        return QByteArray();
    }

    // 2. 写入临时输入文件
    QString inputPath = writeTempInput(mermaidCode);
    if (inputPath.isEmpty()) {
        return QByteArray();
    }

    // 3. 准备输出路径
    QString key = cacheKey(mermaidCode);
    QString outputPath = tempOutputPath(key);
    // 清理上次输出（避免读到旧文件）
    if (QFile::exists(outputPath)) {
        QFile::remove(outputPath);
    }

    // 4. 调用 mmdc
    QProcess process;
    process.setProgram(s_mmdcPath);
    process.setArguments({
        QStringLiteral("-i"), inputPath,
        QStringLiteral("-o"), outputPath,
        QStringLiteral("-t"), currentMmdcTheme(),
        QStringLiteral("-b"), QStringLiteral("transparent")
    });

    process.start();
    if (!process.waitForStarted(5000)) {
        LOG_DEBUG_S("MermaidRenderer", "renderViaMmdc",
                    "mmdc 进程启动失败");
        QFile::remove(inputPath);
        return QByteArray();
    }

    // mmdc 首次运行可能较慢（Node.js 启动 + puppeteer 初始化），给 15 秒
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        LOG_DEBUG_S("MermaidRenderer", "renderViaMmdc",
                    "mmdc 渲染超时（15s）");
        QFile::remove(inputPath);
        return QByteArray();
    }

    // 清理输入文件
    QFile::remove(inputPath);

    if (process.exitCode() != 0) {
        QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        LOG_DEBUG_S("MermaidRenderer", "renderViaMmdc",
                    "mmdc 渲染失败 exitCode=" << process.exitCode()
                    << " stderr: " << err.toStdString());
        return QByteArray();
    }

    // 5. 读取输出 SVG
    QFile out(outputPath);
    if (!out.open(QIODevice::ReadOnly)) {
        LOG_DEBUG_S("MermaidRenderer", "renderViaMmdc",
                    "无法读取输出文件: " << outputPath.toStdString());
        return QByteArray();
    }
    QByteArray svg = out.readAll();
    out.close();
    QFile::remove(outputPath);  // 清理输出文件

    // 简单校验：SVG 应以 <?xml 或 <svg 开头
    if (svg.isEmpty() ||
        (!svg.startsWith("<?xml") && !svg.startsWith("<svg"))) {
        LOG_DEBUG_S("MermaidRenderer", "renderViaMmdc",
                    "输出文件不是有效的 SVG: " << svg.left(80).toStdString());
        return QByteArray();
    }

    return svg;
}
