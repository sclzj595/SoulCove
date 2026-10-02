#include "core/build/QtDetector.h"
#include "Logger.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>

// ============================================================
// QtInstallation
// ============================================================

QString QtInstallation::displayText() const
{
    // 列表展示格式：Qt 6.5.3 (mingw_64) @ C:/Qt/6.5.3/mingw_64
    if (prefixPath.isEmpty()) return QStringLiteral("(empty)");
    if (version.isEmpty() && compiler.isEmpty()) return prefixPath;
    return QStringLiteral("Qt %1 (%2) @ %3").arg(version, compiler, prefixPath);
}

// ============================================================
// QtDetector
// ============================================================

QList<QtInstallation> QtDetector::detectAll()
{
    QList<QtInstallation> results;

    // 1. 标准路径扫描：各盘符根目录下的 \Qt 目录（官方安装器默认布局）
    //    兼容 C:\Qt、D:\Qt、E:\Qt 等
    QStringList scanRoots;
    const auto drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        scanRoots << drive.absoluteFilePath() + QStringLiteral("/Qt");
    }

    // 2. 遍历每个 \Qt 根目录下的版本目录（如 6.5.3, 5.15.2）
    for (const QString& qtRoot : scanRoots) {
        QDir rootDir(qtRoot);
        if (!rootDir.exists()) continue;

        // 只取目录名形如 x.y.z 的子目录（过滤 Examples/Docs/Tools 等）
        const auto versionDirs = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& versionInfo : versionDirs) {
            const QString versionName = versionInfo.fileName();
            const QString version = parseVersion(versionName);
            if (version.isEmpty()) continue;  // 非版本号目录，跳过

            // 版本目录下的编译器目录（如 mingw_64, msvc2019_64）
            QDir versionDir(versionInfo.absoluteFilePath());
            const auto compilerDirs = versionDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& compilerInfo : compilerDirs) {
                const QString compilerName = compilerInfo.fileName();
                // 跳过明显无关目录（Examples/Docs/Tools 等）
                if (compilerName.startsWith(QStringLiteral("Examples")) ||
                    compilerName.startsWith(QStringLiteral("Docs")) ||
                    compilerName.startsWith(QStringLiteral("Tools"))) continue;

                QtInstallation inst = probeCompilerDir(versionInfo.absoluteFilePath(), compilerName);
                if (!inst.prefixPath.isEmpty()) {
                    // 去重：避免同一前缀被多次加入
                    bool dup = false;
                    for (const auto& existing : results) {
                        if (existing.prefixPath == inst.prefixPath) { dup = true; break; }
                    }
                    if (!dup) {
                        results.append(inst);
                    }
                }
            }
        }
    }

    // 3. 补充：从环境变量检测（Qt6_DIR / CMAKE_PREFIX_PATH）
    QtInstallation envInst = detectFromEnv();
    if (!envInst.prefixPath.isEmpty()) {
        bool dup = false;
        for (const auto& existing : results) {
            if (existing.prefixPath == envInst.prefixPath) { dup = true; break; }
        }
        if (!dup) {
            results.prepend(envInst);  // 环境变量配置优先展示
        }
    }

    LOG_INFO_S("QtDetector", "detectAll", "检测到 Qt 安装数量=" << results.size());
    return results;
}

QtInstallation QtDetector::detectFromEnv()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // 优先检查 Qt6_DIR 环境变量（通常指向 lib/cmake/Qt6）
    QString qt6Dir = env.value(QStringLiteral("Qt6_DIR"));
    if (!qt6Dir.isEmpty()) {
        // Qt6_DIR 形如 C:/Qt/6.5.3/mingw_64/lib/cmake/Qt6
        // 反推前缀：去掉末尾的 /lib/cmake/Qt6
        QDir dir(qt6Dir);
        // 向上回溯 4 级：Qt6 -> cmake -> lib -> <prefix>
        for (int i = 0; i < 4; ++i) {
            if (!dir.cdUp()) break;
        }
        QString prefix = dir.absolutePath();
        QtInstallation inst = validatePrefix(prefix);
        if (!inst.prefixPath.isEmpty()) {
            LOG_INFO_S("QtDetector", "detectFromEnv",
                       "从 Qt6_DIR 检测到 Qt: " << inst.prefixPath.toUtf8().constData());
            return inst;
        }
    }

    // 其次检查 CMAKE_PREFIX_PATH 环境变量（可能含多个路径，取第一个有效 Qt）
    QString prefixPath = env.value(QStringLiteral("CMAKE_PREFIX_PATH"));
    if (!prefixPath.isEmpty()) {
        // CMAKE_PREFIX_PATH 可能以分号分隔多个路径
        const auto parts = prefixPath.split(QRegularExpression(QStringLiteral("[;]")),
                                            Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            QtInstallation inst = validatePrefix(part.trimmed());
            if (!inst.prefixPath.isEmpty()) {
                LOG_INFO_S("QtDetector", "detectFromEnv",
                           "从 CMAKE_PREFIX_PATH 检测到 Qt: " << inst.prefixPath.toUtf8().constData());
                return inst;
            }
        }
    }

    return QtInstallation();  // 未检测到
}

QtInstallation QtDetector::validatePrefix(const QString& prefix)
{
    QtInstallation inst;  // 默认空
    if (prefix.isEmpty()) return inst;

    QString normalized = QDir(prefix).absolutePath();
    if (!QDir(normalized).exists()) return inst;

    // 验证 Qt6Config.cmake 或 Qt5Config.cmake 存在
    const QString qt6Config = normalized + QStringLiteral("/lib/cmake/Qt6/Qt6Config.cmake");
    const QString qt5Config = normalized + QStringLiteral("/lib/cmake/Qt5/Qt5Config.cmake");
    bool isQt6 = QFile::exists(qt6Config);
    bool isQt5 = QFile::exists(qt5Config);
    if (!isQt6 && !isQt5) return inst;

    inst.prefixPath = normalized;
    inst.cmakeDir = normalized + (isQt6 ? QStringLiteral("/lib/cmake/Qt6")
                                         : QStringLiteral("/lib/cmake/Qt5"));

    // 推断版本号与编译器：路径形如 .../6.5.3/mingw_64
    // 取最后两级目录名
    QDir dir(normalized);
    QString compiler = dir.dirName();           // 最后一级：mingw_64
    if (dir.cdUp()) {
        QString version = parseVersion(dir.dirName());  // 倒数第二级：6.5.3
        inst.version = version;
    }
    inst.compiler = compiler;

    return inst;
}

QtInstallation QtDetector::probeCompilerDir(const QString& versionDir, const QString& compilerName)
{
    QString prefix = QDir(versionDir).absoluteFilePath(compilerName);
    return validatePrefix(prefix);
}

QString QtDetector::parseVersion(const QString& versionDirName)
{
    // 版本号校验：必须以数字开头，形如 6.5.3 或 5.15.2
    if (versionDirName.isEmpty() || !versionDirName[0].isDigit()) return QString();

    // 严格匹配 x.y.z 格式（至少两段）
    static const QRegularExpression re(QStringLiteral("^\\d+(\\.\\d+)+$"));
    QRegularExpressionMatch match = re.match(versionDirName);
    if (match.hasMatch()) {
        return versionDirName;
    }
    return QString();
}
