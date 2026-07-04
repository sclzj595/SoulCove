#include "controller/FileController.h"
#include "core/fileio/EncodingDetector.h"
#include "Logger.hpp"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QStringConverter>
#include <QTextCodec>

// ========== 文件读写 ==========

QString FileController::readFile(const QString& filePath,
                                 QString* detectedEncoding)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_DEBUG("[FileController] readFile 打开失败:" << filePath
                  << "err:" << file.errorString());
        return QString();
    }
    QByteArray rawData = file.readAll();
    file.close();

    // 自动编码检测
    EncodingDetectionResult result = EncodingDetector::detect(rawData);
    QString content;
    QString effectiveEncoding = QStringLiteral("UTF-8");

    if (result.isValid && result.codec) {
        effectiveEncoding = result.encodingName;
        content = result.codec->toUnicode(rawData);
    } else if (effectiveEncoding.compare("GBK", Qt::CaseInsensitive) == 0 ||
               effectiveEncoding.compare("GB18030", Qt::CaseInsensitive) == 0) {
        QTextCodec* codec = QTextCodec::codecForName(effectiveEncoding.toUtf8());
        content = codec ? codec->toUnicode(rawData) : QString::fromUtf8(rawData);
    } else {
        auto decoder = QStringDecoder(QStringConverter::Utf8);
        content = decoder.isValid() ? decoder(rawData) : QString::fromUtf8(rawData);
    }

    if (detectedEncoding) *detectedEncoding = effectiveEncoding;
    return content;
}

// P3-M03 子项1: 从原始字节流检测行尾类型
// 统计前 16KB 中 \r\n / \r / \n 出现次数，取最多的作为结果
// 兼容二进制：扫描字符前需确保不是 NULL（0x00），避免二进制文件误判
QString FileController::detectEol(const QByteArray& rawData)
{
    int n = qMin(rawData.size(), 16 * 1024);
    int crlf = 0, lf = 0, cr = 0;
    for (int i = 0; i < n; ++i) {
        char c = rawData[i];
        if (c == '\r') {
            if (i + 1 < n && rawData[i + 1] == '\n') {
                ++crlf;
                ++i;  // 跳过 \n
            } else {
                ++cr;
            }
        } else if (c == '\n') {
            ++lf;
        }
    }
    // 优先级：CRLF > LF > CR（数量相等时取跨平台兼容性更好的）
    if (crlf >= lf && crlf >= cr && crlf > 0) return QStringLiteral("CRLF");
    if (lf >= cr && lf > 0) return QStringLiteral("LF");
    if (cr > 0) return QStringLiteral("CR");
    return QStringLiteral("LF");  // 默认
}

// P3-M03 子项1: 读取文件并检测行尾类型
QString FileController::readFileWithEol(const QString& filePath,
                                        QString* detectedEol)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (detectedEol) *detectedEol = QStringLiteral("LF");
        return QString();
    }
    QByteArray rawData = file.readAll();
    file.close();

    if (detectedEol) *detectedEol = detectEol(rawData);

    // 自动编码检测
    EncodingDetectionResult result = EncodingDetector::detect(rawData);
    QString content;
    QString effectiveEncoding = QStringLiteral("UTF-8");

    if (result.isValid && result.codec) {
        effectiveEncoding = result.encodingName;
        content = result.codec->toUnicode(rawData);
    } else {
        auto decoder = QStringDecoder(QStringConverter::Utf8);
        content = decoder.isValid() ? decoder(rawData) : QString::fromUtf8(rawData);
    }

    return content;
}

bool FileController::writeFile(const QString& filePath,
                               const QString& content,
                               const QString& encoding,
                               const QString& eol)
{
    // P3-M03 子项1: 按指定 EOL 类型统一行尾
    // Qt 文档内部使用单个 '\n'（U+000A）作为段落分隔符，转换为指定 EOL 序列
    QString normalized = content;
    if (eol.compare(QStringLiteral("CRLF"), Qt::CaseInsensitive) == 0) {
        normalized = QString(content).replace(QChar('\n'), QStringLiteral("\r\n"));
    } else if (eol.compare(QStringLiteral("CR"), Qt::CaseInsensitive) == 0) {
        normalized = QString(content).replace(QChar('\n'), QChar('\r'));
    }
    // LF 或空字符串：保持 \n（无需转换）

    QFile file(filePath);
    // 不使用 QIODevice::Text（避免 Qt 自动行尾转换覆盖 EOL 设置）
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_DEBUG("[FileController] writeFile 打开失败:" << filePath
                  << "err:" << file.errorString());
        return false;
    }

    // GBK/GB18030 走 QTextCodec
    if (encoding.compare("GBK", Qt::CaseInsensitive) == 0 ||
        encoding.compare("GB18030", Qt::CaseInsensitive) == 0) {
        QTextCodec* codec = QTextCodec::codecForName(encoding.toUtf8());
        if (codec) {
            file.write(codec->fromUnicode(normalized));
            file.flush();
            file.close();
            return true;
        }
    }

    // UTF-16 走 QTextCodec
    if (encoding.contains("UTF-16", Qt::CaseInsensitive)) {
        QTextCodec* codec = QTextCodec::codecForName(encoding.toUtf8());
        if (codec) {
            file.write(codec->fromUnicode(normalized));
            file.flush();
            file.close();
            return true;
        }
    }

    // 默认：QStringConverter（UTF-8 / ASCII 等）
    auto opt = QStringConverter::encodingForName(encoding.toUtf8());
    QStringConverter::Encoding enc = opt.value_or(QStringConverter::Utf8);

    QTextStream out(&file);
    out.setEncoding(enc);
    out << normalized;
    out.flush();
    file.close();
    return true;
}

bool FileController::createFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_DEBUG("[FileController] createFile 失败:" << filePath
                  << "err:" << file.errorString());
        return false;
    }
    file.close();
    return true;
}

// ========== 文件系统操作 ==========

bool FileController::deleteFile(const QString& filePath)
{
    if (!QFile::remove(filePath)) {
        LOG_DEBUG("[FileController] deleteFile 失败:" << filePath);
        return false;
    }
    return true;
}

bool FileController::renameFile(const QString& filePath, const QString& newPath)
{
    if (!QFile::rename(filePath, newPath)) {
        LOG_DEBUG("[FileController] renameFile 失败:" << filePath << "->" << newPath);
        return false;
    }
    return true;
}

bool FileController::moveFile(const QString& sourcePath,
                              const QString& targetPath,
                              bool overwrite)
{
    // 同路径无需移动
    if (QFileInfo(sourcePath).absoluteFilePath() ==
        QFileInfo(targetPath).absoluteFilePath()) {
        return true;
    }

    // 目标已存在
    if (QFileInfo::exists(targetPath)) {
        if (!overwrite) {
            LOG_DEBUG("[FileController] moveFile 目标已存在且未授权覆盖:" << targetPath);
            return false;
        }
        QFile::remove(targetPath);
    }

    // 优先 rename（同盘符快速）
    if (QFile::rename(sourcePath, targetPath)) return true;

    // 跨盘符 fallback：copy + remove
    if (QFile::copy(sourcePath, targetPath)) {
        QFile::remove(sourcePath);
        return true;
    }

    LOG_DEBUG("[FileController] moveFile 失败:" << sourcePath << "->" << targetPath);
    return false;
}

bool FileController::exists(const QString& filePath)
{
    return QFile::exists(filePath);
}

// ========== 路径工具 ==========

QString FileController::fileName(const QString& filePath)
{
    return QFileInfo(filePath).fileName();
}

QString FileController::absolutePath(const QString& filePath)
{
    return QFileInfo(filePath).absolutePath();
}

QString FileController::absoluteFilePath(const QString& filePath)
{
    return QFileInfo(filePath).absoluteFilePath();
}
