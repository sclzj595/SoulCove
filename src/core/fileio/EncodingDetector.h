#ifndef ENCODINGDETECTOR_H
#define ENCODINGDETECTOR_H

#include <QString>
#include <QByteArray>
#include <QTextCodec>
#include <memory>

/// @brief 文件编码检测结果
struct EncodingDetectionResult {
    QString encodingName;        ///< 编码名称（如 "UTF-8", "GBK", "UTF-16LE"）
    QTextCodec* codec = nullptr; ///< 对应的 QTextCodec（RAII：不负责释放）
    double confidence = 0.0;     ///< 置信度 (0.0 - 1.0)
    bool hasBOM = false;         ///< 是否有 BOM 标记
    bool isValid = false;        ///  检测是否成功

    /// @brief 转换字节数组到字符串
    QString toString(const QByteArray& data) const;
};

/// @brief 文件编码自动检测器
///
/// 功能：
/// - 自动识别常见文本编码（UTF-8/GBK/UTF-16/ISO-8859-1等）
/// - BOM 标记检测
/// - 统计特征分析（字节分布、高频字符）
/// - 提供编码转换功能
///
/// RAII保证：
/// - 无动态资源分配（使用栈对象）
/// - 异常安全的检测流程
class EncodingDetector
{
public:
    /// @brief 支持的编码列表
    enum class Encoding {
        UTF8,           // UTF-8 (with/without BOM)
        UTF8_BOM,       // UTF-8 with BOM
        UTF16LE,        // UTF-16 Little Endian
        UTF16BE,        // UTF-16 Big Endian
        GBK,            // GBK/GB2312 (中文)
        GB18030,        // GB18030 (中文扩展)
        ISO8859_1,      // Latin-1 (西欧)
        ASCII,          // 纯 ASCII
        Unknown         // 无法确定
    };

    /// @brief 检测文件编码
    /// @param rawData 原始文件数据
    /// @return 检测结果（包含编码名称和置信度）
    static EncodingDetectionResult detect(const QByteArray& rawData);

    /// @brief 检测文件编码（从文件路径读取）
    /// @param filePath 文件路径
    /// @return 检测结果
    static EncodingDetectionResult detectFromFile(const QString& filePath);

    /// @brief 将数据转换为指定编码的字符串
    /// @param data 原始数据
    /// @param targetEncoding 目标编码（默认 UTF-8）
    /// @return 转换后的字符串（失败返回空字符串）
    static QString convertToUtf8(const QByteArray& data,
                                  const QString& sourceEncoding = QString());

    /// @brief 获取编码的可读名称
    static QString encodingName(Encoding enc);

    /// @brief 检查是否为有效的 UTF-8 序列
    static bool isValidUtf8(const QByteArray& data);

private:
    /// @brief 检查 BOM 标记
    static Encoding checkBOM(const QByteArray& data);

    /// @brief 基于统计分析检测编码（无BOM时）
    static Encoding detectByStatistics(const QByteArray& data, int& confidence);
};

#endif // ENCODINGDETECTOR_H
