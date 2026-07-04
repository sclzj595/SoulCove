#ifndef SNIPPETMANAGERDIALOG_H
#define SNIPPETMANAGERDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

struct CodeSnippet;

/// @brief 代码片段管理对话框（P2-H02 子项1）
/// 左侧列表 + 右侧详情编辑 + 底部操作按钮
/// 支持 CRUD、内部 JSON 导入导出、VSCode 格式导入导出
class SnippetManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SnippetManagerDialog(QWidget* parent = nullptr);

signals:
    /// 片段集合发生变更（增删改/导入）时发出，通知 Widget 刷新补全
    void snippetChanged();

private slots:
    void onAddSnippet();
    void onDeleteSnippet();
    void onImportJson();
    void onExportJson();
    void onImportVscode();
    void onExportVscode();
    void onApplyEdit();          // 应用右侧编辑区改动到当前选中片段
    void onSelectionChanged();   // 左侧列表选中变化 → 右侧刷新详情

private:
    void setupUI();
    void refreshList();                   // 刷新左侧列表
    void loadDetail(const CodeSnippet& s);// 加载片段到右侧编辑区
    void clearDetail();                   // 清空右侧编辑区
    bool applyCurrentEdit();              // 将右侧编辑区内容写回 SnippetManager，成功返回 true
    void notifyChanged();                 // 保存后发出 snippetChanged

    // === 左侧：片段列表 ===
    QListWidget* m_listWidget;
    QLabel*      m_countLabel;

    // === 右侧：详情编辑区 ===
    QLineEdit* m_nameEdit;
    QLineEdit* m_prefixEdit;
    QComboBox* m_languageCombo;
    QLineEdit* m_descEdit;
    QTextEdit* m_bodyEdit;
    QLabel*    m_hintLabel;       // 操作提示（如占位符语法说明）

    // === 底部按钮 ===
    QPushButton* m_btnAdd;
    QPushButton* m_btnDelete;
    QPushButton* m_btnApply;      // 应用编辑
    QPushButton* m_btnImport;
    QPushButton* m_btnExport;
    QPushButton* m_btnImportVscode;
    QPushButton* m_btnExportVscode;
    QPushButton* m_btnClose;

    QString m_currentId;          // 当前选中的片段 ID（空表示新建/未选中）
};

#endif // SNIPPETMANAGERDIALOG_H
