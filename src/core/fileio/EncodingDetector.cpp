#include "core/fileio/EncodingDetector.h"
#include "Logger.hpp"

#include <QFile>
#include <QDebug>
#include <QMap>
#include <cmath>

// ============================================================
// EncodingDetectionResult 实现
// ============================================================

QString EncodingDetectionResult::toString(const QByteArray& data) const
{
    if (!codec || !isValid) {
        // Fallback：尝试 UTF-8
        auto* utf8Codec = QTextCodec::codecForName("UTF-8");
        if (utf8Codec) {
            return utf8Codec->toUnicode(data);
        }
        return QString::fromLatin1(data);
    }

    QTextCodec::ConverterState state;
    QString result = codec->toUnicode(data.constData(), data.size(), &state);

    if (state.invalidChars > 0) {
        LOG_WARN_S("EncodingDetector", "toString", "编码转换失败，存在" << state.invalidChars << "个无效字符");
    }

    return result;
}

// ============================================================
// EncodingDetector 核心实现
// ============================================================

EncodingDetectionResult EncodingDetector::detect(const QByteArray& rawData)
{
    EncodingDetectionResult result;

    if (rawData.isEmpty()) {
        result.encodingName = QStringLiteral("UTF-8");
        result.codec = QTextCodec::codecForName("UTF-8");
        result.confidence = 1.0;
        result.isValid = true;
        return result;
    }

    // 1. 检查 BOM 标记
    Encoding bomEncoding = checkBOM(rawData);
    bool hasBom = (bomEncoding != Encoding::Unknown);

    if (hasBom) {
        result.hasBOM = true;
        switch (bomEncoding) {
        case Encoding::UTF8_BOM:
            result.encodingName = QStringLiteral("UTF-8");
            result.codec = QTextCodec::codecForName("UTF-8");
            result.confidence = 1.0;
            break;
        case Encoding::UTF16LE:
            result.encodingName = QStringLiteral("UTF-16LE");
            result.codec = QTextCodec::codecForName("UTF-16LE");
            result.confidence = 1.0;
            break;
        case Encoding::UTF16BE:
            result.encodingName = QStringLiteral("UTF-16BE");
            result.codec = QTextCodec::codecForName("UTF-16BE");
            result.confidence = 1.0;
            break;
        default:
            break;
        }

        result.isValid = (result.codec != nullptr);
        return result;
    }

    // 2. 无BOM时使用统计分析
    int confidence = 0;
    Encoding detectedEnc = detectByStatistics(rawData, confidence);

    switch (detectedEnc) {
    case Encoding::UTF8:
        result.encodingName = QStringLiteral("UTF-8");
        result.codec = QTextCodec::codecForName("UTF-8");
        result.confidence = confidence / 100.0;
        break;
    case Encoding::GBK:
        result.encodingName = QStringLiteral("GBK");
        result.codec = QTextCodec::codecForName("GBK");
        // GBK 可能不存在，尝试 GB18030
        if (!result.codec) {
            result.codec = QTextCodec::codecForName("GB18030");
            result.encodingName = QStringLiteral("GB18030");
        }
        result.confidence = confidence / 100.0;
        break;
    case Encoding::GB18030:
        result.encodingName = QStringLiteral("GB18030");
        result.codec = QTextCodec::codecForName("GB18030");
        result.confidence = confidence / 100.0;
        break;
    case Encoding::ISO8859_1:
        result.encodingName = QStringLiteral("ISO-8859-1");
        result.codec = QTextCodec::codecForName("ISO-8859-1");
        result.confidence = confidence / 100.0;
        break;
    case Encoding::ASCII:
        result.encodingName = QStringLiteral("ASCII");
        result.codec = QTextCodec::codecForName("ASCII");
        result.confidence = 1.0;  // ASCII 是100%确定
        break;
    default:
        // 默认使用 UTF-8（最常见）
        result.encodingName = QStringLiteral("UTF-8");
        result.codec = QTextCodec::codecForName("UTF-8");
        result.confidence = 0.3;  // 低置信度
        break;
    }

    result.hasBOM = false;
    result.isValid = (result.codec != nullptr);
    return result;
}

EncodingDetectionResult EncodingDetector::detectFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARN_S("EncodingDetector", "detectFromFile", "无法打开文件:" << filePath);
        return {};
    }

    QByteArray data = file.readAll();
    file.close();

    return detect(data);
}

QString EncodingDetector::convertToUtf8(const QByteArray& data, const QString& sourceEncoding)
{
    if (sourceEncoding.isEmpty()) {
        // 自动检测编码
        auto detection = detect(data);
        return detection.toString(data);
    }

    // 使用指定编码转换
    auto* codec = QTextCodec::codecForName(sourceEncoding.toUtf8());
    if (!codec) {
        LOG_WARN_S("EncodingDetector", "convertToUtf8", "不支持的编码:" << sourceEncoding);
        return QString::fromUtf8(data);  // Fallback to UTF-8
    }

    QTextCodec::ConverterState state;
    QString result = codec->toUnicode(data.constData(), data.size(), &state);

    if (state.invalidChars > 0) {
        LOG_DEBUG_S("EncodingDetector", "convertToUtf8", "转换时有" << state.invalidChars << "个无效字符");
    }

    return result;
}

QString EncodingDetector::encodingName(Encoding enc)
{
    switch (enc) {
    case Encoding::UTF8:     return QStringLiteral("UTF-8");
    case Encoding::UTF8_BOM: return QStringLiteral("UTF-8 (BOM)");
    case Encoding::UTF16LE:  return QStringLiteral("UTF-16LE");
    case Encoding::UTF16BE:  return QStringLiteral("UTF-16BE");
    case Encoding::GBK:      return QStringLiteral("GBK");
    case Encoding::GB18030:  return QStringLiteral("GB18030");
    case Encoding::ISO8859_1:return QStringLiteral("ISO-8859-1");
    case Encoding::ASCII:    return QStringLiteral("ASCII");
    default:                 return QStringLiteral("Unknown");
    }
}

bool EncodingDetector::isValidUtf8(const QByteArray& data)
{
    // 简化的 UTF-8 有效性检查
    int i = 0;
    while (i < data.size()) {
        unsigned char ch = static_cast<unsigned char>(data.at(i));

        if (ch <= 0x7F) {
            // ASCII: 0xxxxxxx
            ++i;
        } else if ((ch & 0xE0) == 0xC0) {
            // 2字节序列: 110xxxxx 10xxxxxx
            if (i + 1 >= data.size() || (data.at(i+1) & 0xC0) != 0x80)
                return false;
            i += 2;
        } else if ((ch & 0xF0) == 0xE0) {
            // 3字节序列: 1110xxxx 10xxxxxx 10xxxxxx
            if (i + 2 >= data.size() ||
                (data.at(i+1) & 0xC0) != 0x80 ||
                (data.at(i+2) & 0xC0) != 0x80)
                return false;
            i += 3;
        } else if ((ch & 0xF8) == 0xF0) {
            // 4字节序列: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            if (i + 3 >= data.size() ||
                (data.at(i+1) & 0xC0) != 0x80 ||
                (data.at(i+2) & 0xC0) != 0x80 ||
                (data.at(i+3) & 0xC0) != 0x80)
                return false;
            i += 4;
        } else {
            return false;  // 无效的 UTF-8 起始字节
        }
    }

    return true;
}

// ============================================================
// 内部检测方法
// ============================================================

EncodingDetector::Encoding EncodingDetector::checkBOM(const QByteArray& data)
{
    // UTF-8 BOM: EF BB BF
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        return Encoding::UTF8_BOM;
    }

    // UTF-16 LE BOM: FF FE
    if (data.size() >= 2 &&
        static_cast<unsigned char>(data[0]) == 0xFF &&
        static_cast<unsigned char>(data[1]) == 0xFE) {
        return Encoding::UTF16LE;
    }

    // UTF-16 BE BOM: FE FF
    if (data.size() >= 2 &&
        static_cast<unsigned char>(data[0]) == 0xFE &&
        static_cast<unsigned char>(data[1]) == 0xFF) {
        return Encoding::UTF16BE;
    }

    return Encoding::Unknown;
}

EncodingDetector::Encoding EncodingDetector::detectByStatistics(const QByteArray& data, int& confidence)
{
    confidence = 0;

    // 快速检查：是否为纯 ASCII
    bool isAscii = true;
    for (int i = 0; i < data.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(data.at(i));
        if (ch > 0x7F) {
            isAscii = false;
            break;
        }
    }

    if (isAscii) {
        confidence = 100;
        return Encoding::ASCII;
    }

    // 检查是否为有效的 UTF-8
    if (isValidUtf8(data)) {
        confidence = 90;
        return Encoding::UTF8;
    }

    // 统计字节分布：检测 GBK/GB18030 特征
    // GBK 高位字节范围：0x81-0xFE（排除 0x7F）
    // GBK 低位字节范围：0x40-0x7E, 0x80-0xFE
    int gbkSequences = 0;
    int totalMultiByte = 0;

    for (int i = 0; i < data.size() - 1; ++i) {
        unsigned char ch1 = static_cast<unsigned char>(data.at(i));

        if (ch1 > 0x80) {  // 高位字节
            unsigned char ch2 = static_cast<unsigned char>(data.at(i + 1));

            // GBK 高位字节特征
            if (ch1 >= 0x81 && ch1 <= 0xFE) {
                // GBK 低位字节特征
                if ((ch2 >= 0x40 && ch2 <= 0x7E) || (ch2 >= 0x80 && ch2 <= 0xFE)) {
                    ++gbkSequences;
                    ++totalMultiByte;
                    ++i;  // 跳过低位字节
                }
            }
        }
    }

    // 如果 GBK 序列占比超过阈值，判定为 GBK
    if (totalMultiByte > 0) {
        double gbkRatio = static_cast<double>(gbkSequences) / totalMultiByte;
        if (gbkRatio > 0.6) {  // 60% 以上符合 GBK 特征
            confidence = static_cast<int>(gbkRatio * 85);  // 最高85置信度
            return Encoding::GBK;
        }
    }

    // 默认返回 ISO-8859-1（单字节编码，不会出错）
    confidence = 20;
    return Encoding::ISO8859_1;
}
