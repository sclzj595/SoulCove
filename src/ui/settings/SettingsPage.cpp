#include "ui/settings/SettingsPage.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"
#include "core/shortcut/ShortcutManager.h"  // T7: 快捷键管理器

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include "ui/dialog/ModernDialog.h"
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QColorDialog>
#include <QDialog>
#include <QKeyEvent>
#include <QHeaderView>

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

    m_categoryList->addItem(tr("外观"));
    m_categoryList->addItem(tr("编辑器"));
    m_categoryList->addItem(tr("终端"));
    m_categoryList->addItem(tr("智能提示"));
    m_categoryList->addItem(tr("快捷键"));
    m_categoryList->addItem(tr("LSP 语言服务器"));
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

    m_pageStack->addWidget(appearancePage);
    m_pageStack->addWidget(editorPage);
    m_pageStack->addWidget(terminalPage);
    m_pageStack->addWidget(completionPage);

    // 快捷键页面（独立创建，返回 QWidget 指针）
    // 注意：添加顺序必须与 m_categoryList 的项目顺序一致
    // 分类列表顺序：外观(0) 编辑器(1) 终端(2) 智能提示(3) 快捷键(4) LSP(5)
    createShortcutsPage();

    m_pageStack->addWidget(lspPage);

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

    auto* hintLabel = new QLabel(tr("查看和自定义所有快捷键，系统自动检测冲突"), page);
    hintLabel->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(hintLabel);

    // 搜索框
    m_shortcutSearchInput = new QLineEdit(page);
    m_shortcutSearchInput->setPlaceholderText(tr("搜索快捷键..."));
    m_shortcutSearchInput->setObjectName(QStringLiteral("searchInput"));
    layout->addWidget(m_shortcutSearchInput);

    // 表格
    m_shortcutTable = new QTableWidget(page);
    m_shortcutTable->setColumnCount(4);
    m_shortcutTable->setHorizontalHeaderLabels({
        tr("命令名称"), tr("当前快捷键"), tr("分类"), tr("操作")
    });
    m_shortcutTable->horizontalHeader()->setStretchLastSection(true);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_shortcutTable->setColumnWidth(3, 80);
    m_shortcutTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_shortcutTable->setAlternatingRowColors(true);

    // 表格样式跟随主题
    const auto& p = ThemeManager::instance().currentPalette();
    QString bg = p.bgDialog.name(QColor::HexRgb);
    QString fg = p.fgPrimary.name(QColor::HexRgb);
    QString border = p.borderDefault.name(QColor::HexRgb);
    QString altBg = p.bgHover.name(QColor::HexRgb);
    QString headerBg = p.bgTitleBar.name(QColor::HexRgb);
    QString accent = p.accentPrimary.name(QColor::HexArgb);

    m_shortcutTable->setStyleSheet(
        QStringLiteral("QTableWidget { background-color: %1; alternate-background-color: %2;"
                       " color: %3; gridline-color: %4; border: 1px solid %4; border-radius: 4px; }")
            .arg(bg, altBg, fg, border)
        + QStringLiteral("QTableWidget::item { padding: 4px 8px; }")
        + QStringLiteral("QTableWidget::item:selected { background-color: %1; color: #ffffff; }")
            .arg(accent)
        + QStringLiteral("QHeaderView::section { background-color: %1; color: %2;"
                       " padding: 6px 8px; border: 1px solid %3; border-bottom: 2px solid %4; font-weight: bold; }")
            .arg(headerBg, fg, border, accent)
        + QStringLiteral("QPushButton { background-color: %1; color: white; border: 1px solid %1;"
                       " padding: 3px 12px; border-radius: 3px; min-width: 50px; }")
            .arg(accent)
        + QStringLiteral("QPushButton:hover { background-color: %1; }")
            .arg(p.accentHover.name(QColor::HexArgb))
        + QStringLiteral("QPushButton:pressed { background-color: %1; }")
            .arg(p.selectionBg.name(QColor::HexArgb))
    );

    layout->addWidget(m_shortcutTable, 1);

    // 初始化快捷键数据（从 ShortcutManager 加载）
    auto& shortcutMgr = ShortcutManager::instance();
    if (!shortcutMgr.allShortcuts().isEmpty()) {
        // 使用 ShortcutManager 的数据
        m_shortcutItems.clear();
        for (const auto& item : shortcutMgr.allShortcuts()) {
            ShortcutItem uiItem;
            uiItem.commandName = item.displayName;
            uiItem.keySequence = item.currentKey.toString();
            uiItem.category = item.category;
            uiItem.defaultKey = item.defaultKey.toString();
            m_shortcutItems.append(uiItem);
        }
    } else {
        // Fallback：硬编码默认值（兼容旧版本）
        m_shortcutItems = {
            {tr("打开文件"),          QStringLiteral("Ctrl+O"),        tr("文件"),   QStringLiteral("Ctrl+O")},
            {tr("新建文件"),          QStringLiteral("Ctrl+N"),        tr("文件"),   QStringLiteral("Ctrl+N")},
            {tr("保存文件"),          QStringLiteral("Ctrl+S"),        tr("文件"),   QStringLiteral("Ctrl+S")},
            {tr("另存为"),            QStringLiteral("Ctrl+Shift+S"),  tr("文件"),   QStringLiteral("Ctrl+Shift+S")},
            {tr("撤销"),              QStringLiteral("Ctrl+Z"),        tr("编辑"),   QStringLiteral("Ctrl+Z")},
            {tr("重做"),              QStringLiteral("Ctrl+Y"),        tr("编辑"),   QStringLiteral("Ctrl+Y")},
            {tr("查找"),              QStringLiteral("Ctrl+F"),        tr("编辑"),   QStringLiteral("Ctrl+F")},
            {tr("替换"),              QStringLiteral("Ctrl+H"),        tr("编辑"),   QStringLiteral("Ctrl+H")},
            {tr("切换侧边栏"),        QStringLiteral("Ctrl+B"),        tr("视图"),   QStringLiteral("Ctrl+B")},
            {tr("切换终端"),          QStringLiteral("Ctrl+`"),        tr("终端"),   QStringLiteral("Ctrl+`")},
            {tr("新建终端"),          QStringLiteral("Ctrl+Shift+`"),  tr("终端"),   QStringLiteral("Ctrl+Shift+`")},
            {tr("清屏"),              QStringLiteral("Ctrl+L"),        tr("终端"),   QStringLiteral("Ctrl+L")},
            {tr("命令面板"),          QStringLiteral("Ctrl+Shift+P"),  tr("其他"),   QStringLiteral("Ctrl+Shift+P")},
            {tr("切换主题"),          QStringLiteral("Ctrl+Shift+T"),  tr("其他"),   QStringLiteral("Ctrl+Shift+T")},
            {tr("打开设置"),          QStringLiteral("Ctrl+,"),        tr("设置"),   QStringLiteral("Ctrl+,")},
            {tr("关闭标签"),          QStringLiteral("Ctrl+W"),        tr("文件"),   QStringLiteral("Ctrl+W")},
            {tr("全选"),              QStringLiteral("Ctrl+A"),        tr("编辑"),   QStringLiteral("Ctrl+A")},
            {tr("复制"),              QStringLiteral("Ctrl+C"),        tr("编辑"),   QStringLiteral("Ctrl+C")},
            {tr("粘贴"),              QStringLiteral("Ctrl+V"),        tr("编辑"),   QStringLiteral("Ctrl+V")},
            {tr("剪切"),              QStringLiteral("Ctrl+X"),        tr("编辑"),   QStringLiteral("Ctrl+X")},
        };
    }

    // 填充表格数据
    refreshShortcutTable(QString());

    // 底部操作按钮区
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);

    auto* btnReset = new QPushButton(tr("恢复默认"), page);
    btnReset->setObjectName(QStringLiteral("btnResetSection"));
    btnLayout->addWidget(btnReset);

    auto* btnExport = new QPushButton(tr("导出快捷键"), page);
    btnExport->setObjectName(QStringLiteral("btnExportConfig"));
    btnLayout->addWidget(btnExport);

    auto* btnImport = new QPushButton(tr("导入快捷键"), page);
    btnImport->setObjectName(QStringLiteral("btnImportConfig"));
    btnLayout->addWidget(btnImport);

    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    // 信号连接
    connect(m_shortcutSearchInput, &QLineEdit::textChanged,
            this, &SettingsPage::onShortcutSearchChanged);
    connect(btnReset, &QPushButton::clicked,
            this, &SettingsPage::onShortcutResetDefaults);
    connect(btnExport, &QPushButton::clicked,
            this, &SettingsPage::onShortcutExport);
    connect(btnImport, &QPushButton::clicked,
            this, &SettingsPage::onShortcutImport);

    // 添加到页面堆栈
    m_pageStack->addWidget(page);
}

void SettingsPage::refreshShortcutTable(const QString& filter)
{
    m_shortcutTable->setRowCount(0);

    for (int i = 0; i < m_shortcutItems.size(); ++i) {
        const auto& item = m_shortcutItems[i];

        // 搜索过滤
        if (!filter.isEmpty()) {
            bool match = item.commandName.contains(filter, Qt::CaseInsensitive)
                      || item.keySequence.contains(filter, Qt::CaseInsensitive)
                      || item.category.contains(filter, Qt::CaseInsensitive);
            if (!match) continue;
        }

        int row = m_shortcutTable->rowCount();
        m_shortcutTable->insertRow(row);

        // 命令名称
        auto* nameItem = new QTableWidgetItem(item.commandName);
        nameItem->setData(Qt::UserRole, i);  // 存储原始索引
        m_shortcutTable->setItem(row, 0, nameItem);

        // 当前快捷键（检查是否冲突）
        auto* keyItem = new QTableWidgetItem(item.keySequence);
        // 冲突检测：高亮重复的快捷键
        bool hasConflict = false;
        for (int j = 0; j < m_shortcutItems.size(); ++j) {
            if (j != i && m_shortcutItems[j].keySequence == item.keySequence) {
                hasConflict = true;
                break;
            }
        }
        if (hasConflict) {
            keyItem->setForeground(QColor("#ff4444"));  // 红色高亮冲突
        }
        m_shortcutTable->setItem(row, 1, keyItem);

        // 分类
        m_shortcutTable->setItem(row, 2, new QTableWidgetItem(item.category));

        // 操作按钮
        auto* modifyBtn = new QPushButton(tr("修改"), m_shortcutTable);
        modifyBtn->setProperty("row", row);
        modifyBtn->setProperty("dataIndex", i);
        connect(modifyBtn, &QPushButton::clicked, this, [this, idx = i]() {
            onShortcutModifyClicked(idx);
        });
        m_shortcutTable->setCellWidget(row, 3, modifyBtn);
    }
}

void SettingsPage::onShortcutModifyClicked(int dataIndex)
{
    if (dataIndex < 0 || dataIndex >= m_shortcutItems.size()) return;

    ShortcutCaptureDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || dialog.capturedSequence().isEmpty()) {
        return;
    }

    QString newSeq = dialog.capturedSequence();

    // 使用 ShortcutManager 进行冲突检测和修改（T7增强）
    auto& shortcutMgr = ShortcutManager::instance();
    QStringList conflicts = shortcutMgr.checkConflict(QKeySequence(newSeq));

    if (!conflicts.isEmpty()) {
        // 获取冲突命令的显示名称
        QString conflictNames;
        for (const auto& conflictId : conflicts) {
            auto item = shortcutMgr.shortcut(conflictId);
            if (!conflictNames.isEmpty()) conflictNames += QStringLiteral(", ");
            conflictNames += item.displayName;
        }

        int result = ModernDialog::warning(this, tr("快捷键冲突"),
            tr("该快捷键已被「%1」使用，是否覆盖？").arg(conflictNames));
        if (result != ModernDialog::ROLE_ACCEPT) return;
    }

    // 更新本地UI数据
    m_shortcutItems[dataIndex].keySequence = newSeq;

    // 同步到 ShortcutManager（通过ID查找，这里简化处理）
    // 实际应该存储ID映射，当前使用命令名称匹配
    for (auto& mgrItem : shortcutMgr.allShortcuts()) {
        if (mgrItem.displayName == m_shortcutItems[dataIndex].commandName) {
            shortcutMgr.setShortcut(mgrItem.id, QKeySequence(newSeq));
            break;
        }
    }

    refreshShortcutTable(m_shortcutSearchInput->text());
}

void SettingsPage::onShortcutSearchChanged(const QString& text)
{
    refreshShortcutTable(text.trimmed());
}

void SettingsPage::onShortcutResetDefaults()
{
    int result = ModernDialog::question(this, tr("恢复默认"),
        tr("确定要将所有快捷键恢复为默认值吗？"));
    if (result != ModernDialog::ROLE_ACCEPT) return;

    // 使用 ShortcutManager 重置（T7增强）
    auto& shortcutMgr = ShortcutManager::instance();
    shortcutMgr.resetAllToDefault();

    // 刷新UI数据
    m_shortcutItems.clear();
    for (const auto& item : shortcutMgr.allShortcuts()) {
        ShortcutItem uiItem;
        uiItem.commandName = item.displayName;
        uiItem.keySequence = item.currentKey.toString();
        uiItem.category = item.category;
        uiItem.defaultKey = item.defaultKey.toString();
        m_shortcutItems.append(uiItem);
    }

    refreshShortcutTable(m_shortcutSearchInput->text());
}

void SettingsPage::onShortcutExport()
{
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("导出快捷键"),
        QString(), tr("JSON 文件 (*.json)")
    );
    if (filePath.isEmpty()) return;

    QJsonArray arr;
    for (const auto& item : m_shortcutItems) {
        QJsonObject obj;
        obj[QStringLiteral("command")] = item.commandName;
        obj[QStringLiteral("keySequence")] = item.keySequence;
        obj[QStringLiteral("category")] = item.category;
        obj[QStringLiteral("default")] = item.defaultKey;
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        ModernDialog::information(this, tr("导出成功"), tr("快捷键配置已导出到：\n%1").arg(filePath));
    } else {
        ModernDialog::warning(this, tr("导出失败"), tr("无法写入文件：\n%1").arg(filePath));
    }
}

void SettingsPage::onShortcutImport()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入快捷键"),
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

    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        ModernDialog::warning(this, tr("导入失败"), tr("JSON 解析错误或格式不正确"));
        return;
    }

    QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        QString cmd = obj.value(QStringLiteral("command")).toString();
        QString seq = obj.value(QStringLiteral("keySequence")).toString();

        for (auto& item : m_shortcutItems) {
            if (item.commandName == cmd) {
                item.keySequence = seq;
                break;
            }
        }
    }

    refreshShortcutTable(m_shortcutSearchInput->text());
    ModernDialog::information(this, tr("导入成功"), tr("快捷键配置已成功导入"));
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
    ConfigManager::instance().setValue(QStringLiteral("app/language"), lang);
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

void SettingsPage::onCategoryChanged(int row)
{
    if (row >= 0 && row < m_pageStack->count()) {
        m_pageStack->setCurrentIndex(row);
    }
}

void SettingsPage::onSearchTextChanged(const QString& text)
{
    filterSettings(text.trimmed());
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
    }
}

void SettingsPage::onResetAll()
{
    int result = ModernDialog::question(this, tr("恢复默认"),
        tr("确定要恢复所有设置为默认值吗？"));
    if (result == ModernDialog::ROLE_ACCEPT) {
        for (int i = 0; i < 6; ++i) {
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
    QStringList editorKeywords = {tr("编辑"), tr("行号"), tr("缩进"), tr("保存"), tr("自动保存")};
    QStringList terminalKeywords = {tr("终端"), tr("CMD"), tr("PowerShell")};
    QStringList completionKeywords = {tr("提示"), tr("补全"), tr("智能"), tr("延迟"), tr("匹配"), tr("模糊")};
    QStringList shortcutKeywords = {tr("快捷键"), tr("快捷"), tr("热键")};
    QStringList lspKeywords = {tr("LSP"), tr("语言服务器"), tr("pylsp"), tr("clangd"), tr("补全"), tr("诊断")};

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
