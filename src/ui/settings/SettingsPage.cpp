#include "ui/settings/SettingsPage.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"
#include "core/i18n/I18nManager.h"  // P3-M05: 语言切换
#include "core/shortcut/ShortcutManager.h"  // T7: 快捷键管理器
#include "core/build/QtDetector.h"          // P1 C05-3: Qt 安装检测器
#include "ui/snippet/SnippetManagerDialog.h"  // P2-H02 子项1: 代码片段管理对话框
#include "ui/shortcut/KeySequenceEdit.h"    // P2-H05 子项3: 按键录制输入框

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include "ui/dialog/ModernDialog.h"
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QInputDialog>   // C05-3: 自动检测 Qt 选择对话框
#include <QMessageBox>    // C05-3: 检测结果提示
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QColorDialog>
#include <QDialog>
#include <QKeyEvent>
#include <QHeaderView>
#include <QPointer>   // P2-H05 子项3: KeySequenceEdit 生命周期守卫

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("settingsPage"));
    setupUI();
    loadCurrentConfig();

    // 监听配置中心变更 — 当编辑器 Ctrl+滚轮缩放字体时，同步更新 SpinBox（双向联动）
    // QSignalBlocker 防止 setValue 再次触发 onFontSizeChanged → setFontSize 造成回环
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, [this](const QString& key, const QVariant& value) {
        if (key == QStringLiteral("Display/fontSize") && m_fontSizeSpin) {
            int size = value.toInt();
            if (m_fontSizeSpin->value() != size) {
                QSignalBlocker blocker(m_fontSizeSpin);
                m_fontSizeSpin->setValue(size);
            }
        }
    });

    // J3: 监听主题切换 — 切换主题后重新应用快捷键页面样式，修复表格不刷新 bug
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](const QString&) {
        applyShortcutPageTheme();
    });

    // Bug5: 搜索防抖定时器 — 150ms 防抖，避免每次按键同步全量过滤导致主线程阻塞/白屏
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(150);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
        filterSettings(m_pendingKeyword);
    });
}

void SettingsPage::setupUI()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === 左侧：搜索 + 分类导航 ===
    auto* navWidget = new QWidget(this);
    navWidget->setFixedWidth(200);
    auto* navLayout = new QVBoxLayout(navWidget);
    navLayout->setContentsMargins(12, 16, 8, 16);
    navLayout->setSpacing(8);

    // 搜索框
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(tr("搜索设置..."));
    m_searchInput->setObjectName(QStringLiteral("searchInput"));
    navLayout->addWidget(m_searchInput);

    // 分类列表
    m_categoryList = new QListWidget(this);
    m_categoryList->setObjectName(QStringLiteral("sideFileList"));
    m_categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categoryList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categoryList->setFrameShape(QFrame::NoFrame);
    // P3-M05: 分类列表项设置最小高度，防止不同语言切换时项高度跳变
    m_categoryList->setIconSize(QSize(16, 16));
    // 通过 QSS 设置 item padding 提高最小高度（语言切换时保持稳定布局）
    m_categoryList->setStyleSheet(QStringLiteral(
        "QListWidget#sideFileList::item { padding: 8px 12px; min-height: 28px; }"
    ));

    m_categoryList->addItem(tr("外观"));
    m_categoryList->addItem(tr("编辑器"));
    m_categoryList->addItem(tr("终端"));
    m_categoryList->addItem(tr("智能提示"));
    m_categoryList->addItem(tr("快捷键"));
    m_categoryList->addItem(tr("LSP 语言服务器"));
    m_categoryList->addItem(tr("构建配置"));   // P1 C05-2
    m_categoryList->addItem(tr("Markdown"));   // P3-M02 子项2
    m_categoryList->setCurrentRow(0);

    navLayout->addWidget(m_categoryList);

    // 恢复默认按钮
    auto* btnLayout = new QVBoxLayout();
    m_btnResetSection = new QPushButton(tr("恢复本页默认"), this);
    m_btnResetAll = new QPushButton(tr("恢复全部默认"), this);
    m_btnResetSection->setObjectName(QStringLiteral("btnResetSection"));
    m_btnResetAll->setObjectName(QStringLiteral("btnResetAll"));
    btnLayout->addWidget(m_btnResetSection);
    btnLayout->addWidget(m_btnResetAll);

    // 配置导出/导入按钮
    m_btnExportConfig = new QPushButton(tr("导出配置"), this);
    m_btnImportConfig = new QPushButton(tr("导入配置"), this);
    btnLayout->addWidget(m_btnExportConfig);
    btnLayout->addWidget(m_btnImportConfig);

    navLayout->addLayout(btnLayout);

    navLayout->addStretch();

    mainLayout->addWidget(navWidget);

    // === 右侧：配置内容区 ===
    m_pageStack = new QStackedWidget(this);

    // 创建各分类页面
    auto* appearancePage = new QWidget();
    createAppearancePage(appearancePage);

    auto* editorPage = new QWidget();
    createEditorPage(editorPage);

    auto* terminalPage = new QWidget();
    createTerminalPage(terminalPage);

    auto* completionPage = new QWidget();
    createCompletionPage(completionPage);

    auto* lspPage = new QWidget();
    createLspPage(lspPage);

    // P1 C05-2: 构建配置页
    auto* buildPage = new QWidget();
    createBuildPage(buildPage);

    // P3-M02 子项2: Markdown 自定义 CSS 配置页
    auto* markdownPage = new QWidget();
    createMarkdownPage(markdownPage);

    m_pageStack->addWidget(appearancePage);
    m_pageStack->addWidget(editorPage);
    m_pageStack->addWidget(terminalPage);
    m_pageStack->addWidget(completionPage);

    // 快捷键页面（独立创建，返回 QWidget 指针）
    // 注意：添加顺序必须与 m_categoryList 的项目顺序一致
    // 分类列表顺序：外观(0) 编辑器(1) 终端(2) 智能提示(3) 快捷键(4) LSP(5) 构建(6) Markdown(7)
    createShortcutsPage();

    m_pageStack->addWidget(lspPage);
    m_pageStack->addWidget(buildPage);
    m_pageStack->addWidget(markdownPage);

    // 用滚动区域包裹
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(m_pageStack);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setObjectName(QStringLiteral("settingsScrollArea"));

    mainLayout->addWidget(scrollArea, 1);

    // === 信号连接 ===
    connect(m_categoryList, &QListWidget::currentRowChanged,
            this, &SettingsPage::onCategoryChanged);
    connect(m_searchInput, &QLineEdit::textChanged,
            this, &SettingsPage::onSearchTextChanged);
    connect(m_btnResetSection, &QPushButton::clicked,
            this, &SettingsPage::onResetCurrentSection);
    connect(m_btnResetAll, &QPushButton::clicked,
            this, &SettingsPage::onResetAll);
    connect(m_btnExportConfig, &QPushButton::clicked,
            this, &SettingsPage::onExportConfig);
    connect(m_btnImportConfig, &QPushButton::clicked,
            this, &SettingsPage::onImportConfig);

    // P3-M05: 长文本提示标签启用自动换行，适配不同语言（英文偏长，中文偏短）
    // 遍历所有 settingsHint QLabel，统一开启 wordWrap，避免英文字符串溢出截断
    QList<QLabel*> hintLabels = findChildren<QLabel*>(QStringLiteral("settingsHint"));
    for (QLabel* lbl : hintLabels) {
        lbl->setWordWrap(true);
        lbl->setMinimumHeight(0);  // 允许布局自适应
    }
}

void SettingsPage::createAppearancePage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    // 标题
    auto* titleLabel = new QLabel(tr("外观"), page);
    titleLabel->setObjectName(QStringLiteral("settingsMainTitle"));
    layout->addWidget(titleLabel);

    auto* hintLabel = new QLabel(tr("自定义编辑器的外观和主题配色"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(hintLabel);

    // --- 主题配色 ---
    auto* sectionLabel = new QLabel(tr("主题配色"), page);
    sectionLabel->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(sectionLabel);

    auto* themeLayout = new QHBoxLayout();
    auto* themeLabel = new QLabel(tr("主题:"), page);
    themeLabel->setFixedWidth(120);
    m_themeCombo = new QComboBox(page);
    auto& tm = ThemeManager::instance();
    for (const auto& key : tm.themeKeys()) {
        m_themeCombo->addItem(tm.themeDisplayName(key), key);
    }
    themeLayout->addWidget(themeLabel);
    themeLayout->addWidget(m_themeCombo, 1);
    themeLayout->addStretch();
    layout->addLayout(themeLayout);

    auto* themeHint = new QLabel(tr("切换编辑器主题配色，支持亮色/暗色主题"), page);
    themeHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(themeHint);

    // --- 字体 ---
    auto* fontSection = new QLabel(tr("字体"), page);
    fontSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(fontSection);

    auto* fontSizeLayout = new QHBoxLayout();
    auto* fontSizeLabel = new QLabel(tr("字体大小:"), page);
    fontSizeLabel->setFixedWidth(120);
    m_fontSizeSpin = new QSpinBox(page);
    m_fontSizeSpin->setRange(8, 48);
    m_fontSizeSpin->setValue(14);
    m_fontSizeSpin->setSuffix(QStringLiteral(" px"));
    fontSizeLayout->addWidget(fontSizeLabel);
    fontSizeLayout->addWidget(m_fontSizeSpin);
    fontSizeLayout->addStretch();
    layout->addLayout(fontSizeLayout);

    auto* fontHint = new QLabel(tr("设置编辑器字体大小，范围 8-48px"), page);
    fontHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(fontHint);

    // --- 语言 ---
    auto* langSection = new QLabel(tr("语言"), page);
    langSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(langSection);

    auto* langLayout = new QHBoxLayout();
    auto* langLabel = new QLabel(tr("界面语言:"), page);
    langLabel->setFixedWidth(120);
    m_languageCombo = new QComboBox(page);
    m_languageCombo->addItem(tr("简体中文"), QStringLiteral("zh_CN"));
    m_languageCombo->addItem(tr("English"), QStringLiteral("en_US"));
    langLayout->addWidget(langLabel);
    langLayout->addWidget(m_languageCombo, 1);
    langLayout->addStretch();
    layout->addLayout(langLayout);

    auto* langHint = new QLabel(tr("切换界面语言，修改后需重启应用生效"), page);
    langHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(langHint);

    layout->addStretch();

    // 信号
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onThemeChanged);
    connect(m_fontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPage::onFontSizeChanged);
    connect(m_languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onLanguageChanged);
}

void SettingsPage::createEditorPage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(tr("编辑器"), page);
    titleLabel->setObjectName(QStringLiteral("settingsMainTitle"));
    layout->addWidget(titleLabel);

    auto* hintLabel = new QLabel(tr("配置编辑器的行为和显示选项"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(hintLabel);

    // --- 显示 ---
    auto* displaySection = new QLabel(tr("显示"), page);
    displaySection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(displaySection);

    m_lineNumbersCheck = new QCheckBox(tr("显示行号"), page);
    m_lineNumbersCheck->setChecked(true);
    layout->addWidget(m_lineNumbersCheck);

    auto* lineNumHint = new QLabel(tr("在编辑器左侧显示行号，方便定位代码"), page);
    lineNumHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(lineNumHint);

    // --- 缩进 ---
    auto* indentSection = new QLabel(tr("缩进"), page);
    indentSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(indentSection);

    auto* tabSizeLayout = new QHBoxLayout();
    auto* tabSizeLabel = new QLabel(tr("缩进大小:"), page);
    tabSizeLabel->setFixedWidth(120);
    m_tabSizeSpin = new QSpinBox(page);
    m_tabSizeSpin->setRange(2, 8);
    m_tabSizeSpin->setValue(4);
    m_tabSizeSpin->setSuffix(QStringLiteral(" 空格"));
    tabSizeLayout->addWidget(tabSizeLabel);
    tabSizeLayout->addWidget(m_tabSizeSpin);
    tabSizeLayout->addStretch();
    layout->addLayout(tabSizeLayout);

    auto* tabHint = new QLabel(tr("设置代码缩进的空格数量，默认 4"), page);
    tabHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(tabHint);

    // --- 缩进风格 (M4) ---
    auto* indentStyleSection = new QLabel(tr("缩进风格"), page);
    indentStyleSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(indentStyleSection);

    auto* indentStyleLayout = new QHBoxLayout();
    auto* indentStyleLabel = new QLabel(tr("缩进方式:"), page);
    indentStyleLabel->setFixedWidth(120);
    m_indentStyleCombo = new QComboBox(page);
    m_indentStyleCombo->addItem(tr("空格 (Spaces)"), QStringLiteral("spaces"));
    m_indentStyleCombo->addItem(tr("制表符 (Tabs)"), QStringLiteral("tabs"));
    indentStyleLayout->addWidget(indentStyleLabel);
    indentStyleLayout->addWidget(m_indentStyleCombo, 1);
    indentStyleLayout->addStretch();
    layout->addLayout(indentStyleLayout);

    // --- 格式化工具路径 (M4) ---
    auto* formatToolSection = new QLabel(tr("格式化工具"), page);
    formatToolSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(formatToolSection);

    auto* formatToolLayout = new QHBoxLayout();
    auto* formatToolLabel = new QLabel(tr("clang-format:"), page);
    formatToolLabel->setFixedWidth(120);
    m_formatToolPathLabel = new QLabel(tr("(自动检测)"), page);
    m_formatToolPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_formatToolPathBtn = new QPushButton(tr("浏览..."), page);
    m_formatToolPathBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_formatToolPathBtn->setFixedWidth(80);
    formatToolLayout->addWidget(formatToolLabel);
    formatToolLayout->addWidget(m_formatToolPathLabel, 1);
    formatToolLayout->addWidget(m_formatToolPathBtn);
    formatToolLayout->addStretch();
    layout->addLayout(formatToolLayout);

    auto* formatToolHint = new QLabel(tr("代码格式化工具路径，留空则自动检测系统 clang-format 或使用内置格式化"), page);
    formatToolHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(formatToolHint);

    // --- 保存 ---
    auto* saveSection = new QLabel(tr("保存"), page);
    saveSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(saveSection);

    m_autoSaveCheck = new QCheckBox(tr("启用自动保存（30秒）"), page);
    layout->addWidget(m_autoSaveCheck);

    auto* autoSaveHint = new QLabel(tr("每30秒自动保存已修改的文件，避免意外丢失"), page);
    autoSaveHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(autoSaveHint);

    // --- JSON 自动格式化 (M10) ---
    m_autoFormatJsonCheck = new QCheckBox(tr("保存 .json 文件时自动格式化"), page);
    layout->addWidget(m_autoFormatJsonCheck);

    auto* jsonFormatHint = new QLabel(tr("保存JSON文件前自动校验并美化输出"), page);
    jsonFormatHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(jsonFormatHint);

    // --- 拼写检查 (P3-M03 子项5) ---
    m_spellCheckCheck = new QCheckBox(tr("启用拼写检查"), page);
    layout->addWidget(m_spellCheckCheck);

    auto* spellHint = new QLabel(tr("对英文单词进行拼写检查，错误的单词以红色波浪线标注；右键可查看建议或加入词典"), page);
    spellHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(spellHint);

    layout->addStretch();

    // 信号
    connect(m_lineNumbersCheck, &QCheckBox::toggled,
            this, &SettingsPage::onLineNumbersToggled);
    connect(m_tabSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPage::onTabSizeChanged);
    connect(m_autoSaveCheck, &QCheckBox::toggled,
            this, &SettingsPage::onAutoSaveToggled);
    connect(m_indentStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onIndentStyleChanged);
    connect(m_formatToolPathBtn, &QPushButton::clicked,
            this, &SettingsPage::onFormatToolPathClicked);
    connect(m_autoFormatJsonCheck, &QCheckBox::toggled,
            this, &SettingsPage::onAutoFormatJsonToggled);
    connect(m_spellCheckCheck, &QCheckBox::toggled,
            this, &SettingsPage::onSpellCheckToggled);
}

void SettingsPage::createTerminalPage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(tr("终端"), page);
    titleLabel->setObjectName(QStringLiteral("settingsMainTitle"));
    layout->addWidget(titleLabel);

    auto* hintLabel = new QLabel(tr("配置内嵌终端的行为和外观"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(hintLabel);

    // --- 终端类型 ---
    auto* typeSection = new QLabel(tr("终端类型"), page);
    typeSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(typeSection);

    auto* termTypeLayout = new QHBoxLayout();
    auto* termTypeLabel = new QLabel(tr("默认终端:"), page);
    termTypeLabel->setFixedWidth(120);
    m_terminalTypeCombo = new QComboBox(page);
    m_terminalTypeCombo->addItem(tr("CMD"), QStringLiteral("cmd"));
    m_terminalTypeCombo->addItem(tr("PowerShell"), QStringLiteral("powershell"));
    termTypeLayout->addWidget(termTypeLabel);
    termTypeLayout->addWidget(m_terminalTypeCombo, 1);
    termTypeLayout->addStretch();
    layout->addLayout(termTypeLayout);

    auto* termTypeHint = new QLabel(tr("选择默认的终端类型，Windows下支持CMD和PowerShell"), page);
    termTypeHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(termTypeHint);

    // --- 终端字体 ---
    auto* fontSection = new QLabel(tr("字体"), page);
    fontSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(fontSection);

    auto* termFontLayout = new QHBoxLayout();
    auto* termFontLabel = new QLabel(tr("终端字号:"), page);
    termFontLabel->setFixedWidth(120);
    m_terminalFontSpin = new QSpinBox(page);
    m_terminalFontSpin->setRange(8, 32);
    m_terminalFontSpin->setValue(13);
    m_terminalFontSpin->setSuffix(QStringLiteral(" px"));
    termFontLayout->addWidget(termFontLabel);
    termFontLayout->addWidget(m_terminalFontSpin);
    termFontLayout->addStretch();
    layout->addLayout(termFontLayout);

    auto* termFontHint = new QLabel(tr("设置终端的字体大小"), page);
    termFontHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(termFontHint);

    // --- 字体选择 ---
    auto* fontFamilyLayout = new QHBoxLayout();
    auto* fontFamilyLabel = new QLabel(tr("终端字体:"), page);
    fontFamilyLabel->setFixedWidth(120);
    m_terminalFontFamilyCombo = new QComboBox(page);
    m_terminalFontFamilyCombo->addItem(tr("Consolas"), QStringLiteral("Consolas"));
    m_terminalFontFamilyCombo->addItem(tr("Cascadia Code"), QStringLiteral("Cascadia Code"));
    m_terminalFontFamilyCombo->addItem(tr("JetBrains Mono"), QStringLiteral("JetBrains Mono"));
    m_terminalFontFamilyCombo->addItem(tr("Lucida Console"), QStringLiteral("Lucida Console"));
    fontFamilyLayout->addWidget(fontFamilyLabel);
    fontFamilyLayout->addWidget(m_terminalFontFamilyCombo, 1);
    fontFamilyLayout->addStretch();
    layout->addLayout(fontFamilyLayout);

    auto* fontFamilyHint = new QLabel(tr("选择终端使用的等宽字体"), page);
    fontFamilyHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(fontFamilyHint);

    // --- 颜色配置 ---
    auto* colorSection = new QLabel(tr("颜色配置"), page);
    colorSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(colorSection);

    // 前景色
    auto* fgColorLayout = new QHBoxLayout();
    auto* fgColorLabel = new QLabel(tr("前景色:"), page);
    fgColorLabel->setFixedWidth(120);
    m_terminalFgColorBtn = new QPushButton(page);
    m_terminalFgColorBtn->setFixedSize(32, 24);
    m_terminalFgColorBtn->setObjectName(QStringLiteral("colorPreviewBtn"));
    m_terminalFgColorLabel = new QLabel(tr("#cccccc"), page);
    fgColorLayout->addWidget(fgColorLabel);
    fgColorLayout->addWidget(m_terminalFgColorBtn);
    fgColorLayout->addWidget(m_terminalFgColorLabel);
    fgColorLayout->addStretch();
    layout->addLayout(fgColorLayout);

    auto* fgColorHint = new QLabel(tr("终端文字颜色（默认 #cccccc）"), page);
    fgColorHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(fgColorHint);

    // 背景色
    auto* bgColorLayout = new QHBoxLayout();
    auto* bgColorLabel = new QLabel(tr("背景色:"), page);
    bgColorLabel->setFixedWidth(120);
    m_terminalBgColorBtn = new QPushButton(page);
    m_terminalBgColorBtn->setFixedSize(32, 24);
    m_terminalBgColorBtn->setObjectName(QStringLiteral("colorPreviewBtn"));
    m_terminalBgColorLabel = new QLabel(tr("#1e1e1e"), page);
    bgColorLayout->addWidget(bgColorLabel);
    bgColorLayout->addWidget(m_terminalBgColorBtn);
    bgColorLayout->addWidget(m_terminalBgColorLabel);
    bgColorLayout->addStretch();
    layout->addLayout(bgColorLayout);

    auto* bgColorHint = new QLabel(tr("终端背景颜色（默认 #1e1e1e）"), page);
    bgColorHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(bgColorHint);

    // 光标色
    auto* cursorColorLayout = new QHBoxLayout();
    auto* cursorColorLabel_title = new QLabel(tr("光标颜色:"), page);
    cursorColorLabel_title->setFixedWidth(120);
    m_terminalCursorColorBtn = new QPushButton(page);
    m_terminalCursorColorBtn->setFixedSize(32, 24);
    m_terminalCursorColorBtn->setObjectName(QStringLiteral("colorPreviewBtn"));
    m_terminalCursorColorLabel = new QLabel(tr("#ffffff"), page);
    cursorColorLayout->addWidget(cursorColorLabel_title);
    cursorColorLayout->addWidget(m_terminalCursorColorBtn);
    cursorColorLayout->addWidget(m_terminalCursorColorLabel);
    cursorColorLayout->addStretch();
    layout->addLayout(cursorColorLayout);

    auto* cursorColorHint = new QLabel(tr("光标显示颜色（默认 #ffffff）"), page);
    cursorColorHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(cursorColorHint);

    // --- 光标样式 ---
    auto* cursorStyleSection = new QLabel(tr("光标样式"), page);
    cursorStyleSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(cursorStyleSection);

    auto* cursorShapeLayout = new QHBoxLayout();
    auto* cursorShapeLabel = new QLabel(tr("光标形状:"), page);
    cursorShapeLabel->setFixedWidth(120);
    m_terminalCursorShapeCombo = new QComboBox(page);
    m_terminalCursorShapeCombo->addItem(tr("方块 (Block)"), QStringLiteral("block"));
    m_terminalCursorShapeCombo->addItem(tr("下划线 (Underline)"), QStringLiteral("underline"));
    m_terminalCursorShapeCombo->addItem(tr("竖线 (Vertical Bar)"), QStringLiteral("ibeam"));
    cursorShapeLayout->addWidget(cursorShapeLabel);
    cursorShapeLayout->addWidget(m_terminalCursorShapeCombo, 1);
    cursorShapeLayout->addStretch();
    layout->addLayout(cursorShapeLayout);

    auto* cursorBlinkLayout = new QHBoxLayout();
    auto* cursorBlinkLabel = new QLabel(tr("光标闪烁:"), page);
    cursorBlinkLabel->setFixedWidth(120);
    m_terminalCursorBlinkCheck = new QCheckBox(tr("启用光标闪烁"), page);
    m_terminalCursorBlinkCheck->setChecked(true);
    cursorBlinkLayout->addWidget(cursorBlinkLabel);
    cursorBlinkLayout->addWidget(m_terminalCursorBlinkCheck);
    cursorBlinkLayout->addStretch();
    layout->addLayout(cursorBlinkLayout);

    auto* cursorStyleHint = new QLabel(tr("自定义终端光标的形状和动画效果"), page);
    cursorStyleHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(cursorStyleHint);

    layout->addStretch();

    // 信号
    connect(m_terminalTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onTerminalTypeChanged);
    connect(m_terminalFontSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPage::onTerminalFontChanged);
    connect(m_terminalFontFamilyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onTerminalFontFamilyChanged);
    connect(m_terminalFgColorBtn, &QPushButton::clicked,
            this, &SettingsPage::onTerminalFgColorClicked);
    connect(m_terminalBgColorBtn, &QPushButton::clicked,
            this, &SettingsPage::onTerminalBgColorClicked);
    connect(m_terminalCursorColorBtn, &QPushButton::clicked,
            this, &SettingsPage::onTerminalCursorColorClicked);
    connect(m_terminalCursorShapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onTerminalCursorShapeChanged);
    connect(m_terminalCursorBlinkCheck, &QCheckBox::toggled,
            this, &SettingsPage::onTerminalCursorBlinkToggled);
}

void SettingsPage::createCompletionPage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(tr("智能提示"), page);
    titleLabel->setObjectName(QStringLiteral("settingsMainTitle"));
    layout->addWidget(titleLabel);

    auto* hintLabel = new QLabel(tr("配置代码补全和智能提示的行为"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(hintLabel);

    // --- 基本设置 ---
    auto* basicSection = new QLabel(tr("基本设置"), page);
    basicSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(basicSection);

    m_completionCheck = new QCheckBox(tr("启用智能补全"), page);
    m_completionCheck->setChecked(true);
    layout->addWidget(m_completionCheck);

    auto* compHint = new QLabel(tr("输入时自动弹出代码补全建议列表"), page);
    compHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(compHint);

    // --- 触发设置 ---
    auto* triggerSection = new QLabel(tr("触发设置"), page);
    triggerSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(triggerSection);

    auto* delayLayout = new QHBoxLayout();
    auto* delayLabel = new QLabel(tr("提示延迟:"), page);
    delayLabel->setFixedWidth(120);
    m_completionDelaySpin = new QSpinBox(page);
    m_completionDelaySpin->setRange(100, 2000);
    m_completionDelaySpin->setValue(500);
    m_completionDelaySpin->setSuffix(QStringLiteral(" ms"));
    m_completionDelaySpin->setSingleStep(100);
    delayLayout->addWidget(delayLabel);
    delayLayout->addWidget(m_completionDelaySpin);
    delayLayout->addStretch();
    layout->addLayout(delayLayout);

    auto* delayHint = new QLabel(tr("输入后等待多久弹出提示，默认 500ms"), page);
    delayHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(delayHint);

    auto* prefixLayout = new QHBoxLayout();
    auto* prefixLabel = new QLabel(tr("最小前缀:"), page);
    prefixLabel->setFixedWidth(120);
    m_minPrefixSpin = new QSpinBox(page);
    m_minPrefixSpin->setRange(1, 5);
    m_minPrefixSpin->setValue(2);
    prefixLayout->addWidget(prefixLabel);
    prefixLayout->addWidget(m_minPrefixSpin);
    prefixLayout->addStretch();
    layout->addLayout(prefixLayout);

    auto* prefixHint = new QLabel(tr("输入几个字符后开始触发提示，默认 2"), page);
    prefixHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(prefixHint);

    // --- 匹配模式 ---
    auto* matchSection = new QLabel(tr("匹配模式"), page);
    matchSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(matchSection);

    auto* matchLayout = new QHBoxLayout();
    auto* matchLabel = new QLabel(tr("匹配模式:"), page);
    matchLabel->setFixedWidth(120);
    m_matchingModeCombo = new QComboBox(page);
    m_matchingModeCombo->addItem(tr("模糊匹配"), QStringLiteral("fuzzy"));
    m_matchingModeCombo->addItem(tr("子串匹配"), QStringLiteral("substr"));
    m_matchingModeCombo->addItem(tr("前缀匹配"), QStringLiteral("prefix"));
    matchLayout->addWidget(matchLabel);
    matchLayout->addWidget(m_matchingModeCombo, 1);
    matchLayout->addStretch();
    layout->addLayout(matchLayout);

    auto* matchHint = new QLabel(tr("模糊匹配：输入字符序列匹配（如\"ptn\"匹配\"println\"）\n子串匹配：包含输入文本即匹配\n前缀匹配：仅匹配开头文本"), page);
    matchHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(matchHint);

    // --- 代码片段（P2-H02 子项1：入口按钮）---
    auto* snippetSection = new QLabel(tr("代码片段"), page);
    snippetSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(snippetSection);

    auto* snippetLayout = new QHBoxLayout();
    auto* snippetHint = new QLabel(tr("管理常用代码模板，支持占位符与 VSCode 格式导入导出"), page);
    snippetHint->setObjectName(QStringLiteral("settingsHint"));
    snippetHint->setWordWrap(true);
    snippetLayout->addWidget(snippetHint, 1);
    auto* btnSnippetManage = new QPushButton(tr("管理代码片段..."), page);
    btnSnippetManage->setObjectName(QStringLiteral("btnSnippetManage"));
    snippetLayout->addWidget(btnSnippetManage);
    layout->addLayout(snippetLayout);

    // 点击弹出片段管理对话框
    connect(btnSnippetManage, &QPushButton::clicked, page, [page]() {
        SnippetManagerDialog dlg(page);
        dlg.exec();
    });

    layout->addStretch();

    // 信号
    connect(m_completionCheck, &QCheckBox::toggled,
            this, &SettingsPage::onCompletionToggled);
    connect(m_completionDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPage::onCompletionDelayChanged);
    connect(m_minPrefixSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPage::onMinPrefixChanged);
    connect(m_matchingModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onMatchingModeChanged);
}

void SettingsPage::createLspPage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(tr("LSP 语言服务器"), page);
    titleLabel->setObjectName(QStringLiteral("settingsMainTitle"));
    layout->addWidget(titleLabel);

    auto* hintLabel = new QLabel(tr("配置 Language Server Protocol (LSP) 语言服务器，提供代码补全、跳转定义、诊断等功能"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(hintLabel);

    // --- Python 语言服务器 ---
    auto* pySection = new QLabel(tr("Python 语言服务器 (pylsp)"), page);
    pySection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(pySection);

    auto* pyLayout = new QHBoxLayout();
    auto* pyLabel = new QLabel(tr("pylsp 路径:"), page);
    pyLabel->setFixedWidth(120);
    m_lspPythonPathLabel = new QLabel(tr("(自动检测 pylsp)"), page);
    m_lspPythonPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_lspPythonPathBtn = new QPushButton(tr("浏览..."), page);
    m_lspPythonPathBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_lspPythonPathBtn->setFixedWidth(80);
    pyLayout->addWidget(pyLabel);
    pyLayout->addWidget(m_lspPythonPathLabel, 1);
    pyLayout->addWidget(m_lspPythonPathBtn);
    pyLayout->addStretch();
    layout->addLayout(pyLayout);

    auto* pyHint = new QLabel(tr("Python LSP 服务器路径，留空则自动检测系统中的 pylsp 或 python-lsp-server"), page);
    pyHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(pyHint);

    // --- C++ 语言服务器 ---
    auto* cppSection = new QLabel(tr("C++ 语言服务器 (clangd)"), page);
    cppSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(cppSection);

    auto* cppLayout = new QHBoxLayout();
    auto* cppLabel = new QLabel(tr("clangd 路径:"), page);
    cppLabel->setFixedWidth(120);
    m_lspCppPathLabel = new QLabel(tr("(自动检测 clangd)"), page);
    m_lspCppPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_lspCppPathBtn = new QPushButton(tr("浏览..."), page);
    m_lspCppPathBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_lspCppPathBtn->setFixedWidth(80);
    cppLayout->addWidget(cppLabel);
    cppLayout->addWidget(m_lspCppPathLabel, 1);
    cppLayout->addWidget(m_lspCppPathBtn);
    cppLayout->addStretch();
    layout->addLayout(cppLayout);

    auto* cppHint = new QLabel(tr("C++ LSP 服务器路径，留空则自动检测系统中的 clangd"), page);
    cppHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(cppHint);

    // --- JavaScript 语言服务器 ---
    auto* jsSection = new QLabel(tr("JavaScript/TypeScript 语言服务器"), page);
    jsSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(jsSection);

    auto* jsLayout = new QHBoxLayout();
    auto* jsLabel = new QLabel(tr("typescript-language-server 路径:"), page);
    jsLabel->setFixedWidth(120);
    m_lspJsPathLabel = new QLabel(tr("(自动检测)"), page);
    m_lspJsPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_lspJsPathBtn = new QPushButton(tr("浏览..."), page);
    m_lspJsPathBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_lspJsPathBtn->setFixedWidth(80);
    jsLayout->addWidget(jsLabel);
    jsLayout->addWidget(m_lspJsPathLabel, 1);
    jsLayout->addWidget(m_lspJsPathBtn);
    jsLayout->addStretch();
    layout->addLayout(jsLayout);

    auto* jsHint = new QLabel(tr("JavaScript/TypeScript LSP 服务器路径，留空则自动检测 typescript-language-server"), page);
    jsHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(jsHint);

    // --- 自动启动 ---
    auto* autoStartSection = new QLabel(tr("通用设置"), page);
    autoStartSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(autoStartSection);

    m_lspAutoStartCheck = new QCheckBox(tr("打开文件时自动启动对应语言服务器"), page);
    layout->addWidget(m_lspAutoStartCheck);

    auto* autoStartHint = new QLabel(tr("打开 .py/.cpp/.js 等文件时自动启动对应的语言服务器进程"), page);
    autoStartHint->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(autoStartHint);

    layout->addStretch();

    // 信号
    connect(m_lspPythonPathBtn, &QPushButton::clicked, this, &SettingsPage::onLspPythonPathClicked);
    connect(m_lspCppPathBtn, &QPushButton::clicked, this, &SettingsPage::onLspCppPathClicked);
    connect(m_lspJsPathBtn, &QPushButton::clicked, this, &SettingsPage::onLspJsPathClicked);
    connect(m_lspAutoStartCheck, &QCheckBox::toggled, this, &SettingsPage::onLspAutoStartToggled);
}

// ============================================================
// 快捷键捕获对话框（内部辅助类）
// ============================================================

/// @brief 快捷键捕获对话框 - 监听按键并转换为可读字符串
class ShortcutCaptureDialog : public QDialog
{
public:
    explicit ShortcutCaptureDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("修改快捷键"));
        setFixedSize(400, 150);
        setModal(true);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 20, 20, 20);

        auto* hintLabel = new QLabel(tr("按下新的快捷键组合..."), this);
        hintLabel->setObjectName(QStringLiteral("settingsSectionTitle"));
        hintLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(hintLabel);

        m_keyLabel = new QLabel(tr("等待输入..."), this);
        m_keyLabel->setObjectName(QStringLiteral("settingsHint"));
        m_keyLabel->setAlignment(Qt::AlignCenter);
        m_keyLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold; padding: 10px;"));
        layout->addWidget(m_keyLabel);

        auto* escLabel = new QLabel(tr("按 Esc 取消"), this);
        escLabel->setObjectName(QStringLiteral("settingsHint"));
        escLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(escLabel);

        layout->addStretch();

        // 样式跟随主题（适配亮/暗模式）
        const auto& p = ThemeManager::instance().currentPalette();
        setStyleSheet(
            QStringLiteral("QDialog { background-color: %1; color: %2; }")
                .arg(p.bgDialog.name(QColor::HexRgb))
                .arg(p.fgPrimary.name(QColor::HexRgb))
            + QStringLiteral("QLabel { color: %1; border: none; }")
                .arg(p.fgPrimary.name(QColor::HexRgb))
        );
    }

    QString capturedSequence() const { return m_capturedSeq; }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            reject();
            return;
        }
        if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift ||
            event->key() == Qt::Key_Alt || event->key() == Qt::Key_Meta) {
            return; // 纯修饰键不处理
        }

        // 转换为可读字符串
        QString seq = keyEventToString(event);
        m_keyLabel->setText(seq);
        m_capturedSeq = seq;
        accept();
    }

private:
    static QString keyEventToString(QKeyEvent* event)
    {
        QStringList parts;

        if (event->modifiers() & Qt::ControlModifier) parts << QStringLiteral("Ctrl");
        if (event->modifiers() & Qt::ShiftModifier)   parts << QStringLiteral("Shift");
        if (event->modifiers() & Qt::AltModifier)     parts << QStringLiteral("Alt");
        if (event->modifiers() & Qt::MetaModifier)    parts << QStringLiteral("Meta");

        int key = event->key();
        QString keyName;
        if (key >= Qt::Key_A && key <= Qt::Key_Z) {
            keyName = QStringLiteral("A");
            keyName[0] = QLatin1Char('A' + (key - Qt::Key_A));
        } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
            keyName = QString::number(key - Qt::Key_0);
        } else {
            switch (key) {
            case Qt::Key_F1: case Qt::Key_F2: case Qt::Key_F3: case Qt::Key_F4:
            case Qt::Key_F5: case Qt::Key_F6: case Qt::Key_F7: case Qt::Key_F8:
            case Qt::Key_F9: case Qt::Key_F10: case Qt::Key_F11: case Qt::Key_F12:
                keyName = QStringLiteral("F") + QString::number(key - Qt::Key_F1 + 1);
                break;
            case Qt::Key_Backspace:   keyName = QStringLiteral("Backspace"); break;
            case Qt::Key_Tab:         keyName = QStringLiteral("Tab"); break;
            case Qt::Key_Return:      [[fallthrough]];
            case Qt::Key_Enter:       keyName = QStringLiteral("Enter"); break;
            case Qt::Key_Escape:      keyName = QStringLiteral("Esc"); break;
            case Qt::Key_Space:       keyName = QStringLiteral("Space"); break;
            case Qt::Key_PageUp:      keyName = QStringLiteral("PageUp"); break;
            case Qt::Key_PageDown:    keyName = QStringLiteral("PageDown"); break;
            case Qt::Key_End:         keyName = QStringLiteral("End"); break;
            case Qt::Key_Home:        keyName = QStringLiteral("Home"); break;
            case Qt::Key_Left:        keyName = QStringLiteral("Left"); break;
            case Qt::Key_Up:          keyName = QStringLiteral("Up"); break;
            case Qt::Key_Right:       keyName = QStringLiteral("Right"); break;
            case Qt::Key_Down:        keyName = QStringLiteral("Down"); break;
            case Qt::Key_Insert:      keyName = QStringLiteral("Insert"); break;
            case Qt::Key_Delete:      keyName = QStringLiteral("Delete"); break;
            case Qt::Key_Semicolon:   keyName = QStringLiteral(";"); break;
            case Qt::Key_Period:      keyName = QStringLiteral("."); break;
            case Qt::Key_Comma:       keyName = QStringLiteral(","); break;
            case Qt::Key_QuoteDbl:    keyName = QStringLiteral("\""); break;
            case Qt::Key_QuoteLeft:   keyName = QStringLiteral("`"); break;
            case Qt::Key_BracketLeft: keyName = QStringLiteral("["); break;
            case Qt::Key_BracketRight:keyName = QStringLiteral("]"); break;
            case Qt::Key_BraceLeft:   keyName = QStringLiteral("{"); break;
            case Qt::Key_BraceRight:  keyName = QStringLiteral("}"); break;
            case Qt::Key_Backslash:   keyName = QStringLiteral("\\"); break;
            case Qt::Key_Minus:       keyName = QStringLiteral("-"); break;
            case Qt::Key_Equal:       keyName = QStringLiteral("="); break;
            case Qt::Key_Plus:        keyName = QStringLiteral("+"); break;
            default:
                keyName = event->text().toUpper();
                if (keyName.isEmpty()) keyName = QString::number(key);
                break;
            }
        }

        parts << keyName;
        return parts.join(QLatin1Char('+'));
    }

    QLabel* m_keyLabel;
    QString m_capturedSeq;
};

// ============================================================
// P1 C05-2: 构建设置页面创建
// ============================================================

void SettingsPage::createBuildPage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(tr("构建配置"), page);
    titleLabel->setObjectName(QStringLiteral("settingsMainTitle"));
    layout->addWidget(titleLabel);

    auto* hintLabel = new QLabel(tr("配置 CMake 构建所需的 Qt/OpenSSL/zlib 路径与构建类型。这些路径对应 CMakeLists.txt 中的 CMAKE_PREFIX_PATH / OPENSSL_ROOT_DIR / ZLIB_ROOT 环境变量"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    // --- Qt 安装路径 ---
    auto* qtSection = new QLabel(tr("Qt 安装路径"), page);
    qtSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(qtSection);

    auto* qtLayout = new QHBoxLayout();
    auto* qtLabel = new QLabel(tr("Qt 路径:"), page);
    qtLabel->setFixedWidth(120);
    m_buildQtPathLabel = new QLabel(tr("(使用环境变量 CMAKE_PREFIX_PATH)"), page);
    m_buildQtPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_buildQtPathLabel->setWordWrap(true);
    m_buildQtPathBtn = new QPushButton(tr("浏览..."), page);
    m_buildQtPathBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_buildQtPathBtn->setFixedWidth(80);
    m_buildAutoDetectBtn = new QPushButton(tr("自动检测"), page);  // C05-3
    m_buildAutoDetectBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_buildAutoDetectBtn->setFixedWidth(80);
    qtLayout->addWidget(qtLabel);
    qtLayout->addWidget(m_buildQtPathLabel, 1);
    qtLayout->addWidget(m_buildQtPathBtn);
    qtLayout->addWidget(m_buildAutoDetectBtn);
    qtLayout->addStretch();
    layout->addLayout(qtLayout);

    auto* qtHint = new QLabel(tr("Qt 安装根目录（如 .../6.5.3/mingw_64），对应 CMAKE_PREFIX_PATH。留空则使用环境变量"), page);
    qtHint->setObjectName(QStringLiteral("settingsHint"));
    qtHint->setWordWrap(true);
    layout->addWidget(qtHint);

    // --- OpenSSL 路径 ---
    auto* sslSection = new QLabel(tr("OpenSSL 路径"), page);
    sslSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(sslSection);

    auto* sslLayout = new QHBoxLayout();
    auto* sslLabel = new QLabel(tr("OpenSSL 路径:"), page);
    sslLabel->setFixedWidth(120);
    m_buildOpenSslPathLabel = new QLabel(tr("(使用环境变量 OPENSSL_ROOT_DIR)"), page);
    m_buildOpenSslPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_buildOpenSslPathLabel->setWordWrap(true);
    m_buildOpenSslPathBtn = new QPushButton(tr("浏览..."), page);
    m_buildOpenSslPathBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_buildOpenSslPathBtn->setFixedWidth(80);
    sslLayout->addWidget(sslLabel);
    sslLayout->addWidget(m_buildOpenSslPathLabel, 1);
    sslLayout->addWidget(m_buildOpenSslPathBtn);
    sslLayout->addStretch();
    layout->addLayout(sslLayout);

    auto* sslHint = new QLabel(tr("OpenSSL 安装根目录，对应 OPENSSL_ROOT_DIR。留空则使用环境变量"), page);
    sslHint->setObjectName(QStringLiteral("settingsHint"));
    sslHint->setWordWrap(true);
    layout->addWidget(sslHint);

    // --- zlib 路径 ---
    auto* zlibSection = new QLabel(tr("zlib 路径"), page);
    zlibSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(zlibSection);

    auto* zlibLayout = new QHBoxLayout();
    auto* zlibLabel = new QLabel(tr("zlib 路径:"), page);
    zlibLabel->setFixedWidth(120);
    m_buildZlibPathLabel = new QLabel(tr("(使用环境变量 ZLIB_ROOT)"), page);
    m_buildZlibPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_buildZlibPathLabel->setWordWrap(true);
    m_buildZlibPathBtn = new QPushButton(tr("浏览..."), page);
    m_buildZlibPathBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_buildZlibPathBtn->setFixedWidth(80);
    zlibLayout->addWidget(zlibLabel);
    zlibLayout->addWidget(m_buildZlibPathLabel, 1);
    zlibLayout->addWidget(m_buildZlibPathBtn);
    zlibLayout->addStretch();
    layout->addLayout(zlibLayout);

    auto* zlibHint = new QLabel(tr("zlib 安装根目录（含 lib/libz.a），对应 ZLIB_ROOT。留空则使用环境变量"), page);
    zlibHint->setObjectName(QStringLiteral("settingsHint"));
    zlibHint->setWordWrap(true);
    layout->addWidget(zlibHint);

    // --- 构建类型 ---
    auto* typeSection = new QLabel(tr("构建类型"), page);
    typeSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(typeSection);

    auto* typeLayout = new QHBoxLayout();
    auto* typeLabel = new QLabel(tr("构建类型:"), page);
    typeLabel->setFixedWidth(120);
    m_buildTypeCombo = new QComboBox(page);
    m_buildTypeCombo->addItem(tr("Debug"), QStringLiteral("Debug"));
    m_buildTypeCombo->addItem(tr("Release"), QStringLiteral("Release"));
    m_buildTypeCombo->addItem(tr("RelWithDebInfo"), QStringLiteral("RelWithDebInfo"));
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_buildTypeCombo, 1);
    typeLayout->addStretch();
    layout->addLayout(typeLayout);

    auto* typeHint = new QLabel(tr("对应 CMAKE_BUILD_TYPE，单配置生成器（如 MinGW Makefiles）使用；多配置生成器（如 Ninja Multi-Config）忽略"), page);
    typeHint->setObjectName(QStringLiteral("settingsHint"));
    typeHint->setWordWrap(true);
    layout->addWidget(typeHint);

    // --- 构建目录 (C05-4) ---
    auto* dirSection = new QLabel(tr("构建目录"), page);
    dirSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(dirSection);

    auto* dirLayout = new QHBoxLayout();
    auto* dirLabel = new QLabel(tr("构建目录:"), page);
    dirLabel->setFixedWidth(120);
    m_buildDirLabel = new QLabel(tr("(默认 build)"), page);
    m_buildDirLabel->setObjectName(QStringLiteral("settingsHint"));
    m_buildDirLabel->setWordWrap(true);
    m_buildDirBtn = new QPushButton(tr("浏览..."), page);
    m_buildDirBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_buildDirBtn->setFixedWidth(80);
    dirLayout->addWidget(dirLabel);
    dirLayout->addWidget(m_buildDirLabel, 1);
    dirLayout->addWidget(m_buildDirBtn);
    dirLayout->addStretch();
    layout->addLayout(dirLayout);

    auto* dirHint = new QLabel(tr("CMake 构建输出目录。留空则使用项目下的 build 目录"), page);
    dirHint->setObjectName(QStringLiteral("settingsHint"));
    dirHint->setWordWrap(true);
    layout->addWidget(dirHint);

    m_buildSeparateDirsCheck = new QCheckBox(tr("按构建类型分离目录（build/Debug、build/Release 各自独立）"), page);
    layout->addWidget(m_buildSeparateDirsCheck);

    auto* separateHint = new QLabel(tr("勾选后每种构建类型使用独立子目录，避免 Debug/Release 切换时反复 reconfigure"), page);
    separateHint->setObjectName(QStringLiteral("settingsHint"));
    separateHint->setWordWrap(true);
    layout->addWidget(separateHint);

    // --- 编译器路径 (P1 C05-2) ---
    auto* compilerSection = new QLabel(tr("编译器"), page);
    compilerSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(compilerSection);

    auto* compilerTypeLayout = new QHBoxLayout();
    auto* compilerTypeLabel = new QLabel(tr("编译器类型:"), page);
    compilerTypeLabel->setFixedWidth(120);
    m_buildCompilerCombo = new QComboBox(page);
    m_buildCompilerCombo->addItem(tr("MinGW (GCC)"), QStringLiteral("mingw"));
    m_buildCompilerCombo->addItem(tr("MSVC"), QStringLiteral("msvc"));
    compilerTypeLayout->addWidget(compilerTypeLabel);
    compilerTypeLayout->addWidget(m_buildCompilerCombo, 1);
    compilerTypeLayout->addStretch();
    layout->addLayout(compilerTypeLayout);

    auto* compilerPathLayout = new QHBoxLayout();
    auto* compilerPathLabel = new QLabel(tr("编译器路径:"), page);
    compilerPathLabel->setFixedWidth(120);
    m_buildCompilerPathLabel = new QLabel(tr("(使用系统默认)"), page);
    m_buildCompilerPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_buildCompilerPathLabel->setWordWrap(true);
    m_buildCompilerPathBtn = new QPushButton(tr("浏览..."), page);
    m_buildCompilerPathBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_buildCompilerPathBtn->setFixedWidth(80);
    compilerPathLayout->addWidget(compilerPathLabel);
    compilerPathLayout->addWidget(m_buildCompilerPathLabel, 1);
    compilerPathLayout->addWidget(m_buildCompilerPathBtn);
    compilerPathLayout->addStretch();
    layout->addLayout(compilerPathLayout);

    auto* compilerHint = new QLabel(tr("MinGW/MSVC 编译器路径，留空则使用系统默认编译器"), page);
    compilerHint->setObjectName(QStringLiteral("settingsHint"));
    compilerHint->setWordWrap(true);
    layout->addWidget(compilerHint);

    // --- CMake 额外参数 (P1 C05-2) ---
    auto* cmakeArgsSection = new QLabel(tr("CMake 额外参数"), page);
    cmakeArgsSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(cmakeArgsSection);

    auto* cmakeArgsLayout = new QHBoxLayout();
    auto* cmakeArgsLabel = new QLabel(tr("额外参数:"), page);
    cmakeArgsLabel->setFixedWidth(120);
    m_buildCmakeArgsEdit = new QLineEdit(page);
    m_buildCmakeArgsEdit->setPlaceholderText(tr("如 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"));
    cmakeArgsLayout->addWidget(cmakeArgsLabel);
    cmakeArgsLayout->addWidget(m_buildCmakeArgsEdit, 1);
    layout->addLayout(cmakeArgsLayout);

    auto* cmakeArgsHint = new QLabel(tr("传递给 CMake 的额外参数，多个参数用空格分隔"), page);
    cmakeArgsHint->setObjectName(QStringLiteral("settingsHint"));
    cmakeArgsHint->setWordWrap(true);
    layout->addWidget(cmakeArgsHint);

    // --- 应用并重新配置 (P1 C05-2) ---
    auto* applyLayout = new QHBoxLayout();
    m_buildApplyBtn = new QPushButton(tr("应用并重新配置"), page);
    m_buildApplyBtn->setObjectName(QStringLiteral("btnResetSection"));
    applyLayout->addWidget(m_buildApplyBtn);
    applyLayout->addStretch();
    layout->addLayout(applyLayout);

    auto* applyHint = new QLabel(tr("写入配置文件并提示重启生效。重新配置需在终端手动执行 cmake 命令"), page);
    applyHint->setObjectName(QStringLiteral("settingsHint"));
    applyHint->setWordWrap(true);
    layout->addWidget(applyHint);

    layout->addStretch();

    // 信号
    connect(m_buildQtPathBtn, &QPushButton::clicked, this, &SettingsPage::onBuildQtPathClicked);
    connect(m_buildAutoDetectBtn, &QPushButton::clicked, this, &SettingsPage::onBuildAutoDetectQt);  // C05-3
    connect(m_buildOpenSslPathBtn, &QPushButton::clicked, this, &SettingsPage::onBuildOpenSslPathClicked);
    connect(m_buildZlibPathBtn, &QPushButton::clicked, this, &SettingsPage::onBuildZlibPathClicked);
    connect(m_buildTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onBuildTypeChanged);
    connect(m_buildDirBtn, &QPushButton::clicked, this, &SettingsPage::onBuildDirClicked);  // C05-4
    connect(m_buildSeparateDirsCheck, &QCheckBox::toggled, this, &SettingsPage::onBuildSeparateDirsToggled);  // C05-4
    connect(m_buildCompilerPathBtn, &QPushButton::clicked, this, &SettingsPage::onBuildCompilerPathClicked);
    connect(m_buildApplyBtn, &QPushButton::clicked, this, &SettingsPage::onBuildApplyReconfigure);
}

// ============================================================
// P3-M02 子项2: Markdown 自定义 CSS 配置页
// ============================================================

void SettingsPage::createMarkdownPage(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(tr("Markdown 预览样式"), page);
    titleLabel->setObjectName(QStringLiteral("settingsMainTitle"));
    layout->addWidget(titleLabel);

    auto* hintLabel = new QLabel(tr(
        "自定义 Markdown 预览区的 CSS 样式表。用户 CSS 会叠加在主题预设（暗色/浅色）之上，"
        "优先级更高。可用于调整字体、颜色、间距、代码块样式等。"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    // --- CSS 编辑区 ---
    auto* cssSection = new QLabel(tr("自定义 CSS"), page);
    cssSection->setObjectName(QStringLiteral("settingsSectionTitle"));
    layout->addWidget(cssSection);

    m_mdCssEdit = new QPlainTextEdit(page);
    m_mdCssEdit->setObjectName(QStringLiteral("mdCssEdit"));
    m_mdCssEdit->setPlaceholderText(tr(
        "在此输入自定义 CSS，例如：\n"
        "body { font-size: 16px; }\n"
        "pre { background-color: #f5f5f5; }\n"
        "h1 { color: #ff6600; }"));
    // 等宽字体，便于编辑 CSS
    QFont monoFont(QStringLiteral("Consolas"), 10);
    monoFont.setStyleHint(QFont::Monospace);
    m_mdCssEdit->setFont(monoFont);
    // 行高自适应，最小可视区域
    m_mdCssEdit->setMinimumHeight(240);
    layout->addWidget(m_mdCssEdit, 1);

    auto* cssHint = new QLabel(tr(
        "提示：留空则仅使用主题预设 CSS（暗色/浅色自动切换）。"
        "修改后点击「应用」即时生效，所有打开的 Markdown 文档预览会自动刷新。"), page);
    cssHint->setObjectName(QStringLiteral("settingsHint"));
    cssHint->setWordWrap(true);
    layout->addWidget(cssHint);

    // --- 按钮区 ---
    auto* btnLayout = new QHBoxLayout();
    m_mdCssApplyBtn = new QPushButton(tr("应用"), page);
    m_mdCssApplyBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_mdCssApplyBtn->setToolTip(tr("保存 CSS 并刷新所有 Markdown 预览"));
    m_mdCssImportBtn = new QPushButton(tr("从文件导入..."), page);
    m_mdCssImportBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_mdCssImportBtn->setToolTip(tr("从 .css 文件导入样式表到编辑区"));
    m_mdCssResetBtn = new QPushButton(tr("重置"), page);
    m_mdCssResetBtn->setObjectName(QStringLiteral("btnResetSection"));
    m_mdCssResetBtn->setToolTip(tr("清空用户 CSS，恢复使用主题预设"));
    btnLayout->addWidget(m_mdCssApplyBtn);
    btnLayout->addWidget(m_mdCssImportBtn);
    btnLayout->addWidget(m_mdCssResetBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    layout->addStretch();

    // 信号连接
    connect(m_mdCssApplyBtn, &QPushButton::clicked, this, &SettingsPage::onMdCssApplyClicked);
    connect(m_mdCssImportBtn, &QPushButton::clicked, this, &SettingsPage::onMdCssImportClicked);
    connect(m_mdCssResetBtn, &QPushButton::clicked, this, &SettingsPage::onMdCssResetClicked);
}

// ============================================================
// P3-M02 子项2: Markdown CSS 槽函数
// ============================================================

void SettingsPage::onMdCssApplyClicked()
{
    if (!m_mdCssEdit) return;
    QString css = m_mdCssEdit->toPlainText();
    // 通过 ConfigManager 持久化（会触发 configChanged 信号，MarkdownMode 监听后自动刷新预览）
    ConfigManager::instance().setMarkdownCustomCss(css);
    emit configChanged();
    ModernDialog::information(
        this,
        tr("已应用"),
        tr("Markdown 自定义 CSS 已应用，预览将自动刷新。")
    );
}

void SettingsPage::onMdCssImportClicked()
{
    if (!m_mdCssEdit) return;
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("导入 CSS 文件"),
        QStringLiteral(""),
        tr("CSS 文件 (*.css);;所有文件 (*)")
    );
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ModernDialog::warning(
            this,
            tr("导入失败"),
            tr("无法打开文件: %1").arg(path)
        );
        return;
    }
    QString css = QString::fromUtf8(file.readAll());
    file.close();

    // 加载到编辑区（不立即应用，需用户点击「应用」按钮确认）
    m_mdCssEdit->setPlainText(css);
    ModernDialog::information(
        this,
        tr("已导入"),
        tr("CSS 已导入编辑区，点击「应用」按钮生效。")
    );
}

void SettingsPage::onMdCssResetClicked()
{
    if (!m_mdCssEdit) return;
    int result = ModernDialog::question(
        this,
        tr("重置确认"),
        tr("确定清空自定义 CSS 并恢复使用主题预设吗？")
    );
    if (result != ModernDialog::ROLE_ACCEPT) return;

    m_mdCssEdit->clear();
    ConfigManager::instance().setMarkdownCustomCss(QString());
    emit configChanged();
    ModernDialog::information(
        this,
        tr("已重置"),
        tr("已清空自定义 CSS，预览将使用主题预设。")
    );
}

// ============================================================
// 快捷键设置页面创建
// ============================================================

void SettingsPage::createShortcutsPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    // 标题
    auto* titleLabel = new QLabel(tr("快捷键"), page);
    titleLabel->setObjectName(QStringLiteral("settingsMainTitle"));
    layout->addWidget(titleLabel);

    auto* hintLabel = new QLabel(tr("查看和自定义所有快捷键，系统自动检测冲突。双击快捷键单元格进入录制。"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(hintLabel);

    // 搜索框（P2-H05 子项2: 占位文本「搜索快捷键...」+ 200ms 防抖）
    m_shortcutSearchInput = new QLineEdit(page);
    m_shortcutSearchInput->setPlaceholderText(tr("搜索快捷键..."));
    m_shortcutSearchInput->setObjectName(QStringLiteral("searchInput"));
    layout->addWidget(m_shortcutSearchInput);

    // P2-H05 子项2: 搜索防抖定时器（200ms）
    m_shortcutSearchDebounceTimer = new QTimer(this);
    m_shortcutSearchDebounceTimer->setSingleShot(true);
    m_shortcutSearchDebounceTimer->setInterval(200);
    connect(m_shortcutSearchDebounceTimer, &QTimer::timeout,
            this, &SettingsPage::onShortcutSearchDebounced);

    // 表格 — P2-H05 子项2: 改为 3 列（命令、描述、快捷键），支持点击表头排序
    m_shortcutTable = new QTableWidget(page);
    m_shortcutTable->setColumnCount(3);
    m_shortcutTable->setHorizontalHeaderLabels({
        tr("命令"), tr("描述"), tr("快捷键")
    });
    m_shortcutTable->horizontalHeader()->setStretchLastSection(false);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    // P2-H05 子项2: 启用点击表头排序
    m_shortcutTable->setSortingEnabled(true);
    m_shortcutTable->horizontalHeader()->setSectionsClickable(true);
    m_shortcutTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_shortcutTable->setAlternatingRowColors(true);
    m_shortcutTable->verticalHeader()->setVisible(false);  // J4: 隐藏左侧行号列
    m_shortcutTable->setShowGrid(false);                    // J4: 弱化网格线，提升质感

    // J3/J4: 表格样式提取到 applyShortcutPageTheme()，支持主题切换时动态刷新
    applyShortcutPageTheme();

    layout->addWidget(m_shortcutTable, 1);

    // 初始化快捷键数据（从 ShortcutManager 加载）
    auto& shortcutMgr = ShortcutManager::instance();
    if (!shortcutMgr.allShortcuts().isEmpty()) {
        // 使用 ShortcutManager 的数据
        m_shortcutItems.clear();
        for (const auto& item : shortcutMgr.allShortcuts()) {
            ShortcutItem uiItem;
            uiItem.id = item.id;
            uiItem.commandName = item.displayName;
            uiItem.description = item.description;
            uiItem.keySequence = item.currentKey.toString();
            uiItem.category = item.category;
            uiItem.defaultKey = item.defaultKey.toString();
            m_shortcutItems.append(uiItem);
        }
    } else {
        // Fallback：硬编码默认值（兼容旧版本，ShortcutManager 未初始化时使用）
        m_shortcutItems = {
            {QStringLiteral("file.open"),     tr("打开文件"),          tr("打开文件对话框"),        QStringLiteral("Ctrl+O"),        tr("文件"),   QStringLiteral("Ctrl+O")},
            {QStringLiteral("file.new"),      tr("新建文件"),          tr("创建新文件"),            QStringLiteral("Ctrl+N"),        tr("文件"),   QStringLiteral("Ctrl+N")},
            {QStringLiteral("file.save"),     tr("保存文件"),          tr("保存当前文件"),          QStringLiteral("Ctrl+S"),        tr("文件"),   QStringLiteral("Ctrl+S")},
            {QStringLiteral("file.saveAs"),   tr("另存为"),            tr("另存为新文件"),          QStringLiteral("Ctrl+Shift+S"),  tr("文件"),   QStringLiteral("Ctrl+Shift+S")},
            {QStringLiteral("edit.undo"),     tr("撤销"),              tr("撤销上一步操作"),        QStringLiteral("Ctrl+Z"),        tr("编辑"),   QStringLiteral("Ctrl+Z")},
            {QStringLiteral("edit.redo"),     tr("重做"),              tr("重做撤销的操作"),        QStringLiteral("Ctrl+Y"),        tr("编辑"),   QStringLiteral("Ctrl+Y")},
            {QStringLiteral("edit.find"),     tr("查找"),              tr("打开查找对话框"),        QStringLiteral("Ctrl+F"),        tr("编辑"),   QStringLiteral("Ctrl+F")},
            {QStringLiteral("edit.replace"),  tr("替换"),              tr("打开替换对话框"),        QStringLiteral("Ctrl+H"),        tr("编辑"),   QStringLiteral("Ctrl+H")},
            {QStringLiteral("command.palette"),tr("命令面板"),         tr("打开全局命令搜索框"),    QStringLiteral("Ctrl+Shift+P"),  tr("全局"),   QStringLiteral("Ctrl+Shift+P")},
            {QStringLiteral("file.closeTab"), tr("关闭标签页"),        tr("关闭当前标签页"),        QStringLiteral("Ctrl+W"),        tr("文件"),   QStringLiteral("Ctrl+W")},
        };
    }

    // 填充表格数据
    refreshShortcutTable(QString());

    // 底部操作按钮区 — P2-H05 子项4: 4 个按钮（重置为默认 / 切换 VSCode 预设 / 导出 / 导入）
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);

    auto* btnReset = new QPushButton(tr("重置为默认"), page);
    btnReset->setObjectName(QStringLiteral("btnResetSection"));
    btnLayout->addWidget(btnReset);

    m_btnShortcutVSCode = new QPushButton(tr("切换 VSCode 预设"), page);
    m_btnShortcutVSCode->setObjectName(QStringLiteral("btnShortcutVSCode"));
    btnLayout->addWidget(m_btnShortcutVSCode);

    auto* btnExport = new QPushButton(tr("导出"), page);
    btnExport->setObjectName(QStringLiteral("btnExportConfig"));
    btnLayout->addWidget(btnExport);

    auto* btnImport = new QPushButton(tr("导入"), page);
    btnImport->setObjectName(QStringLiteral("btnImportConfig"));
    btnLayout->addWidget(btnImport);

    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    // 信号连接
    connect(m_shortcutSearchInput, &QLineEdit::textChanged,
            this, &SettingsPage::onShortcutSearchChanged);
    // P2-H05 子项3: 双击快捷键列进入录制模式
    connect(m_shortcutTable, &QTableWidget::cellDoubleClicked,
            this, &SettingsPage::onShortcutCellDoubleClicked);
    connect(btnReset, &QPushButton::clicked,
            this, &SettingsPage::onShortcutResetDefaults);
    connect(m_btnShortcutVSCode, &QPushButton::clicked,
            this, &SettingsPage::onShortcutApplyVSCode);
    connect(btnExport, &QPushButton::clicked,
            this, &SettingsPage::onShortcutExport);
    connect(btnImport, &QPushButton::clicked,
            this, &SettingsPage::onShortcutImport);

    // 添加到页面堆栈
    m_pageStack->addWidget(page);
}

// J3/J4: 应用快捷键页面主题样式 — 表格、搜索框、按钮全量适配主题
// 切换主题时由 ThemeManager::themeChanged 信号触发重新调用
void SettingsPage::applyShortcutPageTheme()
{
    if (!m_shortcutTable) return;

    const auto& p = ThemeManager::instance().currentPalette();

    // 提取主题色为 CSS 颜色字符串
    QString bg       = p.bgDialog.name(QColor::HexRgb);
    QString fg       = p.fgPrimary.name(QColor::HexRgb);
    QString fgSec    = p.fgSecondary.name(QColor::HexRgb);
    QString border   = p.borderDefault.name(QColor::HexRgb);
    QString borderF  = p.borderFocus.name(QColor::HexRgb);
    QString altBg    = p.bgHover.name(QColor::HexRgb);
    QString headerBg = p.bgTitleBar.name(QColor::HexRgb);
    QString accent   = p.accentPrimary.name(QColor::HexArgb);
    QString accentH  = p.accentHover.name(QColor::HexArgb);
    QString accentP  = p.accentPressed.name(QColor::HexArgb);
    QString selBg    = p.selectionBg.name(QColor::HexArgb);
    QString inputBg  = p.bgInput.name(QColor::HexRgb);

    // J4: 表格样式 — 圆角边框、交替行色、hover 高亮、弱化网格线
    QString tableQss = QStringLiteral(
        // 表格容器：圆角边框 + 主题背景
        "QTableWidget {"
        "  background-color: %1;"
        "  alternate-background-color: %2;"
        "  color: %3;"
        "  border: 1px solid %4;"
        "  border-radius: 6px;"
        "  outline: none;"
        "}"
        // 数据行：增加内边距提升可读性
        "QTableWidget::item {"
        "  padding: 6px 10px;"
        "  border-bottom: 1px solid %4;"
        "}"
        // 悬浮行：主题 hover 色
        "QTableWidget::item:hover {"
        "  background-color: %2;"
        "}"
        // 选中行：强调色背景 + 白色文字
        "QTableWidget::item:selected {"
        "  background-color: %5;"
        "  color: #ffffff;"
        "}"
        // 表头：深色背景 + 底部强调线 + 加粗
        "QHeaderView::section {"
        "  background-color: %6;"
        "  color: %3;"
        "  padding: 8px 10px;"
        "  border: none;"
        "  border-bottom: 2px solid %7;"
        "  font-weight: bold;"
        "}"
        // 修改按钮：圆角 + hover/pressed 动效
        "QPushButton {"
        "  background-color: %7;"
        "  color: white;"
        "  border: none;"
        "  padding: 5px 14px;"
        "  border-radius: 4px;"
        "  min-width: 50px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: %8;"
        "}"
        "QPushButton:pressed {"
        "  background-color: %9;"
        "}"
    ).arg(bg, altBg, fg, border, selBg, headerBg, accent, accentH, accentP);

    m_shortcutTable->setStyleSheet(tableQss);

    // J4: 搜索框样式 — 主题边框 + focus 高亮
    if (m_shortcutSearchInput) {
        m_shortcutSearchInput->setStyleSheet(QStringLiteral(
            "QLineEdit {"
            "  background-color: %1;"
            "  color: %2;"
            "  border: 1px solid %3;"
            "  border-radius: 4px;"
            "  padding: 6px 10px;"
            "  font-size: 13px;"
            "}"
            "QLineEdit:focus {"
            "  border: 1px solid %4;"
            "}"
            "QLineEdit::placeholder {"
            "  color: %5;"
            "}"
        ).arg(inputBg, fg, border, borderF, fgSec));
    }
}

void SettingsPage::refreshShortcutTable(const QString& filter)
{
    // P2-H05 子项2: 排序期间禁用重排，避免插入时触发不稳定排序
    m_shortcutTable->setSortingEnabled(false);
    m_shortcutTable->setRowCount(0);

    for (int i = 0; i < m_shortcutItems.size(); ++i) {
        const auto& item = m_shortcutItems[i];

        // P2-H05 子项2: 按命令 ID、描述、当前快捷键文本匹配
        if (!filter.isEmpty()) {
            bool match = item.commandName.contains(filter, Qt::CaseInsensitive)
                      || item.keySequence.contains(filter, Qt::CaseInsensitive)
                      || item.description.contains(filter, Qt::CaseInsensitive)
                      || item.id.contains(filter, Qt::CaseInsensitive)
                      || item.category.contains(filter, Qt::CaseInsensitive);
            if (!match) continue;
        }

        int row = m_shortcutTable->rowCount();
        m_shortcutTable->insertRow(row);

        // 列 0：命令（displayName）
        auto* nameItem = new QTableWidgetItem(item.commandName);
        nameItem->setData(Qt::UserRole, i);  // 存储原始 m_shortcutItems 索引
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_shortcutTable->setItem(row, 0, nameItem);

        // 列 1：描述
        auto* descItem = new QTableWidgetItem(item.description);
        descItem->setData(Qt::UserRole, i);
        descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);
        m_shortcutTable->setItem(row, 1, descItem);

        // 列 2：当前快捷键（冲突高亮）
        auto* keyItem = new QTableWidgetItem(item.keySequence);
        keyItem->setData(Qt::UserRole, i);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        // 冲突检测：高亮重复的快捷键
        bool hasConflict = false;
        for (int j = 0; j < m_shortcutItems.size(); ++j) {
            if (j != i && !item.keySequence.isEmpty()
                && m_shortcutItems[j].keySequence == item.keySequence) {
                hasConflict = true;
                break;
            }
        }
        if (hasConflict) {
            keyItem->setForeground(QColor("#ff4444"));  // 红色高亮冲突
        }
        m_shortcutTable->setItem(row, 2, keyItem);
    }

    // 重新启用排序（P2-H05 子项2: 支持点击表头排序）
    m_shortcutTable->setSortingEnabled(true);
}

void SettingsPage::onShortcutModifyClicked(int dataIndex)
{
    // P2-H05 子项3: 保留向后兼容入口（点击修改按钮触发）
    // 实际录制入口已迁移到 onShortcutCellDoubleClicked
    if (dataIndex < 0 || dataIndex >= m_shortcutItems.size()) return;

    // 在快捷键列中找到对应行并触发双击录制
    for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
        QTableWidgetItem* it = m_shortcutTable->item(row, 2);
        if (it && it->data(Qt::UserRole).toInt() == dataIndex) {
            onShortcutCellDoubleClicked(row, 2);
            return;
        }
    }
}

void SettingsPage::onShortcutSearchChanged(const QString& text)
{
    // P2-H05 子项2: 防抖 200ms — 避免每次按键同步全量过滤
    m_pendingShortcutKeyword = text.trimmed();
    if (m_shortcutSearchDebounceTimer) {
        m_shortcutSearchDebounceTimer->start();  // 单次定时器，会自动重置
    } else {
        refreshShortcutTable(m_pendingShortcutKeyword);
    }
}

void SettingsPage::onShortcutSearchDebounced()
{
    refreshShortcutTable(m_pendingShortcutKeyword);
}

// P2-H05 子项3: 双击快捷键单元格进入按键录制模式
void SettingsPage::onShortcutCellDoubleClicked(int row, int col)
{
    // 仅快捷键列（第 2 列）进入录制
    if (col != 2) return;
    QTableWidgetItem* keyItem = m_shortcutTable->item(row, col);
    if (!keyItem) return;

    int dataIndex = keyItem->data(Qt::UserRole).toInt();
    if (dataIndex < 0 || dataIndex >= m_shortcutItems.size()) return;

    // 排序期间禁用，避免录制过程中行被重排
    m_shortcutTable->setSortingEnabled(false);

    auto* editor = new KeySequenceEdit(m_shortcutTable);
    editor->setKeySequence(QKeySequence(m_shortcutItems[dataIndex].keySequence));
    m_shortcutTable->setCellWidget(row, col, editor);
    editor->setFocus();
    editor->selectAll();

    // 录制完成：提交并恢复显示
    // 捕获 row 以避免 currentRow() 在延迟发射时已变化
    QPointer<KeySequenceEdit> editorGuard(editor);
    int dataIndexCapture = dataIndex;
    int rowCapture = row;
    auto commitEdit = [this, editorGuard, dataIndexCapture, rowCapture]() {
        if (!editorGuard) return;
        QKeySequence newSeq = editorGuard->keySequence();

        // 冲突检测
        auto& shortcutMgr = ShortcutManager::instance();
        QString id = m_shortcutItems[dataIndexCapture].id;
        QStringList conflicts = shortcutMgr.checkConflict(newSeq,
            id.isEmpty() ? QString() : id);

        if (!conflicts.isEmpty() && !newSeq.isEmpty()) {
            QString conflictNames;
            for (const auto& cid : conflicts) {
                auto cItem = shortcutMgr.shortcut(cid);
                if (!conflictNames.isEmpty()) conflictNames += QStringLiteral(", ");
                conflictNames += cItem.displayName;
            }
            int r = ModernDialog::warning(this, tr("快捷键冲突"),
                tr("该快捷键已被「%1」使用，是否覆盖？").arg(conflictNames));
            if (r != ModernDialog::ROLE_ACCEPT) {
                // 取消，恢复原值
                m_shortcutTable->removeCellWidget(rowCapture, 2);
                m_shortcutTable->setSortingEnabled(true);
                refreshShortcutTable(m_shortcutSearchInput->text());
                return;
            }
            // 用户确认覆盖，清空冲突命令的快捷键
            for (const auto& cid : conflicts) {
                shortcutMgr.setShortcut(cid, QKeySequence());
            }
        }

        // 提交新值
        m_shortcutItems[dataIndexCapture].keySequence = newSeq.toString();
        if (!id.isEmpty()) {
            shortcutMgr.setShortcut(id, newSeq);
        }

        // 移除编辑器并刷新表格
        m_shortcutTable->removeCellWidget(rowCapture, 2);
        m_shortcutTable->setSortingEnabled(true);
        refreshShortcutTable(m_shortcutSearchInput->text());
    };

    connect(editor, &KeySequenceEdit::editingFinished, this, commitEdit);
    connect(editor, &KeySequenceEdit::editingCanceled, this, [this, rowCapture]() {
        m_shortcutTable->removeCellWidget(rowCapture, 2);
        m_shortcutTable->setSortingEnabled(true);
        refreshShortcutTable(m_shortcutSearchInput->text());
    });
}

void SettingsPage::onShortcutResetDefaults()
{
    int result = ModernDialog::question(this, tr("重置为默认"),
        tr("确定要将所有快捷键重置为默认预设吗？"));
    if (result != ModernDialog::ROLE_ACCEPT) return;

    // P2-H05 子项4: 调用 applyPreset(Default) 而非 resetAllToDefault
    // 以确保 currentPreset 持久化字段同步更新
    auto& shortcutMgr = ShortcutManager::instance();
    shortcutMgr.applyPreset(ShortcutPreset::Default);

    // 刷新UI数据
    m_shortcutItems.clear();
    for (const auto& item : shortcutMgr.allShortcuts()) {
        ShortcutItem uiItem;
        uiItem.id = item.id;
        uiItem.commandName = item.displayName;
        uiItem.description = item.description;
        uiItem.keySequence = item.currentKey.toString();
        uiItem.category = item.category;
        uiItem.defaultKey = item.defaultKey.toString();
        m_shortcutItems.append(uiItem);
    }

    refreshShortcutTable(m_shortcutSearchInput->text());
}

// P2-H05 子项4: 切换到 VSCode 预设
void SettingsPage::onShortcutApplyVSCode()
{
    int result = ModernDialog::question(this, tr("切换 VSCode 预设"),
        tr("将批量覆盖关键命令的快捷键为 VSCode 风格，是否继续？"));
    if (result != ModernDialog::ROLE_ACCEPT) return;

    auto& shortcutMgr = ShortcutManager::instance();
    shortcutMgr.applyPreset(ShortcutPreset::VSCode);

    // 刷新UI数据
    m_shortcutItems.clear();
    for (const auto& item : shortcutMgr.allShortcuts()) {
        ShortcutItem uiItem;
        uiItem.id = item.id;
        uiItem.commandName = item.displayName;
        uiItem.description = item.description;
        uiItem.keySequence = item.currentKey.toString();
        uiItem.category = item.category;
        uiItem.defaultKey = item.defaultKey.toString();
        m_shortcutItems.append(uiItem);
    }

    refreshShortcutTable(m_shortcutSearchInput->text());
    ModernDialog::information(this, tr("已切换"),
        tr("已切换到 VSCode 预设方案"));
}

void SettingsPage::onShortcutExport()
{
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("导出快捷键"),
        QString(), tr("JSON 文件 (*.json)")
    );
    if (filePath.isEmpty()) return;

    // P2-H05 子项4: 使用 ShortcutManager::exportToJson 导出全部快捷键
    auto& shortcutMgr = ShortcutManager::instance();
    if (shortcutMgr.exportToJson(filePath)) {
        ModernDialog::information(this, tr("导出成功"),
            tr("快捷键配置已导出到：\n%1").arg(filePath));
    } else {
        ModernDialog::warning(this, tr("导出失败"),
            tr("无法写入文件：\n%1").arg(filePath));
    }
}

void SettingsPage::onShortcutImport()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入快捷键"),
        QString(), tr("JSON 文件 (*.json)")
    );
    if (filePath.isEmpty()) return;

    // P2-H05 子项4: 导入前预检冲突，弹窗提示用户确认
    auto& shortcutMgr = ShortcutManager::instance();
    QStringList conflicts = shortcutMgr.checkImportConflicts(filePath);

    if (conflicts.size() == 1 && conflicts.first() == QStringLiteral("PARSE_ERROR")) {
        ModernDialog::warning(this, tr("导入失败"),
            tr("JSON 解析错误或无法读取文件"));
        return;
    }

    if (!conflicts.isEmpty()) {
        int r = ModernDialog::warning(this, tr("导入冲突"),
            tr("导入将覆盖以下命令的快捷键：\n\n%1\n\n是否继续？")
                .arg(conflicts.join(QStringLiteral("\n"))));
        if (r != ModernDialog::ROLE_ACCEPT) return;
    }

    if (!shortcutMgr.importFromJson(filePath)) {
        ModernDialog::warning(this, tr("导入失败"),
            tr("导入过程中发生错误"));
        return;
    }

    // 刷新UI数据
    m_shortcutItems.clear();
    for (const auto& item : shortcutMgr.allShortcuts()) {
        ShortcutItem uiItem;
        uiItem.id = item.id;
        uiItem.commandName = item.displayName;
        uiItem.description = item.description;
        uiItem.keySequence = item.currentKey.toString();
        uiItem.category = item.category;
        uiItem.defaultKey = item.defaultKey.toString();
        m_shortcutItems.append(uiItem);
    }

    refreshShortcutTable(m_shortcutSearchInput->text());
    ModernDialog::information(this, tr("导入成功"),
        tr("快捷键配置已成功导入"));
}

void SettingsPage::loadCurrentConfig()
{
    auto& config = ConfigManager::instance();

    // 主题
    QString theme = config.theme();
    int themeIdx = m_themeCombo->findData(theme);
    if (themeIdx >= 0) m_themeCombo->setCurrentIndex(themeIdx);

    // 字体
    m_fontSizeSpin->setValue(config.fontSize());

    // 语言
    QString lang = config.getValue("app/language", QStringLiteral("zh_CN")).toString();
    int langIdx = m_languageCombo->findData(lang);
    if (langIdx >= 0) m_languageCombo->setCurrentIndex(langIdx);

    // 编辑器
    m_autoSaveCheck->setChecked(config.autoSave());
    m_completionCheck->setChecked(config.showCompletion());
    m_lineNumbersCheck->setChecked(config.showLineNumbers());

    // 终端
    QString termType = config.getValue("Terminal/type", QStringLiteral("cmd")).toString();
    int termIdx = m_terminalTypeCombo->findData(termType);
    if (termIdx >= 0) m_terminalTypeCombo->setCurrentIndex(termIdx);

    int termFont = config.getValue("Terminal/fontSize", 13).toInt();
    m_terminalFontSpin->setValue(termFont);

    // 终端外观配置（字体、颜色、光标）
    QString fontFamily = config.getValue("Terminal/fontFamily", QStringLiteral("Consolas")).toString();
    int familyIdx = m_terminalFontFamilyCombo->findData(fontFamily);
    if (familyIdx >= 0) m_terminalFontFamilyCombo->setCurrentIndex(familyIdx);

    QString fgColor = config.getValue("Terminal/fgColor", QStringLiteral("#cccccc")).toString();
    m_terminalFgColorLabel->setText(fgColor);
    m_terminalFgColorBtn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(fgColor));

    QString bgColor = config.getValue("Terminal/bgColor", QStringLiteral("#1e1e1e")).toString();
    m_terminalBgColorLabel->setText(bgColor);
    m_terminalBgColorBtn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(bgColor));

    QString cursorColor = config.getValue("Terminal/cursorColor", QStringLiteral("#ffffff")).toString();
    m_terminalCursorColorLabel->setText(cursorColor);
    m_terminalCursorColorBtn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(cursorColor));

    QString cursorShape = config.getValue("Terminal/cursorShape", QStringLiteral("block")).toString();
    int shapeIdx = m_terminalCursorShapeCombo->findData(cursorShape);
    if (shapeIdx >= 0) m_terminalCursorShapeCombo->setCurrentIndex(shapeIdx);

    bool cursorBlink = config.getValue("Terminal/cursorBlink", true).toBool();
    m_terminalCursorBlinkCheck->setChecked(cursorBlink);

    // 智能提示
    int compDelay = config.getValue("Completion/delay", 500).toInt();
    m_completionDelaySpin->setValue(compDelay);

    int minPrefix = config.getValue("Completion/minPrefix", 2).toInt();
    m_minPrefixSpin->setValue(minPrefix);

    // 匹配模式
    QString matchMode = config.getValue("Completion/matchingMode", QStringLiteral("fuzzy")).toString();
    int matchIdx = m_matchingModeCombo->findData(matchMode);
    if (matchIdx >= 0) m_matchingModeCombo->setCurrentIndex(matchIdx);

    // 缩进
    int tabSize = config.getValue("Editor/tabSize", 4).toInt();
    m_tabSizeSpin->setValue(tabSize);

    // M4: 缩进风格
    QString indentStyle = config.getValue("Editor/indentStyle", QStringLiteral("spaces")).toString();
    int styleIdx = m_indentStyleCombo->findData(indentStyle);
    if (styleIdx >= 0) m_indentStyleCombo->setCurrentIndex(styleIdx);

    // M4: 格式化工具路径
    QString formatPath = config.getValue("Editor/formatToolPath", QString()).toString();
    if (!formatPath.isEmpty()) {
        m_formatToolPathLabel->setText(QFileInfo(formatPath).fileName());
        m_formatToolPathLabel->setToolTip(formatPath);
    }

    // M10: JSON自动格式化
    bool autoFormatJson = config.getValue("Editor/autoFormatJson", false).toBool();
    m_autoFormatJsonCheck->setChecked(autoFormatJson);

    // P3-M03 子项5: 拼写检查开关
    m_spellCheckCheck->setChecked(config.spellCheckEnabled());

    // LSP 配置
    QString lspPythonPath = config.getValue("LSP/pythonServer", QString()).toString();
    if (!lspPythonPath.isEmpty()) {
        m_lspPythonPathLabel->setText(QFileInfo(lspPythonPath).fileName());
        m_lspPythonPathLabel->setToolTip(lspPythonPath);
    }
    QString lspCppPath = config.getValue("LSP/cppServer", QString()).toString();
    if (!lspCppPath.isEmpty()) {
        m_lspCppPathLabel->setText(QFileInfo(lspCppPath).fileName());
        m_lspCppPathLabel->setToolTip(lspCppPath);
    }
    QString lspJsPath = config.getValue("LSP/jsServer", QString()).toString();
    if (!lspJsPath.isEmpty()) {
        m_lspJsPathLabel->setText(QFileInfo(lspJsPath).fileName());
        m_lspJsPathLabel->setToolTip(lspJsPath);
    }
    bool lspAutoStart = config.getValue("LSP/autoStart", true).toBool();
    m_lspAutoStartCheck->setChecked(lspAutoStart);

    // P1 C05-2: 构建配置（使用 ConfigManager 专用方法）
    QString buildQtPath = config.qtPrefixPath();
    if (!buildQtPath.isEmpty()) {
        m_buildQtPathLabel->setText(QDir(buildQtPath).dirName());
        m_buildQtPathLabel->setToolTip(buildQtPath);
    }
    QString buildSslPath = config.opensslPath();
    if (!buildSslPath.isEmpty()) {
        m_buildOpenSslPathLabel->setText(QDir(buildSslPath).dirName());
        m_buildOpenSslPathLabel->setToolTip(buildSslPath);
    }
    QString buildZlibPath = config.getValue(QStringLiteral("Build/zlibPath")).toString();
    if (!buildZlibPath.isEmpty()) {
        m_buildZlibPathLabel->setText(QDir(buildZlibPath).dirName());
        m_buildZlibPathLabel->setToolTip(buildZlibPath);
    }
    // 构建类型：阻塞信号避免启动时触发 onBuildTypeChanged → configChanged
    QString buildType = config.buildType();
    int buildTypeIdx = m_buildTypeCombo->findData(buildType);
    {
        QSignalBlocker blocker(m_buildTypeCombo);
        m_buildTypeCombo->setCurrentIndex(buildTypeIdx >= 0 ? buildTypeIdx : 0);
    }
    // 编译器路径
    QString compilerPath = config.compilerPath();
    if (!compilerPath.isEmpty()) {
        m_buildCompilerPathLabel->setText(QFileInfo(compilerPath).fileName());
        m_buildCompilerPathLabel->setToolTip(compilerPath);
    }
    // CMake 额外参数
    m_buildCmakeArgsEdit->setText(config.cmakeExtraArgs());

    // P3-M02 子项2: Markdown 自定义 CSS（加载到编辑区，便于用户继续编辑）
    if (m_mdCssEdit) {
        m_mdCssEdit->setPlainText(config.markdownCustomCss());
    }
}

// ========== 槽函数 ==========

void SettingsPage::onThemeChanged(int index)
{
    QString themeKey = m_themeCombo->itemData(index).toString();
    emit themeChanged(themeKey);
    ConfigManager::instance().setTheme(themeKey);
    emit configChanged();
}

void SettingsPage::onFontSizeChanged(int value)
{
    emit fontSizeChanged(value);
    ConfigManager::instance().setFontSize(value);
    emit configChanged();
}

void SettingsPage::onAutoSaveToggled(bool checked)
{
    ConfigManager::instance().setAutoSave(checked);
    emit configChanged();
}

void SettingsPage::onCompletionToggled(bool checked)
{
    ConfigManager::instance().setShowCompletion(checked);
    emit configChanged();
}

void SettingsPage::onLineNumbersToggled(bool checked)
{
    ConfigManager::instance().setShowLineNumbers(checked);
    emit configChanged();
}

void SettingsPage::onTabSizeChanged(int value)
{
    ConfigManager::instance().setValue("Editor/tabSize", value);
    emit configChanged();
}

// ========== M4: 格式化相关槽函数 ==========

void SettingsPage::onIndentStyleChanged(int index)
{
    QString style = m_indentStyleCombo->itemData(index).toString();
    ConfigManager::instance().setValue("Editor/indentStyle", style);
    emit configChanged();
}

void SettingsPage::onFormatToolPathClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("选择 clang-format 可执行文件"),
        QStringLiteral(""),
        tr("可执行文件 (*.exe);;所有文件 (*)")
    );
    if (!path.isEmpty()) {
        m_formatToolPathLabel->setText(path);
        m_formatToolPathLabel->setToolTip(path);
        ConfigManager::instance().setValue("Editor/formatToolPath", path);
        emit configChanged();
    }
}

// ========== M10: JSON自动格式化槽函数 ==========

void SettingsPage::onAutoFormatJsonToggled(bool checked)
{
    ConfigManager::instance().setValue("Editor/autoFormatJson", checked);
    emit configChanged();
}

// ========== P3-M03 子项5: 拼写检查槽函数 ==========

void SettingsPage::onSpellCheckToggled(bool checked)
{
    ConfigManager::instance().setSpellCheckEnabled(checked);
    // configChanged 信号由 ConfigManager::setValue 内部触发，各编辑器监听后同步开关
    emit configChanged();
}

void SettingsPage::onTerminalTypeChanged(int index)
{
    QString type = m_terminalTypeCombo->itemData(index).toString();
    ConfigManager::instance().setValue("Terminal/type", type);
    emit configChanged();
}

void SettingsPage::onTerminalFontChanged(int value)
{
    ConfigManager::instance().setValue("Terminal/fontSize", value);
    emit configChanged();
    emit terminalAppearanceChanged();
}

void SettingsPage::onTerminalFontFamilyChanged(int index)
{
    QString family = m_terminalFontFamilyCombo->itemData(index).toString();
    ConfigManager::instance().setValue("Terminal/fontFamily", family);
    emit configChanged();
    emit terminalAppearanceChanged();
}

void SettingsPage::onTerminalFgColorClicked()
{
    QColor currentColor = QColor(m_terminalFgColorLabel->text());
    QColor color = QColorDialog::getColor(currentColor, this, tr("选择前景色"));
    if (color.isValid()) {
        QString hexColor = color.name(QColor::HexRgb);
        m_terminalFgColorLabel->setText(hexColor);
        m_terminalFgColorBtn->setStyleSheet(
            QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(hexColor));
        ConfigManager::instance().setValue("Terminal/fgColor", hexColor);
        emit configChanged();
        emit terminalAppearanceChanged();
    }
}

void SettingsPage::onTerminalBgColorClicked()
{
    QColor currentColor = QColor(m_terminalBgColorLabel->text());
    QColor color = QColorDialog::getColor(currentColor, this, tr("选择背景色"));
    if (color.isValid()) {
        QString hexColor = color.name(QColor::HexRgb);
        m_terminalBgColorLabel->setText(hexColor);
        m_terminalBgColorBtn->setStyleSheet(
            QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(hexColor));
        ConfigManager::instance().setValue("Terminal/bgColor", hexColor);
        emit configChanged();
        emit terminalAppearanceChanged();
    }
}

void SettingsPage::onTerminalCursorColorClicked()
{
    QColor currentColor = QColor(m_terminalCursorColorLabel->text());
    QColor color = QColorDialog::getColor(currentColor, this, tr("选择光标颜色"));
    if (color.isValid()) {
        QString hexColor = color.name(QColor::HexRgb);
        m_terminalCursorColorLabel->setText(hexColor);
        m_terminalCursorColorBtn->setStyleSheet(
            QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(hexColor));
        ConfigManager::instance().setValue("Terminal/cursorColor", hexColor);
        emit configChanged();
        emit terminalAppearanceChanged();
    }
}

void SettingsPage::onTerminalCursorShapeChanged(int index)
{
    QString shape = m_terminalCursorShapeCombo->itemData(index).toString();
    ConfigManager::instance().setValue("Terminal/cursorShape", shape);
    emit configChanged();
    emit terminalAppearanceChanged();
}

void SettingsPage::onTerminalCursorBlinkToggled(bool checked)
{
    ConfigManager::instance().setValue("Terminal/cursorBlink", checked);
    emit configChanged();
    emit terminalAppearanceChanged();
}

void SettingsPage::onCompletionDelayChanged(int value)
{
    ConfigManager::instance().setValue("Completion/delay", value);
    emit configChanged();
}

void SettingsPage::onMinPrefixChanged(int value)
{
    ConfigManager::instance().setValue("Completion/minPrefix", value);
    emit configChanged();
}

void SettingsPage::onMatchingModeChanged(int index)
{
    QString mode = m_matchingModeCombo->itemData(index).toString();
    ConfigManager::instance().setValue("Completion/matchingMode", mode);
    emit configChanged();
}

void SettingsPage::onLanguageChanged(int index)
{
    QString lang = m_languageCombo->itemData(index).toString();
    // P3-M05: 使用 ConfigManager::setLanguage 统一持久化（含校验逻辑）
    ConfigManager::instance().setLanguage(lang);
    // 立即切换翻译器与 QLocale（已构造 UI 仍需重启完全生效）
    I18nManager::instance().switchLanguage(lang);
    // 语言切换需要重启应用才能生效
    ModernDialog::information(
        this,
        tr("提示"),
        tr("语言切换需要重启应用才能生效")
    );
}

// ========== M8: LSP 配置槽函数 ==========

void SettingsPage::onLspPythonPathClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("选择 Python LSP 服务器 (pylsp)"),
        QStringLiteral(""),
        tr("可执行文件 (*.exe);;所有文件 (*)")
    );
    if (!path.isEmpty()) {
        m_lspPythonPathLabel->setText(QFileInfo(path).fileName());
        m_lspPythonPathLabel->setToolTip(path);
        ConfigManager::instance().setValue(QStringLiteral("LSP/pythonServer"), path);
        emit configChanged();
    }
}

void SettingsPage::onLspCppPathClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("选择 C++ LSP 服务器 (clangd)"),
        QStringLiteral(""),
        tr("可执行文件 (*.exe);;所有文件 (*)")
    );
    if (!path.isEmpty()) {
        m_lspCppPathLabel->setText(QFileInfo(path).fileName());
        m_lspCppPathLabel->setToolTip(path);
        ConfigManager::instance().setValue(QStringLiteral("LSP/cppServer"), path);
        emit configChanged();
    }
}

void SettingsPage::onLspJsPathClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("选择 JavaScript/TypeScript LSP 服务器"),
        QStringLiteral(""),
        tr("可执行文件 (*.exe *.cmd *.bat);;所有文件 (*)")
    );
    if (!path.isEmpty()) {
        m_lspJsPathLabel->setText(QFileInfo(path).fileName());
        m_lspJsPathLabel->setToolTip(path);
        ConfigManager::instance().setValue(QStringLiteral("LSP/jsServer"), path);
        emit configChanged();
    }
}

void SettingsPage::onLspAutoStartToggled(bool checked)
{
    ConfigManager::instance().setValue(QStringLiteral("LSP/autoStart"), checked);
    emit configChanged();
}

// ========== P1 C05-2: 构建配置槽函数 ==========

void SettingsPage::onBuildQtPathClicked()
{
    QString cur = ConfigManager::instance().qtPrefixPath();
    QString path = QFileDialog::getExistingDirectory(
        this, tr("选择 Qt 安装路径"),
        cur.isEmpty() ? QStringLiteral("") : cur
    );
    if (!path.isEmpty()) {
        m_buildQtPathLabel->setText(QDir(path).dirName());
        m_buildQtPathLabel->setToolTip(path);
        ConfigManager::instance().setQtPrefixPath(path);
        emit configChanged();
    }
}

void SettingsPage::onBuildOpenSslPathClicked()
{
    QString cur = ConfigManager::instance().opensslPath();
    QString path = QFileDialog::getExistingDirectory(
        this, tr("选择 OpenSSL 安装路径"),
        cur.isEmpty() ? QStringLiteral("") : cur
    );
    if (!path.isEmpty()) {
        m_buildOpenSslPathLabel->setText(QDir(path).dirName());
        m_buildOpenSslPathLabel->setToolTip(path);
        ConfigManager::instance().setOpensslPath(path);
        emit configChanged();
    }
}

void SettingsPage::onBuildZlibPathClicked()
{
    QString cur = ConfigManager::instance().getValue(QStringLiteral("Build/zlibPath")).toString();
    QString path = QFileDialog::getExistingDirectory(
        this, tr("选择 zlib 安装路径"),
        cur.isEmpty() ? QStringLiteral("") : cur
    );
    if (!path.isEmpty()) {
        m_buildZlibPathLabel->setText(QDir(path).dirName());
        m_buildZlibPathLabel->setToolTip(path);
        ConfigManager::instance().setValue(QStringLiteral("Build/zlibPath"), path);
        emit configChanged();
    }
}

void SettingsPage::onBuildTypeChanged(int index)
{
    QString buildType = m_buildTypeCombo->itemData(index).toString();
    ConfigManager::instance().setBuildType(buildType);
    emit configChanged();
    // P1 C05-4: 切换构建类型时提示用户需要重新运行 CMake 配置
    QMessageBox::information(
        this, tr("构建类型已变更"),
        tr("构建类型已切换为 %1，需要重新运行 CMake 配置才能生效。\n"
           "请在终端执行 cmake 命令重新配置构建目录。").arg(buildType));
}

// ========== P1 C05-3: 自动检测 Qt 安装（委托给 QtDetector）==========

QStringList SettingsPage::detectQtInstallations()
{
    // C05-3: 委托给 QtDetector::detectAll()，返回前缀路径列表（向后兼容）
    QStringList found;
    const auto installations = QtDetector::detectAll();
    for (const auto& inst : installations) {
        found << inst.prefixPath;
    }
    return found;
}

void SettingsPage::onBuildAutoDetectQt()
{
    // C05-3: 调用 QtDetector::detectAll()，获取带版本/编译器信息的 Qt 安装列表
    QList<QtInstallation> installations = QtDetector::detectAll();

    if (installations.isEmpty()) {
        QMessageBox::information(
            this, tr("自动检测 Qt"),
            tr("未在标准路径（各盘符 \\Qt\\<版本>\\<编译器>）下检测到 Qt 安装。\n"
               "请手动浏览选择 Qt 安装路径，或检查 Qt 是否已正确安装。"));
        return;
    }

    QString selected;
    QString selectedDisplay;
    if (installations.size() == 1) {
        selected = installations.first().prefixPath;
        selectedDisplay = installations.first().displayText();
    } else {
        // 多个候选：用 QInputDialog 让用户选择（展示版本与编译器信息）
        QStringList items;
        for (const auto& inst : installations) {
            items << inst.displayText();
        }
        bool ok = false;
        QString chosen = QInputDialog::getItem(
            this, tr("自动检测 Qt"),
            tr("检测到多个 Qt 安装，请选择一个："),
            items, 0, false, &ok);
        if (!ok || chosen.isEmpty()) return;
        // 通过显示文本反查 prefixPath
        for (const auto& inst : installations) {
            if (inst.displayText() == chosen) {
                selected = inst.prefixPath;
                selectedDisplay = inst.displayText();
                break;
            }
        }
        if (selected.isEmpty()) return;
    }

    // 应用所选路径（使用 ConfigManager 专用方法）
    m_buildQtPathLabel->setText(QDir(selected).dirName());
    m_buildQtPathLabel->setToolTip(selected);
    ConfigManager::instance().setQtPrefixPath(selected);
    emit configChanged();
    QMessageBox::information(
        this, tr("自动检测 Qt"),
        tr("已应用 Qt 路径：\n%1").arg(selectedDisplay));
}

// ========== P1 C05-4: 构建目录配置 ==========

void SettingsPage::onBuildDirClicked()
{
    // C05-4: 选择构建目录（默认项目根下的 build）
    QString cur = ConfigManager::instance().getValue(QStringLiteral("Build/buildDir")).toString();
    if (cur.isEmpty()) {
        // 默认为项目根目录下的 build
        cur = QDir::currentPath() + QStringLiteral("/build");
    }
    QString path = QFileDialog::getExistingDirectory(
        this, tr("选择构建输出目录"), cur);
    if (!path.isEmpty()) {
        m_buildDirLabel->setText(QDir(path).dirName());
        m_buildDirLabel->setToolTip(path);
        ConfigManager::instance().setValue(QStringLiteral("Build/buildDir"), path);
        emit configChanged();
    }
}

void SettingsPage::onBuildSeparateDirsToggled(bool checked)
{
    // C05-4: 按构建类型分离目录（build/Debug、build/Release 各自独立）
    ConfigManager::instance().setValue(QStringLiteral("Build/separateDirs"), checked);
    emit configChanged();
}

// ========== P1 C05-2: 编译器路径与 CMake 额外参数 ==========

void SettingsPage::onBuildCompilerPathClicked()
{
    // 选择编译器可执行文件（gcc/g++/cl.exe 等）
    QString cur = ConfigManager::instance().compilerPath();
    QString path = QFileDialog::getOpenFileName(
        this, tr("选择编译器可执行文件"),
        cur.isEmpty() ? QStringLiteral("") : cur,
        tr("可执行文件 (*.exe);;所有文件 (*)")
    );
    if (!path.isEmpty()) {
        m_buildCompilerPathLabel->setText(QFileInfo(path).fileName());
        m_buildCompilerPathLabel->setToolTip(path);
        ConfigManager::instance().setCompilerPath(path);
        emit configChanged();
    }
}

void SettingsPage::onBuildApplyReconfigure()
{
    // P1 C05-2: 写入配置文件，提示用户重启生效
    // 同步 CMake 额外参数到 ConfigManager
    ConfigManager::instance().setCmakeExtraArgs(m_buildCmakeArgsEdit->text().trimmed());
    ConfigManager::instance().sync();
    emit configChanged();

    // 提示用户：配置已写入，需手动重新运行 CMake 配置
    ModernDialog::information(
        this, tr("应用并重新配置"),
        tr("构建配置已写入配置文件。\n"
           "请在终端中切换到构建目录，重新执行 CMake 配置命令以使更改生效：\n\n"
           "  cmake <源码目录> -DCMAKE_PREFIX_PATH=<Qt路径> "
           "-DCMAKE_BUILD_TYPE=<构建类型> <额外参数>\n\n"
           "修改构建类型或路径后需要重新配置。"));
}

void SettingsPage::onCategoryChanged(int row)
{
    if (row >= 0 && row < m_pageStack->count()) {
        m_pageStack->setCurrentIndex(row);
    }
}

void SettingsPage::onSearchTextChanged(const QString& text)
{
    // Bug5: 防抖处理 — 缓存最新关键词，重启定时器，150ms 静止后才执行过滤
    // 避免快速输入（如 "CMake"）时每次按键都同步遍历+setHidden 导致主线程阻塞/白屏
    m_pendingKeyword = text.trimmed();
    if (m_searchDebounceTimer) {
        m_searchDebounceTimer->start();
    } else {
        filterSettings(m_pendingKeyword);
    }
}

void SettingsPage::onResetCurrentSection()
{
    int idx = m_categoryList->currentRow();
    switch (idx) {
    case 0: // 外观
        m_themeCombo->setCurrentIndex(0);
        m_fontSizeSpin->setValue(14);
        m_languageCombo->setCurrentIndex(0);  // 默认简体中文
        break;
    case 1: // 编辑器
        m_lineNumbersCheck->setChecked(true);
        m_tabSizeSpin->setValue(4);
        m_autoSaveCheck->setChecked(false);
        // M4: 格式化默认值
        m_indentStyleCombo->setCurrentIndex(0);  // Spaces
        m_formatToolPathLabel->setText(tr("(自动检测)"));
        m_formatToolPathLabel->setToolTip(QString());
        // M10: JSON格式化
        m_autoFormatJsonCheck->setChecked(false);
        // P3-M03 子项5: 拼写检查
        m_spellCheckCheck->setChecked(false);
        break;
    case 2: // 终端
        m_terminalTypeCombo->setCurrentIndex(0);
        m_terminalFontSpin->setValue(12);
        m_terminalFontFamilyCombo->setCurrentIndex(0);
        // 重置颜色为默认值
        m_terminalFgColorLabel->setText(QStringLiteral("#cccccc"));
        m_terminalFgColorBtn->setStyleSheet(QStringLiteral("background-color: #cccccc; border: 1px solid #555; border-radius: 2px;"));
        m_terminalBgColorLabel->setText(QStringLiteral("#1e1e1e"));
        m_terminalBgColorBtn->setStyleSheet(QStringLiteral("background-color: #1e1e1e; border: 1px solid #555; border-radius: 2px;"));
        m_terminalCursorColorLabel->setText(QStringLiteral("#ffffff"));
        m_terminalCursorColorBtn->setStyleSheet(QStringLiteral("background-color: #ffffff; border: 1px solid #555; border-radius: 2px;"));
        m_terminalCursorShapeCombo->setCurrentIndex(0);  // block
        m_terminalCursorBlinkCheck->setChecked(true);
        break;
    case 3: // 智能提示
        m_completionCheck->setChecked(true);
        m_completionDelaySpin->setValue(500);
        m_minPrefixSpin->setValue(2);
        m_matchingModeCombo->setCurrentIndex(0);  // 默认模糊匹配
        break;
    case 4: // 快捷键
        onShortcutResetDefaults();
        break;
    case 5: // LSP
        m_lspPythonPathLabel->setText(tr("(自动检测 pylsp)"));
        m_lspPythonPathLabel->setToolTip(QString());
        m_lspCppPathLabel->setText(tr("(自动检测 clangd)"));
        m_lspCppPathLabel->setToolTip(QString());
        m_lspJsPathLabel->setText(tr("(自动检测)"));
        m_lspJsPathLabel->setToolTip(QString());
        m_lspAutoStartCheck->setChecked(true);
        break;
    case 6: // P1 C05-2: 构建配置
        m_buildQtPathLabel->setText(tr("(使用环境变量 CMAKE_PREFIX_PATH)"));
        m_buildQtPathLabel->setToolTip(QString());
        m_buildOpenSslPathLabel->setText(tr("(使用环境变量 OPENSSL_ROOT_DIR)"));
        m_buildOpenSslPathLabel->setToolTip(QString());
        m_buildZlibPathLabel->setText(tr("(使用环境变量 ZLIB_ROOT)"));
        m_buildZlibPathLabel->setToolTip(QString());
        m_buildTypeCombo->setCurrentIndex(0);  // Debug
        // C05-4: 构建目录与分离目录复选框
        m_buildDirLabel->setText(tr("(默认 build)"));
        m_buildDirLabel->setToolTip(QString());
        m_buildSeparateDirsCheck->setChecked(false);
        // 编译器路径与 CMake 额外参数
        m_buildCompilerCombo->setCurrentIndex(0);  // MinGW
        m_buildCompilerPathLabel->setText(tr("(使用系统默认)"));
        m_buildCompilerPathLabel->setToolTip(QString());
        m_buildCmakeArgsEdit->clear();
        break;
    case 7: // P3-M02 子项2: Markdown 自定义 CSS
        if (m_mdCssEdit) {
            m_mdCssEdit->clear();
        }
        ConfigManager::instance().setMarkdownCustomCss(QString());
        emit configChanged();
        break;
    }
}

void SettingsPage::onResetAll()
{
    int result = ModernDialog::question(this, tr("恢复默认"),
        tr("确定要恢复所有设置为默认值吗？"));
    if (result == ModernDialog::ROLE_ACCEPT) {
        for (int i = 0; i < m_categoryList->count(); ++i) {
            m_categoryList->setCurrentRow(i);
            onResetCurrentSection();
        }
        m_categoryList->setCurrentRow(0);
    }
}

void SettingsPage::filterSettings(const QString& keyword)
{
    if (keyword.isEmpty()) {
        // 显示所有分类
        for (int i = 0; i < m_categoryList->count(); ++i) {
            m_categoryList->item(i)->setHidden(false);
        }
        return;
    }

    // 简单关键词匹配：根据关键词显示对应分类
    QStringList appearanceKeywords = {tr("主题"), tr("配色"), tr("字体"), tr("外观"), tr("亮色"), tr("暗色")};
    QStringList editorKeywords = {tr("编辑"), tr("行号"), tr("缩进"), tr("保存"), tr("自动保存"), tr("拼写")};
    QStringList terminalKeywords = {tr("终端"), tr("CMD"), tr("PowerShell")};
    QStringList completionKeywords = {tr("提示"), tr("补全"), tr("智能"), tr("延迟"), tr("匹配"), tr("模糊")};
    QStringList shortcutKeywords = {tr("快捷键"), tr("快捷"), tr("热键")};
    QStringList lspKeywords = {tr("LSP"), tr("语言服务器"), tr("pylsp"), tr("clangd"), tr("补全"), tr("诊断")};
    QStringList buildKeywords = {tr("构建"), tr("CMake"), tr("Qt"), tr("OpenSSL"), tr("zlib"), tr("Debug"), tr("Release")};
    QStringList markdownKeywords = {tr("Markdown"), tr("CSS"), tr("样式"), tr("预览"), tr("mermaid")};

    // Bug5: 批量更新 — 禁用更新期间的重绘，避免逐项 setHidden 触发多次布局/重绘导致白屏
    m_categoryList->setUpdatesEnabled(false);

    for (int i = 0; i < m_categoryList->count(); ++i) {
        bool match = false;
        QStringList* keywords = nullptr;
        switch (i) {
        case 0: keywords = &appearanceKeywords; break;
        case 1: keywords = &editorKeywords; break;
        case 2: keywords = &terminalKeywords; break;
        case 3: keywords = &completionKeywords; break;
        case 4: keywords = &shortcutKeywords; break;
        case 5: keywords = &lspKeywords; break;
        case 6: keywords = &buildKeywords; break;
        case 7: keywords = &markdownKeywords; break;
        }

        if (keywords) {
            for (const auto& kw : *keywords) {
                if (kw.contains(keyword, Qt::CaseInsensitive) || keyword.contains(kw, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        m_categoryList->item(i)->setHidden(!match);
    }

    m_categoryList->setUpdatesEnabled(true);
    m_categoryList->update();
}

void SettingsPage::onExportConfig()
{
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("导出配置"),
        QString(), tr("JSON 文件 (*.json)")
    );
    if (filePath.isEmpty()) return;

    auto& config = ConfigManager::instance();
    QJsonObject root;

    // 外观
    QJsonObject appearance;
    appearance[QStringLiteral("theme")] = config.theme();
    appearance[QStringLiteral("fontSize")] = config.fontSize();
    root[QStringLiteral("appearance")] = appearance;

    // 编辑器
    QJsonObject editor;
    editor[QStringLiteral("autoSave")] = config.autoSave();
    editor[QStringLiteral("showLineNumbers")] = config.showLineNumbers();
    editor[QStringLiteral("showCompletion")] = config.showCompletion();
    editor[QStringLiteral("tabSize")] = config.getValue("Editor/tabSize", 4).toInt();
    root[QStringLiteral("editor")] = editor;

    // 终端
    QJsonObject terminal;
    terminal[QStringLiteral("type")] = config.getValue("Terminal/type", QStringLiteral("cmd")).toString();
    terminal[QStringLiteral("fontSize")] = config.getValue("Terminal/fontSize", 12).toInt();
    terminal[QStringLiteral("fontFamily")] = config.getValue("Terminal/fontFamily", QStringLiteral("Consolas")).toString();
    terminal[QStringLiteral("fgColor")] = config.getValue("Terminal/fgColor", QStringLiteral("#cccccc")).toString();
    terminal[QStringLiteral("bgColor")] = config.getValue("Terminal/bgColor", QStringLiteral("#1e1e1e")).toString();
    terminal[QStringLiteral("cursorColor")] = config.getValue("Terminal/cursorColor", QStringLiteral("#ffffff")).toString();
    terminal[QStringLiteral("cursorShape")] = config.getValue("Terminal/cursorShape", QStringLiteral("block")).toString();
    terminal[QStringLiteral("cursorBlink")] = config.getValue("Terminal/cursorBlink", true).toBool();
    root[QStringLiteral("terminal")] = terminal;

    // 智能提示
    QJsonObject completion;
    completion[QStringLiteral("delay")] = config.getValue("Completion/delay", 500).toInt();
    completion[QStringLiteral("minPrefix")] = config.getValue("Completion/minPrefix", 2).toInt();
    completion[QStringLiteral("matchingMode")] = config.getValue("Completion/matchingMode", QStringLiteral("fuzzy")).toString();
    root[QStringLiteral("completion")] = completion;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        ModernDialog::information(this, tr("导出成功"), tr("配置已导出到：\n%1").arg(filePath));
    } else {
        ModernDialog::warning(this, tr("导出失败"), tr("无法写入文件：\n%1").arg(filePath));
    }
}

void SettingsPage::onImportConfig()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入配置"),
        QString(), tr("JSON 文件 (*.json)")
    );
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ModernDialog::warning(this, tr("导入失败"), tr("无法读取文件：\n%1").arg(filePath));
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError || doc.isNull()) {
        ModernDialog::warning(this, tr("导入失败"), tr("JSON 解析错误：\n%1").arg(error.errorString()));
        return;
    }

    int result = ModernDialog::question(this, tr("确认导入"),
        tr("导入配置将覆盖当前设置，是否继续？"));
    if (result != ModernDialog::ROLE_ACCEPT) return;

    auto& config = ConfigManager::instance();
    QJsonObject root = doc.object();

    // 外观
    if (root.contains(QStringLiteral("appearance"))) {
        QJsonObject appearance = root[QStringLiteral("appearance")].toObject();
        if (appearance.contains(QStringLiteral("theme"))) {
            QString theme = appearance[QStringLiteral("theme")].toString();
            config.setTheme(theme);
            ThemeManager::instance().switchTheme(theme);
            int themeIdx = m_themeCombo->findData(theme);
            if (themeIdx >= 0) m_themeCombo->setCurrentIndex(themeIdx);
        }
        if (appearance.contains(QStringLiteral("fontSize"))) {
            int size = appearance[QStringLiteral("fontSize")].toInt();
            config.setFontSize(size);
            m_fontSizeSpin->setValue(size);
            emit fontSizeChanged(size);
        }
    }

    // 编辑器
    if (root.contains(QStringLiteral("editor"))) {
        QJsonObject editor = root[QStringLiteral("editor")].toObject();
        if (editor.contains(QStringLiteral("autoSave"))) {
            bool autoSave = editor[QStringLiteral("autoSave")].toBool();
            config.setAutoSave(autoSave);
            m_autoSaveCheck->setChecked(autoSave);
        }
        if (editor.contains(QStringLiteral("showLineNumbers"))) {
            bool show = editor[QStringLiteral("showLineNumbers")].toBool();
            config.setShowLineNumbers(show);
            m_lineNumbersCheck->setChecked(show);
        }
        if (editor.contains(QStringLiteral("showCompletion"))) {
            bool show = editor[QStringLiteral("showCompletion")].toBool();
            config.setShowCompletion(show);
            m_completionCheck->setChecked(show);
        }
        if (editor.contains(QStringLiteral("tabSize"))) {
            int tabSize = editor[QStringLiteral("tabSize")].toInt();
            config.setValue("Editor/tabSize", tabSize);
            m_tabSizeSpin->setValue(tabSize);
        }
    }

    // 终端
    if (root.contains(QStringLiteral("terminal"))) {
        QJsonObject terminal = root[QStringLiteral("terminal")].toObject();
        if (terminal.contains(QStringLiteral("type"))) {
            QString type = terminal[QStringLiteral("type")].toString();
            config.setValue("Terminal/type", type);
            int termIdx = m_terminalTypeCombo->findData(type);
            if (termIdx >= 0) m_terminalTypeCombo->setCurrentIndex(termIdx);
        }
        if (terminal.contains(QStringLiteral("fontSize"))) {
            int fontSize = terminal[QStringLiteral("fontSize")].toInt();
            config.setValue("Terminal/fontSize", fontSize);
            m_terminalFontSpin->setValue(fontSize);
        }
        if (terminal.contains(QStringLiteral("fontFamily"))) {
            QString family = terminal[QStringLiteral("fontFamily")].toString();
            config.setValue("Terminal/fontFamily", family);
            int familyIdx = m_terminalFontFamilyCombo->findData(family);
            if (familyIdx >= 0) m_terminalFontFamilyCombo->setCurrentIndex(familyIdx);
        }
        if (terminal.contains(QStringLiteral("fgColor"))) {
            QString color = terminal[QStringLiteral("fgColor")].toString();
            config.setValue("Terminal/fgColor", color);
            m_terminalFgColorLabel->setText(color);
            m_terminalFgColorBtn->setStyleSheet(
                QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(color));
        }
        if (terminal.contains(QStringLiteral("bgColor"))) {
            QString color = terminal[QStringLiteral("bgColor")].toString();
            config.setValue("Terminal/bgColor", color);
            m_terminalBgColorLabel->setText(color);
            m_terminalBgColorBtn->setStyleSheet(
                QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(color));
        }
        if (terminal.contains(QStringLiteral("cursorColor"))) {
            QString color = terminal[QStringLiteral("cursorColor")].toString();
            config.setValue("Terminal/cursorColor", color);
            m_terminalCursorColorLabel->setText(color);
            m_terminalCursorColorBtn->setStyleSheet(
                QStringLiteral("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(color));
        }
        if (terminal.contains(QStringLiteral("cursorShape"))) {
            QString shape = terminal[QStringLiteral("cursorShape")].toString();
            config.setValue("Terminal/cursorShape", shape);
            int shapeIdx = m_terminalCursorShapeCombo->findData(shape);
            if (shapeIdx >= 0) m_terminalCursorShapeCombo->setCurrentIndex(shapeIdx);
        }
        if (terminal.contains(QStringLiteral("cursorBlink"))) {
            bool blink = terminal[QStringLiteral("cursorBlink")].toBool();
            config.setValue("Terminal/cursorBlink", blink);
            m_terminalCursorBlinkCheck->setChecked(blink);
        }
    }

    // 智能提示
    if (root.contains(QStringLiteral("completion"))) {
        QJsonObject completion = root[QStringLiteral("completion")].toObject();
        if (completion.contains(QStringLiteral("delay"))) {
            int delay = completion[QStringLiteral("delay")].toInt();
            config.setValue("Completion/delay", delay);
            m_completionDelaySpin->setValue(delay);
        }
        if (completion.contains(QStringLiteral("minPrefix"))) {
            int minPrefix = completion[QStringLiteral("minPrefix")].toInt();
            config.setValue("Completion/minPrefix", minPrefix);
            m_minPrefixSpin->setValue(minPrefix);
        }
        if (completion.contains(QStringLiteral("matchingMode"))) {
            QString mode = completion[QStringLiteral("matchingMode")].toString();
            config.setValue("Completion/matchingMode", mode);
            int matchIdx = m_matchingModeCombo->findData(mode);
            if (matchIdx >= 0) m_matchingModeCombo->setCurrentIndex(matchIdx);
        }
    }

    emit configChanged();
    ModernDialog::information(this, tr("导入成功"), tr("配置已成功导入"));
}
