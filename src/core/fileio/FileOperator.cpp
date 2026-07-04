#include "core/fileio/FileOperator.h"
#include "core/fileio/EncodingDetector.h"
#include "Logger.hpp"
#include <QDebug>
#include <QWidget>

FileOperator::FileOperator(QObject* parent)
    : Subject(parent), m_encoding(QStringLiteral("UTF-8"))
{
}

// ========== IFileOperator 实现 ==========

bool FileOperator::openFile(const QString& filePath)
{
    if (filePath.isEmpty()) return false;

    // 先关闭之前打开的文件
    if (m_file.isOpen()) {
        m_file.close();
    }

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        LOG_DEBUG_S("FileOperator", "openFile", "打开文件失败:" << filePath);
        return false;
    }

    m_currentFilePath = filePath;
    m_modified = false;

    // 读取原始字节数据
    QByteArray rawData = m_file.readAll();
    m_file.close();

    // T8: 自动编码检测 — 当编码为 "Auto" 或 "UTF-8"（默认）时自动检测
    QString effectiveEncoding = m_encoding;
    if (m_encoding.compare("Auto", Qt::CaseInsensitive) == 0 ||
        m_encoding.compare("UTF-8", Qt::CaseInsensitive) == 0) {
        EncodingDetectionResult result = EncodingDetector::detect(rawData);
        if (result.isValid && result.codec) {
            effectiveEncoding = result.encodingName;
            m_encoding = effectiveEncoding;  // 更新当前编码
            LOG_DEBUG_S("FileOperator", "openFile",
                        "自动检测编码:" << effectiveEncoding
                        << "置信度:" << result.confidence
                        << "BOM:" << result.hasBOM);
        }
    }

    // 使用检测到的编码解码
    QString content;
    if (effectiveEncoding.compare("GBK", Qt::CaseInsensitive) == 0 ||
        effectiveEncoding.compare("GB18030", Qt::CaseInsensitive) == 0) {
        // GBK/GB18030 需要 QTextCodec
        QTextCodec* codec = QTextCodec::codecForName(effectiveEncoding.toUtf8());
        if (codec) {
            content = codec->toUnicode(rawData);
        } else {
            content = QString::fromUtf8(rawData);
        }
    } else if (effectiveEncoding.contains("UTF-16", Qt::CaseInsensitive)) {
        // UTF-16 需要 QTextCodec
        QTextCodec* codec = QTextCodec::codecForName(effectiveEncoding.toUtf8());
        if (codec) {
            content = codec->toUnicode(rawData);
        } else {
            content = QString::fromUtf8(rawData);
        }
    } else {
        // UTF-8 / ASCII 等 — 使用 QStringConverter
        auto decoder = QStringDecoder(resolveEncoding(effectiveEncoding));
        if (decoder.isValid()) {
            content = decoder(rawData);
        } else {
            content = QString::fromUtf8(rawData);
        }
    }

    if (m_contentWriter) {
        m_contentWriter(content);
    }

    notifyObservers("fileOpened", filePath);
    notifyObservers("encodingChanged", m_encoding);
    LOG_DEBUG_S("FileOperator", "openFile",
                "文件打开成功:" << filePath << "编码:" << effectiveEncoding);
    return true;
}

bool FileOperator::saveFile(const QString& filePath)
{
    QString targetPath = filePath.isEmpty() ? m_currentFilePath : filePath;

    if (targetPath.isEmpty()) {
        // 没有路径则触发另存为逻辑
        return false;   // 由调用方处理另存为对话框
    }

    // 每次保存重新打开文件写入，不依赖已打开的句柄
    // P3-M03 子项1: 不使用 QIODevice::Text（避免 Qt 自动行尾转换覆盖我们的 EOL 设置）
    QFile outFile(targetPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        LOG_DEBUG_S("FileOperator", "saveFile", "保存文件失败:" << targetPath);
        return false;
    }

    m_currentFilePath = targetPath;

    // 获取编辑器内容并写入
    if (m_contentReader) {
        QString content = m_contentReader();

        // P3-M03 子项1: 按当前 EOL 模式统一行尾
        content = convertEol(content);

        // GBK特殊处理（需要QTextCodec）
        if (m_encoding.compare("GBK", Qt::CaseInsensitive) == 0) {
            QTextCodec* codec = QTextCodec::codecForName("GBK");
            if (codec) {
                QByteArray encodedData = codec->fromUnicode(content);
                outFile.write(encodedData);
                outFile.flush();
                outFile.close();
                m_modified = false;
                notifyObservers("fileSaved", targetPath);
                return true;
            }
        }

        QTextStream out(&outFile);
        out.setEncoding(resolveEncoding(m_encoding));
        out << content;
    }

    outFile.close();
    m_modified = false;
    notifyObservers("fileSaved", m_currentFilePath);
    LOG_DEBUG_S("FileOperator", "saveFile", "文件保存成功:" << m_currentFilePath);
    return true;
}

bool FileOperator::saveAsFile(const QString& filePath)
{
    if (filePath.isEmpty()) return false;
    return saveFile(filePath);
}

void FileOperator::newFile()
{
    closeFile();
    if (m_contentWriter) {
        m_contentWriter(QString());
    }
    m_modified = false;
    notifyObservers("fileNew", QVariant());
}

bool FileOperator::hasOpenFile() const
{
    return m_file.isOpen();
}

QString FileOperator::currentFilePath() const
{
    return m_currentFilePath;
}

void FileOperator::setEncoding(const QString& encodingName)
{
    m_encoding = encodingName;
    // 如果文件已打开，用新编码重新读取
    if (m_file.isOpen()) {
        openFile(m_currentFilePath);   // 重新加载
    }
    notifyObservers("encodingChanged", m_encoding);
}

QString FileOperator::encoding() const
{
    return m_encoding;
}

bool FileOperator::isModified() const
{
    return m_modified;
}

void FileOperator::setModified(bool modified)
{
    m_modified = modified;
}

void FileOperator::closeFile()
{
    if (m_file.isOpen()) {
        m_file.close();
    }
    m_currentFilePath.clear();
    m_modified = false;
    notifyObservers("fileClosed", QVariant());
}

// ========== 辅助方法 ==========

void FileOperator::setContentReader(ContentReader reader)
{
    m_contentReader = std::move(reader);
}

void FileOperator::setContentWriter(ContentWriter writer)
{
    m_contentWriter = std::move(writer);
}

QStringConverter::Encoding FileOperator::resolveEncoding(const QString& encodingName)
{
    auto opt = QStringConverter::encodingForName(encodingName.toUtf8());
    if (opt.has_value())
        return opt.value();
    LOG_DEBUG_S("FileOperator", "resolveEncoding", "不支持的编码格式，默认UTF-8:" << encodingName);
    return QStringConverter::Utf8;
}

// P3-M03 子项1: 按当前 EOL 模式统一行尾
// Qt 内部文本使用单个 '\n'（U+000A）作为段落分隔符，将其转换为指定的 EOL 序列
QString FileOperator::convertEol(const QString& content) const
{
    if (m_eolMode.isEmpty() ||
        m_eolMode.compare(QStringLiteral("LF"), Qt::CaseInsensitive) == 0) {
        // LF: 保持 \n（无需转换）
        return content;
    }
    if (m_eolMode.compare(QStringLiteral("CRLF"), Qt::CaseInsensitive) == 0) {
        return QString(content).replace(QChar('\n'), QStringLiteral("\r\n"));
    }
    if (m_eolMode.compare(QStringLiteral("CR"), Qt::CaseInsensitive) == 0) {
        return QString(content).replace(QChar('\n'), QChar('\r'));
    }
    return content;
}
