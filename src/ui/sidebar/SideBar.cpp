#include "ui/sidebar/SideBar.h"
#include "Logger.hpp"
#include "ui/sidebar/GitPanel.h"
#include "ui/sidebar/TasksPanel.h"    // M15: 任务面板（已抽出）
#include "ui/sidebar/SearchPanel.h"   // 搜索面板（已抽出）
#include "ui/sidebar/ExplorerPanel.h" // 资源管理器面板（已抽出）

#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QFileDialog>

SideBar::SideBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sideBar"));
    // 不再固定宽度，由外层 QSplitter 控制拖拽调整
    setMinimumWidth(160);
    setMaximumWidth(600);   // 允许拖到很宽

    // === 主布局：图标栏(48px) + 内容面板 ===
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    // === 活动图标栏（左侧窄条）===
    m_activityBar = new QWidget(this);
    m_activityBar->setObjectName(QStringLiteral("activityBar"));
    m_activityBar->setFixedWidth(48);

    m_activityLayout = new QVBoxLayout(m_activityBar);
    m_activityLayout->setContentsMargins(4, 8, 4, 8);
    m_activityLayout->setSpacing(2);

    // 使用 QString::fromUtf8() 显式指定 UTF-8 编码，确保 Windows/Qt 正确显示 emoji
    m_btnExplorer   = createActivityBtn(QString::fromUtf8("\xF0\x9F\x81\x81"), Activity::Explorer,   tr("资源管理器"));  // 📁
    m_btnSearch     = createActivityBtn(QString::fromUtf8("\xF0\x9F\x94\x8D"), Activity::Search,     tr("搜索"));         // 🔍
    m_btnGit        = createActivityBtn(QString::fromUtf8("\xE2\x9C\x8D"), Activity::Git,        tr("源代码管理"));    // ✍
    m_btnTasks     = createActivityBtn(QString::fromUtf8("\xE2\x9A\x92"), Activity::Tasks,      tr("任务"));          // ⚙ (任务图标)

    // 终端按钮（独立动作按钮，不切换面板，直接触发信号）
    m_btnTerminal = new QPushButton(QString::fromUtf8("\xE2\x96\xBA"), m_activityBar);  // ▶ (终端图标)
    m_btnTerminal->setFixedSize(40, 40);
    m_btnTerminal->setCursor(Qt::PointingHandCursor);
    m_btnTerminal->setObjectName(QStringLiteral("activityBtn"));
    m_btnTerminal->setToolTip(tr("切换终端 (Ctrl+`)"));
    m_btnTerminal->setCheckable(true);
    connect(m_btnTerminal, &QPushButton::clicked, this, &SideBar::onTerminalClicked);

    m_btnExplorer->setChecked(true);
    m_btnExplorer->setProperty("active", true);

    m_activityLayout->addWidget(m_btnExplorer);
    m_activityLayout->addWidget(m_btnSearch);
    m_activityLayout->addWidget(m_btnGit);
    m_activityLayout->addWidget(m_btnTasks);       // M15: 任务按钮
    m_activityLayout->addWidget(m_btnTerminal);
    m_activityLayout->addStretch();

    // === 右侧面板堆栈 ===
    m_panelWidget = new QWidget(this);
    m_panelWidget->setObjectName(QStringLiteral("sidePanel"));
    auto* panelOuterLayout = new QVBoxLayout(m_panelWidget);
    panelOuterLayout->setContentsMargins(0, 0, 0, 0);
    panelOuterLayout->setSpacing(0);

    m_panelStack = new QStackedWidget(m_panelWidget);

    // --- Explorer 面板（已抽出为 ExplorerPanel）---
    m_explorerPanel = new ExplorerPanel(this);
    // 转发 ExplorerPanel 的所有信号到 SideBar 的信号（供 Widget 连接）
    connect(m_explorerPanel, &ExplorerPanel::fileOpenRequested,
            this, &SideBar::fileOpenRequested);
    connect(m_explorerPanel, &ExplorerPanel::fileCreateRequested,
            this, &SideBar::fileCreateRequested);
    connect(m_explorerPanel, &ExplorerPanel::folderCreateRequested,
            this, &SideBar::folderCreateRequested);
    connect(m_explorerPanel, &ExplorerPanel::fileMoveRequested,
            this, &SideBar::fileMoveRequested);
    connect(m_explorerPanel, &ExplorerPanel::fileDeleteRequested,
            this, &SideBar::fileDeleteRequested);
    connect(m_explorerPanel, &ExplorerPanel::fileRenameRequested,
            this, &SideBar::fileRenameRequested);
    connect(m_explorerPanel, &ExplorerPanel::openInFolderRequested,
            this, &SideBar::openInFolderRequested);
    connect(m_explorerPanel, &ExplorerPanel::addFolderToWorkspaceRequested,
            this, &SideBar::addFolderToWorkspaceRequested);
    connect(m_explorerPanel, &ExplorerPanel::removeWorkspaceFolderRequested,
            this, &SideBar::removeWorkspaceFolder);
    connect(m_explorerPanel, &ExplorerPanel::openFolderClicked,
            this, &SideBar::onExplorerOpenFolderClicked);
    // V2.1: 转发 ExplorerPanel 内嵌大纲的符号点击信号（供 Widget 跳转）
    connect(m_explorerPanel, &ExplorerPanel::outlineSymbolClicked,
            this, &SideBar::outlineSymbolClicked);
    m_panelStack->addWidget(m_explorerPanel);

    // --- Search 面板（已抽出为 SearchPanel）---
    m_searchPanel = new SearchPanel(this);
    // 转发搜索面板的信号到 SideBar 的信号（供 Widget 连接）
    connect(m_searchPanel, &SearchPanel::fileOpenRequested,
            this, &SideBar::fileOpenRequested);
    connect(m_searchPanel, &SearchPanel::locateRequested,
            this, [this](const QString& filePath, int line, int col) {
        // 搜索结果的定位复用 outlineSymbolClicked 信号（Widget 层已连接跳转逻辑）
        // V2.1: 搜索结果无结束位置，传 -1 表示仅定位不选中块
        emit outlineSymbolClicked(filePath, line, col, -1, -1);
    });
    m_panelStack->addWidget(m_searchPanel);

    // --- Git 面板（源代码管理）---
    m_gitPanel = new QWidget();
    auto* gitLayout = new QVBoxLayout(m_gitPanel);
    gitLayout->setContentsMargins(0, 0, 0, 0);
    gitLayout->setSpacing(0);

    m_gitPanelWidget = new GitPanel(m_gitPanel);
    gitLayout->addWidget(m_gitPanelWidget);

    m_panelStack->addWidget(m_gitPanel);

    // --- Tasks 面板（M15: 任务系统，已抽出为 TasksPanel）---
    m_tasksPanel = new TasksPanel(this);
    m_panelStack->addWidget(m_tasksPanel);

    panelOuterLayout->addWidget(m_panelStack);

    // 填充文件树（ExplorerPanel 内部处理）
    if (m_explorerPanel) m_explorerPanel->refreshFileList();

    // === 组装 ===
    m_mainLayout->addWidget(m_activityBar);
    m_mainLayout->addWidget(m_panelWidget);

    // 监听主题切换
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &SideBar::refreshActivityStyles);

    // 注：Explorer/Search/Tasks/Outline 面板的信号连接由各自面板内部处理，
    //     SideBar 构造时已转发所有必要信号
}

QPushButton* SideBar::createActivityBtn(const QString& iconText, Activity activity, const QString& tooltip)
{
    auto* btn = new QPushButton(iconText, m_activityBar);
    btn->setFixedSize(40, 40);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setCheckable(true);
    btn->setToolTip(tooltip);
    btn->setProperty("activity", static_cast<int>(activity));
    // 使用支持 emoji 的字体（Windows: Segoe UI Emoji / macOS: Apple Color Emoji）
    QFont emojiFont = btn->font();
    emojiFont.setFamilies({QStringLiteral("Segoe UI Emoji"), QStringLiteral("Apple Color Emoji"), QStringLiteral("Noto Color Emoji")});
    btn->setFont(emojiFont);

    connect(btn, &QPushButton::clicked, this, &SideBar::onActivityButtonClicked);
    return btn;
}

void SideBar::switchToActivity(Activity activity)
{
    m_currentActivity = activity;

    for (auto* btn : {m_btnExplorer, m_btnSearch, m_btnGit, m_btnTasks}) {
        bool isTarget = (btn->property("activity").toInt() == static_cast<int>(activity));
        btn->setChecked(isTarget);
        btn->setProperty("active", isTarget);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }

    // 切换面板堆栈
    switch (activity) {
    case Activity::Explorer:   m_panelStack->setCurrentWidget(m_explorerPanel);   break;
    case Activity::Search:     m_panelStack->setCurrentWidget(m_searchPanel);     break;
    case Activity::Git:        m_panelStack->setCurrentWidget(m_gitPanel);        break;
    case Activity::Tasks:     m_panelStack->setCurrentWidget(m_tasksPanel);      break;
    }
}

void SideBar::setPanelWidth(int width)
{
    setFixedWidth(width);
    m_panelWidget->setMinimumWidth(width - 50);
}

void SideBar::refreshFileList()
{
    // ISideFileBar 接口实现 — 委托给 ExplorerPanel
    if (m_explorerPanel) m_explorerPanel->refreshFileList();
}

void SideBar::setWorkDirectory(const QString& dirPath)
{
    // 兼容旧接口：清空工作区并设置单个文件夹
    m_workspaceFolders.clear();
    if (!dirPath.isEmpty()) {
        m_workspaceFolders.append(dirPath);
    }
    m_workDir = dirPath;
    if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    if (m_explorerPanel) m_explorerPanel->setWorkspaceFolders(m_workspaceFolders);
    emit workspaceFoldersChanged(m_workspaceFolders);
}

bool SideBar::addWorkspaceFolder(const QString& dirPath)
{
    if (dirPath.isEmpty()) return false;

    QString absPath = QDir(dirPath).absolutePath();
    // 去重
    if (m_workspaceFolders.contains(absPath)) {
        return false;
    }

    m_workspaceFolders.append(absPath);
    // 兼容 m_workDir（保持为第一个文件夹）
    if (m_workspaceFolders.size() == 1) {
        m_workDir = absPath;
        if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    }
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    if (m_explorerPanel) m_explorerPanel->setWorkspaceFolders(m_workspaceFolders);
    emit workspaceFoldersChanged(m_workspaceFolders);
    return true;
}

void SideBar::removeWorkspaceFolder(int index)
{
    if (index < 0 || index >= m_workspaceFolders.size()) return;

    m_workspaceFolders.removeAt(index);
    // 更新 m_workDir
    if (m_workspaceFolders.isEmpty()) {
        m_workDir.clear();
    } else {
        m_workDir = m_workspaceFolders.first();
    }
    if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    if (m_explorerPanel) m_explorerPanel->setWorkspaceFolders(m_workspaceFolders);
    emit workspaceFoldersChanged(m_workspaceFolders);
}

void SideBar::clearWorkspace()
{
    m_workspaceFolders.clear();
    m_workDir.clear();
    if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    if (m_explorerPanel) m_explorerPanel->setWorkspaceFolders(m_workspaceFolders);
    emit workspaceFoldersChanged(m_workspaceFolders);
}

// ============================================================
// P2-H04: 多文件夹工作区切换/移除（按路径操作）
// ============================================================

void SideBar::setWorkspaceFolders(const QStringList& folders)
{
    // 切换工作区：清空并重建文件树（去重 + 规范化路径）
    m_workspaceFolders.clear();
    for (const QString& f : folders) {
        if (f.isEmpty()) continue;
        QString abs = QDir(f).absolutePath();
        if (!m_workspaceFolders.contains(abs)) {
            m_workspaceFolders.append(abs);
        }
    }
    // 兼容 m_workDir（保持为第一个文件夹）
    m_workDir = m_workspaceFolders.isEmpty() ? QString() : m_workspaceFolders.first();
    if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    // ExplorerPanel::setWorkspaceFolders 内部会调用 refreshFileList 重建文件树
    if (m_explorerPanel) m_explorerPanel->setWorkspaceFolders(m_workspaceFolders);
    emit workspaceFoldersChanged(m_workspaceFolders);
    LOG_DEBUG("[SideBar] 工作区已切换，共 " << m_workspaceFolders.size() << " 个文件夹");
}

bool SideBar::removeWorkspaceFolderByPath(const QString& folder)
{
    if (folder.isEmpty()) return false;
    QString abs = QDir(folder).absolutePath();
    if (!m_workspaceFolders.contains(abs)) {
        return false;
    }
    m_workspaceFolders.removeAll(abs);
    // 更新 m_workDir
    m_workDir = m_workspaceFolders.isEmpty() ? QString() : m_workspaceFolders.first();
    if (m_tasksPanel) m_tasksPanel->setWorkDirectory(m_workDir);
    if (m_searchPanel) m_searchPanel->setWorkspaceFolders(m_workspaceFolders);
    if (m_explorerPanel) m_explorerPanel->setWorkspaceFolders(m_workspaceFolders);
    emit workspaceFoldersChanged(m_workspaceFolders);
    LOG_DEBUG("[SideBar] 从工作区移除文件夹: " << abs);
    return true;
}

// ========== 槽函数 ==========

void SideBar::onActivityButtonClicked()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    switchToActivity(static_cast<Activity>(btn->property("activity").toInt()));
}

void SideBar::onExplorerOpenFolderClicked()
{
    // ExplorerPanel 的"打开文件夹"按钮点击 — 由 SideBar 处理 QFileDialog
    // （工作区状态归 SideBar 管理，ExplorerPanel 不持有工作区状态）
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("打开文件夹"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        // 复用 setWorkDirectory() 同时更新 m_workDir 和 m_workspaceFolders，
        // 并同步到所有面板（Explorer/Search/Tasks）
        setWorkDirectory(dir);
        emit openFolderRequested();
    }
}

void SideBar::onTerminalClicked()
{
    emit terminalToggleRequested();
}

void SideBar::setTerminalButtonChecked(bool checked)
{
    if (m_btnTerminal) {
        m_btnTerminal->blockSignals(true);
        m_btnTerminal->setChecked(checked);
        m_btnTerminal->blockSignals(false);
    }
}

void SideBar::refreshActivityStyles()
{
    for (auto* btn : {m_btnExplorer, m_btnSearch, m_btnGit, m_btnTasks, m_btnTerminal}) {
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
        btn->update();
    }
}

void SideBar::selectFileByPath(const QString& filePath)
{
    // ISideFileBar 接口实现 — 委托给 ExplorerPanel
    if (m_explorerPanel) m_explorerPanel->selectFileByPath(filePath);
}

// ============================================================
// V2.1: 大纲透传接口（委托给 ExplorerPanel 内嵌的大纲区域）
// ============================================================

void SideBar::updateOutlineForEditor(const QString& filePath, const QList<QVariantMap>& symbols)
{
    if (m_explorerPanel) m_explorerPanel->updateOutline(filePath, symbols);
}

void SideBar::updateOutlineFromText(const QString& filePath, const QString& content)
{
    if (m_explorerPanel) m_explorerPanel->updateOutlineFromText(filePath, content);
}

void SideBar::clearOutline()
{
    if (m_explorerPanel) m_explorerPanel->clearOutline();
}

void SideBar::resetOutlineFilePath(const QString& filePath)
{
    // V2.1 C3: 透传给 ExplorerPanel，立即同步大纲文件路径
    if (m_explorerPanel) m_explorerPanel->resetOutlineFilePath(filePath);
}

void SideBar::savePanelStates()
{
    // V2.1 M2/M3: 关闭时持久化 splitter 高度 + 大纲折叠状态
    if (m_explorerPanel) {
        m_explorerPanel->saveState();
        m_explorerPanel->saveOutlineState();
    }
}
