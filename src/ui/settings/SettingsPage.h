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
#include <QPlainTextEdit>
#include <QTimer>

class QPlainTextEdit;

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
        LSP = 5,
        Build = 6,      // P1 C05-2: 构建配置页
        Markdown = 7    // P3-M02 子项2: Markdown 自定义 CSS 配置页
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
    void onSpellCheckToggled(bool checked);        // P3-M03 子项5: 拼写检查开关
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
    void onShortcutSearchDebounced();           // P2-H05 子项2: 200ms 防抖触发
    void onShortcutCellDoubleClicked(int row, int col);  // P2-H05 子项3: 双击进入录制
    void onShortcutResetDefaults();             // P2-H05 子项4: 重置为默认
    void onShortcutApplyVSCode();               // P2-H05 子项4: 切换 VSCode 预设
    void onShortcutExport();                    // P2-H05 子项4: 导出
    void onShortcutImport();                    // P2-H05 子项4: 导入
    void onLanguageChanged(int index);
    void onLspPythonPathClicked();
    void onLspCppPathClicked();
    void onLspJsPathClicked();
    void onLspAutoStartToggled(bool checked);
    // P1 C05-2: 构建配置页槽函数
    void onBuildQtPathClicked();
    void onBuildOpenSslPathClicked();
    void onBuildZlibPathClicked();
    void onBuildTypeChanged(int index);
    // P1 C05-3: 自动检测 Qt 安装
    void onBuildAutoDetectQt();
    // P1 C05-4: 构建目录配置
    void onBuildDirClicked();
    void onBuildSeparateDirsToggled(bool checked);
    // P1 C05-2: 编译器路径与 CMake 额外参数
    void onBuildCompilerPathClicked();
    void onBuildApplyReconfigure();
    // P3-M02 子项2: Markdown 自定义 CSS 配置页槽函数
    void onMdCssApplyClicked();
    void onMdCssImportClicked();
    void onMdCssResetClicked();

private:
    void setupUI();
    void loadCurrentConfig();
    void createAppearancePage(QWidget* page);
    void createEditorPage(QWidget* page);
    void createTerminalPage(QWidget* page);
    void createCompletionPage(QWidget* page);
    void createLspPage(QWidget* page);
    void createBuildPage(QWidget* page);  // P1 C05-2: 构建配置页
    void createMarkdownPage(QWidget* page);  // P3-M02 子项2: Markdown 自定义 CSS 配置页
    void createShortcutsPage();

    /// P1 C05-3: 自动检测系统已安装的 Qt 版本
    /// 扫描所有盘符根目录下的 \Qt\<版本>\<编译器> 目录，
    /// 验证 lib/cmake/Qt6/Qt6Config.cmake 或 Qt5/Qt5Config.cmake 是否存在
    /// @return 检测到的 Qt 安装路径列表（绝对路径）
    static QStringList detectQtInstallations();

    /// 刷新快捷键表格（带搜索过滤）
    void refreshShortcutTable(const QString& filter);

    /// J3/J4: 应用快捷键页面主题样式（表格、搜索框、按钮）
    /// 切换主题时由 ThemeManager::themeChanged 触发重新调用
    void applyShortcutPageTheme();

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
    QCheckBox*  m_spellCheckCheck;      // P3-M03 子项5: 拼写检查开关

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
    QTimer*        m_shortcutSearchDebounceTimer = nullptr;  // P2-H05 子项2: 200ms 防抖
    QString        m_pendingShortcutKeyword;                 // 防抖窗口内最新关键词
    QPushButton*   m_btnShortcutVSCode = nullptr;            // P2-H05 子项4: 切换 VSCode 预设按钮

    // === LSP 配置 ===
    QLabel*     m_lspPythonPathLabel;
    QPushButton* m_lspPythonPathBtn;
    QLabel*     m_lspCppPathLabel;
    QPushButton* m_lspCppPathBtn;
    QLabel*     m_lspJsPathLabel;
    QPushButton* m_lspJsPathBtn;
    QCheckBox*  m_lspAutoStartCheck;

    // === P1 C05-2: 构建配置 ===
    QLabel*     m_buildQtPathLabel;
    QPushButton* m_buildQtPathBtn;
    QPushButton* m_buildAutoDetectBtn;  // C05-3: 自动检测 Qt
    QLabel*     m_buildOpenSslPathLabel;
    QPushButton* m_buildOpenSslPathBtn;
    QLabel*     m_buildZlibPathLabel;
    QPushButton* m_buildZlibPathBtn;
    QComboBox*  m_buildTypeCombo;
    QLabel*     m_buildDirLabel;          // C05-4: 构建目录
    QPushButton* m_buildDirBtn;           // C05-4: 构建目录选择
    QCheckBox*  m_buildSeparateDirsCheck; // C05-4: 按构建类型分离目录
    QComboBox*  m_buildCompilerCombo;    // 编译器类型 (MinGW/MSVC)
    QLabel*     m_buildCompilerPathLabel; // 编译器路径显示
    QPushButton* m_buildCompilerPathBtn;  // 编译器路径浏览
    QLineEdit*  m_buildCmakeArgsEdit;    // CMake 额外参数
    QPushButton* m_buildApplyBtn;        // 应用并重新配置

    // === P3-M02 子项2: Markdown 自定义 CSS 配置 ===
    QPlainTextEdit* m_mdCssEdit = nullptr;     ///< CSS 编辑区
    QPushButton*    m_mdCssApplyBtn = nullptr; ///< 应用按钮
    QPushButton*    m_mdCssImportBtn = nullptr;///< 从文件导入 CSS
    QPushButton*    m_mdCssResetBtn = nullptr; ///< 重置（清空用户 CSS，使用主题预设）

    /// 快捷键数据结构
    struct ShortcutItem {
        QString id;             // P2-H05: 命令ID（用于 ShortcutManager::setShortcut）
        QString commandName;   // 命令名称（displayName）
        QString description;   // P2-H05: 描述（描述列）
        QString keySequence;   // 当前快捷键
        QString category;      // 分类
        QString defaultKey;    // 默认快捷键
    };
    QList<ShortcutItem> m_shortcutItems;  // 所有快捷键数据

    // Bug5: 搜索防抖定时器 — 避免每次按键同步过滤导致主线程阻塞/白屏
    QTimer* m_searchDebounceTimer = nullptr;
    QString m_pendingKeyword;  // 待过滤的关键词（防抖窗口内最新输入）
};

#endif // SETTINGSPAGE_H
