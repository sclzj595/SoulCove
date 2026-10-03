#include "core/fileio/FileOperator.h"
#include "controller/FileController.h"
#include "Logger.hpp"
#include <QDebug>
#include <QWidget>

FileOperator::FileOperator(QObject* parent)
    : Subject(parent), m_encoding(QStringLiteral("UTF-8"))
{
}

// ========== IFileOperator 实现 ==========

// M3: 双轨收敛 —— FileOperator 曾与 FileController 是两套并行读写实现，
//     编码检测/Truncate/BOM 等修复需在两处同步（已实际漂移出 3 个 bug）。
//     现读写统一委托 FileController，本类只保留状态管理 + 观察者通知 + 回调解耦。

bool FileOperator::openFile(const QString& filePath)
{
    if (filePath.isEmpty()) return false;

    if (!FileController::exists(filePath)) {
        LOG_DEBUG_S("FileOperator", "openFile", "文件不存在:" << filePath);
        return false;
    }

    m_currentFilePath = filePath;
    m_modified = false;

    // 读路径统一走 FileController::readFile（编码检测/BOM 单一实现）
    QString detected;
    const QString content = FileController::readFile(filePath, &detected);

    // T8: Auto/UTF-8（默认）时采纳检测结果（含 "UTF-8 (BOM)"）
    if (m_encoding.compare("Auto", Qt::CaseInsensitive) == 0 ||
        m_encoding.compare("UTF-8", Qt::CaseInsensitive) == 0) {
        if (!detected.isEmpty()) m_encoding = detected;
    }

    if (m_contentWriter) {
        m_contentWriter(content);
    }

    notifyObservers("fileOpened", filePath);
    notifyObservers("encodingChanged", m_encoding);
    LOG_DEBUG_S("FileOperator", "openFile",
                "文件打开成功:" << filePath << "编码:" << m_encoding);
    return true;
}

bool FileOperator::saveFile(const QString& filePath)
{
    QString targetPath = filePath.isEmpty() ? m_currentFilePath : filePath;

    if (targetPath.isEmpty()) {
        // 没有路径则触发另存为逻辑
        return false;   // 由调用方处理另存为对话框
    }
    if (!m_contentReader) return false;

    // 写路径统一走 FileController::writeFile
    // （Truncate / BOM 回写 / GBK 缺 codec 拒写 / EOL 归一化均在单一实现内）
    const QString content = m_contentReader();
    if (!FileController::writeFile(targetPath, content, m_encoding, m_eolMode)) {
        LOG_DEBUG_S("FileOperator", "saveFile", "保存文件失败:" << targetPath);
        return false;
    }

    m_currentFilePath = targetPath;
    m_modified = false;
    notifyObservers("fileSaved", targetPath);
    LOG_DEBUG_S("FileOperator", "saveFile", "文件保存成功:" << targetPath);
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
    // M3: openFile 读完即 close 句柄，m_file.isOpen() 恒为 false，
    //     以路径非空作为"已打开文件"的真实判定
    return !m_currentFilePath.isEmpty();
}

QString FileOperator::currentFilePath() const
{
    return m_currentFilePath;
}

void FileOperator::setEncoding(const QString& encodingName)
{
    m_encoding = encodingName;
    // M3: 同 hasOpenFile —— openFile 后句柄已关闭，m_file.isOpen() 恒 false
    //     导致"切编码重读文件"分支永不执行
    if (!m_currentFilePath.isEmpty()) {
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
