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

bool FileController::writeFile(const QString& filePath,
                               const QString& content,
                               const QString& encoding)
{
    QFile file(filePath);
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
            file.write(codec->fromUnicode(content));
            file.flush();
            file.close();
            return true;
        }
    }

    // UTF-16 走 QTextCodec
    if (encoding.contains("UTF-16", Qt::CaseInsensitive)) {
        QTextCodec* codec = QTextCodec::codecForName(encoding.toUtf8());
        if (codec) {
            file.write(codec->fromUnicode(content));
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
    out << content;
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
