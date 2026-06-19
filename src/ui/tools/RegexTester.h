#ifndef REGEXTESTER_H
#define REGEXTESTER_H

#include <QWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>

/// @brief 正则表达式测试器
/// 实时测试正则表达式匹配结果，支持多种语法模式和匹配选项
class RegexTester : public QWidget
{
    Q_OBJECT

public:
    explicit RegexTester(QWidget* parent = nullptr);

    /// 设置要测试的文本
    void setTestText(const QString& text);

private slots:
    void onRegexChanged();         // 正则变化时重新匹配
    void onOptionChanged();        // 选项变化时重新匹配
    void onCopyMatchedText();      // 复制匹配结果

private:
    void updateResults();

    QLineEdit* m_regexEdit;        // 正则表达式输入
    QTextEdit* m_testTextEdit;     // 测试文本输入
    QTextEdit* m_resultTextEdit;   // 匹配结果显示（富文本高亮）
    QLabel* m_statusLabel;         // 状态: "找到 N 个匹配" 或 "语法错误: xxx"

    QCheckBox* m_caseSensitive;    // 区分大小写
    QCheckBox* m_multiline;        // 多行模式
    QCheckBox* m_dotAll;           // . 匹配换行符
    QComboBox* m_patternSyntax;    // 语法: RegExp / Wildcard / FixedString

    QPushButton* m_btnCopy;        // 复制全部匹配按钮

    QTimer* m_debounceTimer;       // 防抖定时器（300ms）
};

#endif // REGEXTESTER_H
