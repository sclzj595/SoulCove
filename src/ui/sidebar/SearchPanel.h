#ifndef SEARCHPANEL_H
#define SEARCHPANEL_H

#include <QWidget>
#include <QStringList>
#include <QDir>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QPushButton;

/// @brief 搜索面板
///
/// 职责：在工作区文件中搜索文本/符号，支持正则/大小写/文件类型过滤/全局替换。
///       双击结果项打开文件并跳转到对应行。
///
/// 设计说明：
/// - 从 SideBar 抽取，自含 UI（搜索/替换输入 + 选项 + 结果列表）与逻辑
/// - 通过 setWorkspaceFolders 注入工作区文件夹列表（搜索范围）
/// - 发射 fileOpenRequested/locateRequested 信号由 SideBar 转发给 Widget
class SearchPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SearchPanel(QWidget* parent = nullptr);

    /// @brief 设置工作区文件夹列表（搜索范围）
    void setWorkspaceFolders(const QStringList& folders);

signals:
    /// @brief 请求打开文件
    void fileOpenRequested(const QString& filePath);

    /// @brief 请求定位到文件指定行/列（0-based）
    void locateRequested(const QString& filePath, int line, int col);

private slots:
    void onSearchTriggered();
    void onResultDoubleClicked(QListWidgetItem* item);
    void onReplaceAll();

private:
    /// 递归搜索文件内容
    void searchInDirectory(const QDir& dir, const QString& keyword);
    /// 检查文件是否匹配文件类型过滤器
    bool matchesFileFilter(const QString& fileName) const;
    /// 全局符号搜索 — 遍历工作区文件，正则扫描符号定义
    void performSymbolSearch(const QString& keyword);

    QLineEdit*   m_searchInput = nullptr;
    QLineEdit*   m_replaceInput = nullptr;
    QLineEdit*   m_fileFilterInput = nullptr;
    QListWidget* m_searchResults = nullptr;
    QCheckBox*   m_chkCaseSensitive = nullptr;
    QCheckBox*   m_chkRegex = nullptr;
    QCheckBox*   m_chkSymbolSearch = nullptr;
    QPushButton* m_btnReplaceAll = nullptr;

    QStringList  m_workspaceFolders;  // 搜索范围（工作区文件夹列表）
};

#endif // SEARCHPANEL_H
