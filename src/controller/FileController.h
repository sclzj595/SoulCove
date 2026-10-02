#ifndef FILECONTROLLER_H
#define FILECONTROLLER_H

#include <QString>

/// @brief 文件 IO 统一入口控制器
///
/// 职责：消除 Widget 中直通 QFile/QTextStream 的散点调用，
/// 统一封装文件读写与文件系统操作（创建/删除/重命名/移动）。
///
/// 设计说明：
/// - 静态工具类（无状态），与 EditorActions 保持一致风格
/// - 状态化的编辑器绑定文件操作仍由 IFileOperator/FileOperator 承担
/// - 读取自动走 EncodingDetector 编码检测；写入按指定编码（默认 UTF-8）
class FileController
{
public:
    // === 文件读写 ===

    /// 读取文件全部内容（自动编码检测；失败返回空串并写日志）
    /// @param detectedEncoding 可选输出：检测到的编码名
    static QString readFile(const QString& filePath,
                            QString* detectedEncoding = nullptr);

    /// P3-M03 子项1: 从原始字节流检测行尾类型（"LF" / "CRLF" / "CR"）
    /// 统计前 16KB 中各 EOL 序列出现次数，取最多的作为结果
    static QString detectEol(const QByteArray& rawData);

    /// P3-M03 子项1: 读取文件并检测行尾类型
    /// @param detectedEol 可选输出：检测到的行尾类型（"LF"/"CRLF"/"CR"）
    static QString readFileWithEol(const QString& filePath,
                                   QString* detectedEol = nullptr);

    /// 写入文件内容（按指定编码；默认 UTF-8）
    /// @param eol 行尾类型（"LF"/"CRLF"/"CR"，空字符串表示不转换）
    static bool writeFile(const QString& filePath,
                          const QString& content,
                          const QString& encoding = QStringLiteral("UTF-8"),
                          const QString& eol = QString());

    /// 创建空文件（若已存在则截断）
    static bool createFile(const QString& filePath);

    // === 文件系统操作 ===

    static bool deleteFile(const QString& filePath);
    static bool renameFile(const QString& filePath, const QString& newPath);

    /// 移动文件（跨盘符或目标已存在时自动 fallback 到 copy+remove）
    /// @param overwrite 是否覆盖已存在的目标
    static bool moveFile(const QString& sourcePath,
                         const QString& targetPath,
                         bool overwrite = false);

    static bool exists(const QString& filePath);

    // === 路径工具（收敛 QFileInfo 散点） ===

    static QString fileName(const QString& filePath);
    static QString absolutePath(const QString& filePath);
    static QString absoluteFilePath(const QString& filePath);

private:
    FileController() = delete;  // 纯静态工具类
};

#endif // FILECONTROLLER_H
