#ifndef FINDREPLACEBAR_H
#define FINDREPLACEBAR_H

#include <QWidget>
#include <QTextDocument>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class MyTextEdit;

/// @brief 查找替换面板（V1.9 增强）
///
/// 特性：
/// - 持久化嵌入编辑器顶部（非模态对话框）
/// - 支持正则表达式查找
/// - 支持大小写敏感切换
/// - 支持全字匹配
/// - 查找下一个/上一个
/// - 替换当前 + 全部替换
/// - 匹配计数显示
/// - Esc 关闭面板
class FindReplaceBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindReplaceBar(QWidget* parent = nullptr);

    /// 绑定目标编辑器
    void setEditor(MyTextEdit* editor);

    /// 显示面板并聚焦查找输入框
    void showFind();

    /// 显示面板并展开替换输入框
    void showReplace();

    /// 隐藏面板
    void hideBar();

public slots:
    /// 查找下一个
    void findNext();

    /// 查找上一个
    void findPrev();

    /// 替换当前匹配
    void replaceOne();

    /// 全部替换
    void replaceAll();

signals:
    /// 面板关闭信号
    void barHidden();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onFindTextChanged();
    void onToggleReplaceMode(bool checked);

private:
    // 控件
    QLineEdit*  m_findEdit = nullptr;
    QLineEdit*  m_replaceEdit = nullptr;
    QPushButton* m_btnFindNext = nullptr;
    QPushButton* m_btnFindPrev = nullptr;
    QPushButton* m_btnReplace = nullptr;
    QPushButton* m_btnReplaceAll = nullptr;
    QPushButton* m_btnClose = nullptr;
    QCheckBox*  m_chkCaseSensitive = nullptr;
    QCheckBox*  m_chkWholeWord = nullptr;
    QCheckBox*  m_chkRegex = nullptr;
    QLabel*     m_matchCountLabel = nullptr;

    // 状态
    MyTextEdit* m_editor = nullptr;
    bool        m_replaceModeVisible = false;

    /// 执行查找（返回是否找到）
    bool doFind(bool forward);

    /// 获取查找标志
    QTextDocument::FindFlags findFlags() const;

    /// 更新匹配计数
    void updateMatchCount();

    /// 高亮所有匹配项
    void highlightAllMatches();

    /// 应用主题样式
    void applyThemeStyle();
};

#endif // FINDREPLACEBAR_H
