#include "ui/shell/TitleBar.h"
#include "core/i18n/I18nManager.h"  // P3-M05: 语言切换
#include "core/config/ConfigManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QContextMenuEvent>
#include <QActionGroup>
#include <QHash>            // P3-M05: 语言代码 → 显示名映射
#include <QStringList>      // P3-M05: 可用语言列表

/// @brief 创建一个 VSCode 风格的文本菜单按钮（点击弹出菜单）
/// P3-M05: 设置最小宽度，防止 "Save"/"保存" 等不同语言切换时按钮宽度跳变
static QPushButton* createMenuBtn(const QString& text, const QString& shortcut, const QString& tooltip, QWidget* parent)
{
    auto* btn = new QPushButton(text + shortcut, parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName(QStringLiteral("menuBarItem"));
    btn->setToolTip(tooltip);
    btn->setFlat(true);
    btn->setMinimumWidth(80);  // P3-M05: 多语言切换时稳定按钮宽度
    return btn;
}

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(36);
    setObjectName(QStringLiteral("titleBar"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(0);

    // === 左侧：图标 + 标题文字 ===
    m_labelIcon = new QLabel(this);
    // 修复：空值校验，避免 QPixmap::scaled: Pixmap is a null pixmap 警告
    {
        QPixmap icon(QStringLiteral(":/app_icon"));
        if (!icon.isNull()) {
            m_labelIcon->setPixmap(icon.scaled(18, 18, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
    layout->addWidget(m_labelIcon);

    m_labelTitle = new QLabel(tr("SoulCove"), this);
    m_labelTitle->setObjectName(QStringLiteral("titleLabel"));
    layout->addWidget(m_labelTitle);

    // === 分隔符 ===
    auto* separator = new QLabel(QStringLiteral("|"), this);
    separator->setObjectName(QStringLiteral("titleSeparator"));
    layout->addWidget(separator);

    // === VSCode 风格文本菜单栏（单击弹出菜单）===
    // 文件(F) 菜单
    m_btnNew = createMenuBtn(tr("文件"), tr("(F)"), tr("文件操作"), this);
    auto* fileMenu = new QMenu(this);
    fileMenu->addAction(tr("新建文件        Ctrl+N"))->setData(QStringLiteral("new"));
    fileMenu->addAction(tr("打开文件...      Ctrl+O"))->setData(QStringLiteral("open"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("保存             Ctrl+S"))->setData(QStringLiteral("save"));
    fileMenu->addAction(tr("另存为..."))->setData(QStringLiteral("saveAs"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("打开文件夹..."))->setData(QStringLiteral("openFolder"));
    // P2-H04: 工作区持久化
    fileMenu->addAction(tr("打开工作区..."))->setData(QStringLiteral("openWorkspace"));
    fileMenu->addAction(tr("保存工作区..."))->setData(QStringLiteral("saveWorkspace"));
    fileMenu->addSeparator();
    // P3-M01 子项4: 挂载远程工作区（SFTP 双向同步）
    fileMenu->addAction(tr("挂载远程工作区..."))->setData(QStringLiteral("mountRemote"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出               Alt+F4"))->setData(QStringLiteral("quit"));

    connect(fileMenu, &QMenu::triggered, this, [this](QAction* action) {
        QString key = action->data().toString();
        if (key == QStringLiteral("new"))       emit newFileRequested();
        else if (key == QStringLiteral("open"))   emit openFileRequested();
        else if (key == QStringLiteral("save"))   emit saveRequested(); // reuse settings signal for now
        else if (key == QStringLiteral("openFolder")) emit openFolderRequested();
        else if (key == QStringLiteral("openWorkspace")) emit openWorkspaceRequested();
        else if (key == QStringLiteral("saveWorkspace")) emit saveWorkspaceRequested();
        else if (key == QStringLiteral("mountRemote")) emit mountRemoteWorkspaceRequested();
        else if (key == QStringLiteral("quit"))   emit quitRequested();
    });
    connect(m_btnNew, &QPushButton::clicked, this, [this, fileMenu]() {
        fileMenu->exec(m_btnNew->mapToGlobal(QPoint(0, m_btnNew->height())));
    });

    // 编辑(E) 菜单
    m_btnOpen = createMenuBtn(tr("编辑"), tr("(E)"), tr("编辑操作"), this);
    auto* editMenu = new QMenu(this);
    editMenu->addAction(tr("撤销             Ctrl+Z"))->setData(QStringLiteral("undo"));
    editMenu->addAction(tr("重做             Ctrl+Y"))->setData(QStringLiteral("redo"));
    editMenu->addSeparator();
    editMenu->addAction(tr("剪切             Ctrl+X"))->setData(QStringLiteral("cut"));
    editMenu->addAction(tr("复制             Ctrl+C"))->setData(QStringLiteral("copy"));
    editMenu->addAction(tr("粘贴             Ctrl+V"))->setData(QStringLiteral("paste"));
    editMenu->addSeparator();
    editMenu->addAction(tr("全选             Ctrl+A"))->setData(QStringLiteral("selectAll"));
    editMenu->addAction(tr("查找             Ctrl+F"))->setData(QStringLiteral("find"));
    editMenu->addAction(tr("替换           Ctrl+H"))->setData(QStringLiteral("replace"));

    connect(m_btnOpen, &QPushButton::clicked, this, [this, editMenu]() {
        editMenu->exec(m_btnOpen->mapToGlobal(QPoint(0, m_btnOpen->height())));
    });

    // 选择(S) 菜单
    m_btnSave = createMenuBtn(tr("选择"), tr("(S)"), tr("选择操作"), this);
    auto* selectMenu = new QMenu(this);
    selectMenu->addAction(tr("全选             Ctrl+A"))->setData(QStringLiteral("selectAll"));
    selectMenu->addSeparator();
    selectMenu->addAction(tr("展开选中         Ctrl+Shift+]"))->setData(QStringLiteral("expandSel"));
    selectMenu->addAction(tr("折叠选中         Ctrl+Shift+["))->setData(QStringLiteral("collapseSel"));

    connect(m_btnSave, &QPushButton::clicked, this, [this, selectMenu]() {
        selectMenu->exec(m_btnSave->mapToGlobal(QPoint(0, m_btnSave->height())));
    });

    // === P3-M05: 视图(V) 菜单（含语言切换子菜单）===
    m_btnView = createMenuBtn(tr("视图"), tr("(V)"), tr("视图与语言切换"), this);
    auto* viewMenu = new QMenu(this);
    // 语言子菜单
    auto* langMenu = viewMenu->addMenu(tr("语言"));
    auto* langGroup = new QActionGroup(langMenu);
    langGroup->setExclusive(true);

    // 列出可用语言，复选当前语言
    QString currentLang = I18nManager::instance().currentLanguage();
    QStringList availableLangs = I18nManager::instance().availableLanguages();
    // 语言代码 → 显示名映射（保证菜单项稳定可读）
    static const QHash<QString, QString> kLangDisplayNames = {
        { QStringLiteral("zh_CN"), QStringLiteral("简体中文") },
        { QStringLiteral("en_US"), QStringLiteral("English") },
        { QStringLiteral("system"), QStringLiteral("跟随系统") }
    };
    // 始终包含"跟随系统"选项
    QStringList langEntries = availableLangs;
    if (!langEntries.contains(QStringLiteral("system"))) {
        langEntries.append(QStringLiteral("system"));
    }
    for (const QString& langCode : langEntries) {
        QString display = kLangDisplayNames.value(langCode, langCode);
        QAction* act = langMenu->addAction(display);
        act->setData(langCode);
        act->setCheckable(true);
        act->setChecked(langCode == currentLang);
        langGroup->addAction(act);
    }
    connect(langMenu, &QMenu::triggered, this, [this](QAction* action) {
        QString code = action->data().toString();
        if (!code.isEmpty()) {
            emit languageChangeRequested(code);
        }
    });

    // 当点击视图按钮时，刷新语言菜单的勾选状态（应对运行时切换）
    connect(m_btnView, &QPushButton::clicked, this, [this, langMenu, viewMenu]() {
        QString cur = I18nManager::instance().currentLanguage();
        for (QAction* act : langMenu->actions()) {
            act->setChecked(act->data().toString() == cur);
        }
        viewMenu->exec(m_btnView->mapToGlobal(QPoint(0, m_btnView->height())));
    });

    layout->addWidget(m_btnNew);
    layout->addWidget(m_btnOpen);
    layout->addWidget(m_btnSave);
    layout->addWidget(m_btnView);

    // === 弹簧（推开右侧按钮）===
    layout->addStretch();

    // === 设置按钮（齿轮图标）===
    m_btnSettings = new QPushButton(QString::fromUtf8("\xE2\x9A\x99"), this);  // ⚙
    m_btnSettings->setFixedSize(32, 32);
    m_btnSettings->setCursor(Qt::PointingHandCursor);
    m_btnSettings->setObjectName(QStringLiteral("btnSettings"));
    m_btnSettings->setToolTip(tr("设置"));
    layout->addWidget(m_btnSettings);

    // === 右侧：窗口控制按钮（VSCode极简风格）===
    auto createWindowBtn = [](const QString& text, const QString& objName) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setFixedSize(46, 32);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setObjectName(objName);
        return btn;
    };

    m_btnMinimize = createWindowBtn(QStringLiteral("\u2014"), QStringLiteral("btnMinimize"));
    m_btnMaximize = createWindowBtn(QStringLiteral("\u25A1"), QStringLiteral("btnMaximize"));
    m_btnClose    = createWindowBtn(QStringLiteral("\u2715"), QStringLiteral("btnClose"));

    layout->addWidget(m_btnMinimize);
    layout->addWidget(m_btnMaximize);
    layout->addWidget(m_btnClose);

    // 信号连接
    connect(m_btnMinimize, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_btnMaximize, &QPushButton::clicked, this, &TitleBar::maximizeRequested);
    connect(m_btnClose, &QPushButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::setTitle(const QString& title)
{
    m_labelTitle->setText(title);
}

void TitleBar::updateMaximizeIcon(bool isMaximized)
{
    // 最大化时显示 ❐（还原图标），正常时显示 □（最大化图标）
    m_btnMaximize->setText(isMaximized ? QStringLiteral("\u2750") : QStringLiteral("\u25A1"));
}

void TitleBar::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);

    QAction* actOpenFolder = menu.addAction(tr("打开文件夹"));
    QAction* actOpenFile   = menu.addAction(tr("打开文件"));
    QAction* actNewFile    = menu.addAction(tr("新建文件"));

    menu.addSeparator();

    QAction* actRefresh    = menu.addAction(tr("刷新文件列表"));

    menu.addSeparator();

    QAction* actQuit       = menu.addAction(tr("退出"));

    QAction* selected = menu.exec(event->globalPos());
    if (selected == actOpenFolder) {
        emit openFolderRequested();
    } else if (selected == actOpenFile) {
        emit openFileRequested();
    } else if (selected == actNewFile) {
        emit newFileRequested();
    } else if (selected == actRefresh) {
        emit refreshRequested();
    } else if (selected == actQuit) {
        emit quitRequested();
    }
}
