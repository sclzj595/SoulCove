#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QStackedWidget>
#include <QPushButton>
#include <QTableWidget>

/// @brief 设置页面（标签页内嵌，对标VSCode设置面板）
/// 左侧分类导航 + 右侧配置项卡片式布局
/// 包含：外观、编辑器、终端、智能提示等分类
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    /// 设置分类枚举
    enum SettingsCategory {
        Appearance = 0,
        Editor = 1,
        Terminal = 2,
        Completion = 3,
        Shortcuts = 4,
        LSP = 5
    };
    Q_ENUM(SettingsCategory)

    explicit SettingsPage(QWidget* parent = nullptr);

signals:
    /// 主题切换请求
    void themeChanged(const QString& themeKey);
    /// 字体大小变更
    void fontSizeChanged(int size);
    /// 配置变更（通知保存）
    void configChanged();
    /// 终端外观配置变更
    void terminalAppearanceChanged();

private slots:
    void onThemeChanged(int index);
    void onFontSizeChanged(int value);
    void onAutoSaveToggled(bool checked);
    void onCompletionToggled(bool checked);
    void onLineNumbersToggled(bool checked);
    void onTabSizeChanged(int value);
    void onIndentStyleChanged(int index);          // M4
    void onFormatToolPathClicked();                // M4
    void onAutoFormatJsonToggled(bool checked);    // M10
    void onTerminalTypeChanged(int index);
    void onTerminalFontChanged(int value);
    void onTerminalFontFamilyChanged(int index);
    void onTerminalFgColorClicked();
    void onTerminalBgColorClicked();
    void onTerminalCursorColorClicked();
    void onTerminalCursorShapeChanged(int index);
    void onTerminalCursorBlinkToggled(bool checked);
    void onCompletionDelayChanged(int value);
    void onMinPrefixChanged(int value);
    void onMatchingModeChanged(int index);
    void onCategoryChanged(int row);
    void onSearchTextChanged(const QString& text);
    void onResetCurrentSection();
    void onResetAll();
    void onExportConfig();
    void onImportConfig();
    void onShortcutModifyClicked(int row);
    void onShortcutSearchChanged(const QString& text);
    void onShortcutResetDefaults();
    void onShortcutExport();
    void onShortcutImport();
    void onLanguageChanged(int index);
    void onLspPythonPathClicked();
    void onLspCppPathClicked();
    void onLspJsPathClicked();
    void onLspAutoStartToggled(bool checked);

private:
    void setupUI();
    void loadCurrentConfig();
    void createAppearancePage(QWidget* page);
    void createEditorPage(QWidget* page);
    void createTerminalPage(QWidget* page);
    void createCompletionPage(QWidget* page);
    void createLspPage(QWidget* page);
    void createShortcutsPage();

    /// 刷新快捷键表格（带搜索过滤）
    void refreshShortcutTable(const QString& filter);

    /// 根据搜索关键词显示/隐藏配置项
    void filterSettings(const QString& keyword);

    // 导航
    QListWidget*   m_categoryList;
    QStackedWidget* m_pageStack;
    QLineEdit*     m_searchInput;
    QPushButton*   m_btnResetSection;
    QPushButton*   m_btnResetAll;

    // === 外观配置 ===
    QComboBox* m_themeCombo;
    QSpinBox*  m_fontSizeSpin;
    QComboBox* m_languageCombo;  // 语言选择（简体中文 / English）

    // === 编辑器配置 ===
    QCheckBox* m_autoSaveCheck;
    QCheckBox* m_lineNumbersCheck;
    QSpinBox*  m_tabSizeSpin;
    QComboBox* m_indentStyleCombo;       // M4: 缩进风格 (Spaces/Tabs)
    QLabel*     m_formatToolPathLabel;   // M4: 格式化工具路径
    QPushButton* m_formatToolPathBtn;   // M4: 格式化工具路径选择按钮
    QCheckBox*  m_autoFormatJsonCheck;  // M10: 保存时自动格式化JSON

    // === 终端配置 ===
    QComboBox* m_terminalTypeCombo;
    QSpinBox*  m_terminalFontSpin;
    QComboBox* m_terminalFontFamilyCombo;
    QPushButton* m_terminalFgColorBtn;
    QLabel*     m_terminalFgColorLabel;
    QPushButton* m_terminalBgColorBtn;
    QLabel*     m_terminalBgColorLabel;
    QPushButton* m_terminalCursorColorBtn;
    QLabel*     m_terminalCursorColorLabel;
    QComboBox* m_terminalCursorShapeCombo;
    QCheckBox* m_terminalCursorBlinkCheck;

    // === 智能提示配置 ===
    QCheckBox* m_completionCheck;
    QSpinBox*  m_completionDelaySpin;
    QSpinBox*  m_minPrefixSpin;
    QComboBox* m_matchingModeCombo;

    // 配置项容器（用于搜索过滤）
    QList<QWidget*> m_allSettingWidgets;

    // === 配置导出/导入 ===
    QPushButton* m_btnExportConfig;
    QPushButton* m_btnImportConfig;

    // === 快捷键配置 ===
    QTableWidget* m_shortcutTable;
    QLineEdit*     m_shortcutSearchInput;

    // === LSP 配置 ===
    QLabel*     m_lspPythonPathLabel;
    QPushButton* m_lspPythonPathBtn;
    QLabel*     m_lspCppPathLabel;
    QPushButton* m_lspCppPathBtn;
    QLabel*     m_lspJsPathLabel;
    QPushButton* m_lspJsPathBtn;
    QCheckBox*  m_lspAutoStartCheck;

    /// 快捷键数据结构
    struct ShortcutItem {
        QString commandName;   // 命令名称
        QString keySequence;   // 当前快捷键
        QString category;      // 分类
        QString defaultKey;    // 默认快捷键
    };
    QList<ShortcutItem> m_shortcutItems;  // 所有快捷键数据
};

#endif // SETTINGSPAGE_H
