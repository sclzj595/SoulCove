#ifndef FILEOPERATOR_H
#define FILEOPERATOR_H

#include "interfaces/core/IFileOperator.h"
#include "core/base/Subject.h"
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QTextCodec>

/// @brief 文件操作实现类
/// 基于IFileOperator接口实现文件读写、编码解析、状态管理
class FileOperator : public IFileOperator, public Subject
{
public:
    explicit FileOperator(QObject* parent = nullptr);
    ~FileOperator() override = default;

    // === IFileOperator 接口实现 ===
    bool openFile(const QString& filePath) override;
    bool saveFile(const QString& filePath = QString()) override;
    bool saveAsFile(const QString& filePath) override;
    void newFile() override;
    bool hasOpenFile() const override;
    QString currentFilePath() const override;
    void setEncoding(const QString& encodingName) override;
    QString encoding() const override;
    bool isModified() const override;
    void setModified(bool modified) override;  // 外部设置修改状态
    void closeFile() override;

    /// 注册观察者（IFileOperator接口实现，委托给Subject）
    void attachObserver(IObserver* observer) override {
        Subject::attachObserver(observer);
    }

    /// 设置编辑器内容读取回调（解耦：不依赖具体编辑器类）
    using ContentReader = std::function<QString()>;
    using ContentWriter = std::function<void(const QString&)>;

    void setContentReader(ContentReader reader) override;
    void setContentWriter(ContentWriter writer) override;

    // === P3-M03 子项1: EOL（行尾）配置 ===
    /// 设置保存时使用的行尾类型（"LF" / "CRLF" / "CR"），空字符串表示不转换
    void setEolMode(const QString& eol) { m_eolMode = eol; }
    /// 获取当前行尾类型
    QString eolMode() const { return m_eolMode; }

private:
    QFile m_file;
    QString m_currentFilePath;
    QString m_encoding;
    bool m_modified = false;
    ContentReader m_contentReader;
    ContentWriter m_contentWriter;
    QString m_eolMode;  // P3-M03 子项1: 行尾类型（"LF"/"CRLF"/"CR"，空表示不强制转换）

    QStringConverter::Encoding resolveEncoding(const QString& encodingName);

    /// P3-M03 子项1: 按 m_eolMode 转换文本行尾
    QString convertEol(const QString& content) const;
};

#endif // FILEOPERATOR_H
