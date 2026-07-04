#include "ui/terminal/EmbeddedTerminal.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"

#include <QScrollBar>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QShortcut>
#include <QPushButton>
#include <QTextDocument>
#include <QDir>
#include <QStyle>

// ============================================================
// 构造 / 析构
// ============================================================

EmbeddedTerminal::EmbeddedTerminal(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("embeddedTerminal"));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // === 终端标签栏容器（TabBar + 新建按钮）===
    m_tabBarContainer = new QWidget(this);
    m_tabBarContainer->setObjectName(QStringLiteral("terminalTabBarContainer"));
    m_tabBarLayout = new QHBoxLayout(m_tabBarContainer);
    m_tabBarLayout->setContentsMargins(0, 0, 0, 0);
    m_tabBarLayout->setSpacing(0);

    m_tabBar = new QTabBar(m_tabBarContainer);
    m_tabBar->setObjectName(QStringLiteral("terminalTabBar"));
    m_tabBar->setTabsClosable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setMinimumHeight(30);
    m_tabBar->setMaximumHeight(30);

    // 标签栏初始样式（深色默认，applyTheme() 会根据主题动态调整）
    m_tabBar->setStyleSheet(
        QStringLiteral(
            "QTabBar { background: #252526; border-bottom: 1px solid #3c3c3c; }"
            "QTabBar::tab { color: #969696; font-size: 11px; padding: 5px 12px;"
            "            margin-right: 0; border: 1px solid transparent; border-bottom: none;"
            "            border-top-left-radius: 3px; border-top-right-radius: 3px;"
            "            background-color: #2d2d2d; min-width: 70px; }"
            "QTabBar::tab:selected { color: #ffffff; background-color: #1e1e1e;"
            "                   border: 1px solid #3c3c3c; border-bottom: 1px solid #1e1e1e; }"
            "QTabBar::tab:hover:!selected { color: #cccccc; background-color: #383838;"
            "                           border: 1px solid #4a4a4a; border-bottom: none; }"
            )
    );

    // 新建终端按钮 "+"
    m_addTabBtn = new QPushButton(QStringLiteral("+"), m_tabBarContainer);
    m_addTabBtn->setObjectName(QStringLiteral("terminalAddTabBtn"));
    m_addTabBtn->setFixedSize(30, 30);
    m_addTabBtn->setCursor(Qt::PointingHandCursor);
    m_addTabBtn->setToolTip(tr("新建终端 (Ctrl+Shift+`)"));
    m_addTabBtn->setFlat(true);
    connect(m_addTabBtn, &QPushButton::clicked, this, [this]() {
        addTerminalTab(currentTerminalType());
    });

    m_tabBarLayout->addWidget(m_tabBar, 1);
    m_tabBarLayout->addWidget(m_addTabBtn);
    m_layout->addWidget(m_tabBarContainer);

    // === 搜索栏（默认隐藏）===
    m_searchBar = createSearchBar();
    m_searchBar->setVisible(false);
    m_layout->addWidget(m_searchBar);

    // === 会话堆栈 ===
    m_sessionStack = new QStackedWidget(this);
    m_layout->addWidget(m_sessionStack);

    // 信号连接
    connect(m_tabBar, &QTabBar::currentChanged, this, &EmbeddedTerminal::onTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &EmbeddedTerminal::onTabCloseRequested);

    // 监听主题切换
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &EmbeddedTerminal::applyTheme);

    // 快捷键：Ctrl+Shift+` 新建终端
    auto* shortcutNewTerm = new QShortcut(QKeySequence("Ctrl+Shift+`"), this);
    connect(shortcutNewTerm, &QShortcut::activated, this, [this]() {
        addTerminalTab(currentTerminalType());
    });

    // 快捷键：Ctrl+F 切换搜索栏
    auto* shortcutSearch = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(shortcutSearch, &QShortcut::activated, this, [this]() {
        toggleSearchBar(!m_searchBar->isVisible());
    });
}

EmbeddedTerminal::~EmbeddedTerminal()
{
    terminateSession();
}

// ============================================================
// 搜索栏创建（保持不变）
// ============================================================

QWidget* EmbeddedTerminal::createSearchBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("terminalSearchBar"));
    bar->setFixedHeight(36);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(6);

    m_searchInput = new QLineEdit(bar);
    m_searchInput->setObjectName(QStringLiteral("terminalSearchInput"));
    m_searchInput->setPlaceholderText(tr("在终端中查找..."));
    connect(m_searchInput, &QLineEdit::textChanged,
            this, &EmbeddedTerminal::onSearchTextChanged);
    layout->addWidget(m_searchInput, 1);

    auto* btnPrev = new QPushButton(QString::fromUtf8("\xE2\x96\xB2"), bar);  // ▲
    btnPrev->setFixedSize(26, 24);
    btnPrev->setToolTip(tr("上一个匹配 (Shift+Enter)"));
    btnPrev->setCursor(Qt::PointingHandCursor);
    btnPrev->setObjectName(QStringLiteral("searchNavBtn"));
    connect(btnPrev, &QPushButton::clicked, this, &EmbeddedTerminal::onSearchPrev);
    layout->addWidget(btnPrev);

    auto* btnNext = new QPushButton(QString::fromUtf8("\xE2\x96\xBC"), bar);  // ▼
    btnNext->setFixedSize(26, 24);
    btnNext->setToolTip(tr("下一个匹配 (Enter)"));
    btnNext->setCursor(Qt::PointingHandCursor);
    btnNext->setObjectName(QStringLiteral("searchNavBtn"));
    connect(btnNext, &QPushButton::clicked, this, &EmbeddedTerminal::onSearchNext);
    layout->addWidget(btnNext);

    m_searchResultLabel = new QLabel(bar);
    m_searchResultLabel->setObjectName(QStringLiteral("searchResultLabel"));
    m_searchResultLabel->setText(QString());
    layout->addWidget(m_searchResultLabel);

    auto* btnClose = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"), bar);  // ✕
    btnClose->setFixedSize(26, 24);
    btnClose->setToolTip(tr("关闭搜索 (Esc)"));
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setObjectName(QStringLiteral("searchNavBtn"));
    connect(btnClose, &QPushButton::clicked, this, [this]() { toggleSearchBar(false); });
    layout->addWidget(btnClose);

    return bar;
}

void EmbeddedTerminal::toggleSearchBar(bool visible)
{
    if (!m_searchBar) return;
    m_searchBar->setVisible(visible);
    if (visible && m_searchInput) {
        m_searchInput->setFocus();
        m_searchInput->selectAll();
    }
}

// ============================================================
// 标签页管理
// ============================================================

void EmbeddedTerminal::addTerminalTab(const QString& type)
{
    m_sessionCounter++;
    QString name = type == QStringLiteral("powershell")
                       ? tr("PS %1").arg(m_sessionCounter)
                       : tr("CMD %1").arg(m_sessionCounter);

    TerminalSession session;
    session.name = name;
    session.type = type;

    // === 页面容器 ===
    session.pageWidget = new QWidget(m_sessionStack);
    auto* pageLayout = new QVBoxLayout(session.pageWidget);
    pageLayout->setContentsMargins(4, 4, 4, 4);
    pageLayout->setSpacing(0);

    // === 统一视图（输入输出合一）===
    session.view = new TerminalView(session.pageWidget);
    session.view->setObjectName(QStringLiteral("terminalOutput"));
    pageLayout->addWidget(session.view, 1);

    // 安装事件过滤器（用于 Ctrl+A 等全局快捷键）
    session.view->installEventFilter(this);

    m_sessionStack->addWidget(session.pageWidget);

    int index = m_tabBar->addTab(name);
    m_sessions[index] = session;
    m_tabBar->setCurrentIndex(index);

    // 启动后端进程并连接信号
    setupSession(session);

    // 应用当前主题样式
    applyTheme();

    emit tabCountChanged(m_tabBar->count());
}

void EmbeddedTerminal::closeCurrentTab()
{
    int idx = m_tabBar->currentIndex();
    if (idx >= 0) {
        onTabCloseRequested(idx);
    }
}

QString EmbeddedTerminal::currentTerminalType() const
{
    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it != m_sessions.end()) {
        return it.value().type;
    }
    return ConfigManager::instance().getValue("Terminal/type", QStringLiteral("powershell")).toString();
}

// ============================================================
// P2-H01: 终端复用 / 编辑器联动
// ============================================================

TerminalBackend* EmbeddedTerminal::currentBackend() const
{
    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it != m_sessions.end()) {
        return it.value().backend;
    }
    return nullptr;
}

void EmbeddedTerminal::attachExistingBackend(TerminalBackend* backend, const QString& tabTitle)
{
    if (!backend) return;

    backend->addRef();

    m_sessionCounter++;
    QString baseTitle = tabTitle.isEmpty()
                            ? tr("%1 (clone)").arg(m_sessionCounter)
                            : tabTitle;

    TerminalSession session;
    session.name = baseTitle;
    session.type = currentTerminalType();
    session.backend = backend;

    // === 页面容器 ===
    session.pageWidget = new QWidget(m_sessionStack);
    auto* pageLayout = new QVBoxLayout(session.pageWidget);
    pageLayout->setContentsMargins(4, 4, 4, 4);
    pageLayout->setSpacing(0);

    // === 统一视图（输入输出合一）===
    session.view = new TerminalView(session.pageWidget);
    session.view->setObjectName(QStringLiteral("terminalOutput"));
    pageLayout->addWidget(session.view, 1);

    session.view->installEventFilter(this);

    m_sessionStack->addWidget(session.pageWidget);

    int index = m_tabBar->addTab(baseTitle);
    m_sessions[index] = session;
    m_tabBar->setCurrentIndex(index);

    // 连接已有后端信号到新视图
    setupClonedSession(session, backend);

    applyTheme();

    emit tabCountChanged(m_tabBar->count());
}

void EmbeddedTerminal::setupClonedSession(TerminalSession& session, TerminalBackend* backend)
{
    const auto& palette = ThemeManager::instance().currentPalette();

    session.view->showWelcome(
        tr("=== 克隆终端 ===\n共享同一个 shell 会话\n\n"),
        palette.accentPrimary);

    // stdout 数据 → 视图渲染（与原标签页共享同一后端输出）
    connect(backend, &TerminalBackend::readyReadStandardOutput,
            session.view, &TerminalView::appendOutput);

    // stderr 数据 → 视图渲染（红色标识错误输出）
    connect(backend, &TerminalBackend::readyReadStandardError,
            session.view, [view = session.view](const QByteArray& data) {
        view->appendPlainText(QString::fromLocal8Bit(data), QColor(255, 100, 100));
    });

    // 进程退出 → 视图显示状态
    const QColor exitErrorColor = palette.errorColor;
    const QColor exitNormalColor = palette.accentPrimary;
    connect(backend, &TerminalBackend::finished,
            session.view, [view = session.view, exitErrorColor, exitNormalColor](int code, bool crashed) {
        QColor color = crashed ? exitErrorColor : exitNormalColor;
        view->showWelcome(
            crashed ? tr("\n[终端异常退出]\n") : tr("\n[终端已退出 (%1)]\n").arg(code),
            color);
    });

    // 用户提交命令 → 后端执行（与原标签页共享输入通道）
    connect(session.view, &TerminalView::commandSubmitted,
            backend, [backend](const QString& cmd) {
        if (!cmd.isEmpty()) {
            backend->write((cmd + "\r\n").toLocal8Bit());
        } else {
            backend->write("\r\n");
        }
    });

    // 中断请求 → 后端发送 Ctrl+C
    connect(session.view, &TerminalView::interruptRequested,
            backend, &TerminalBackend::sendInterrupt);

    session.view->focusInputEnd();
    session.view->setFocus();
}

// ============================================================
// 会话管理（使用 TerminalBackend）
// ============================================================

void EmbeddedTerminal::setupSession(TerminalSession& session)
{
    // 创建后端进程
    // P2-H01: 不设父对象，由引用计数管理生命周期（release() 在 refCount=0 时 delete this）
    session.backend = new TerminalBackend(nullptr);

    // 确定后端 shell 类型
    TerminalBackend::ShellType shellType = (session.type == QStringLiteral("powershell"))
                                              ? TerminalBackend::ShellType::PowerShell
                                              : TerminalBackend::ShellType::CMD;

    // 启动进程
    bool started = session.backend->start(shellType, m_workingDir);

    const auto& palette = ThemeManager::instance().currentPalette();

    if (!started) {
        session.view->showWelcome(
            tr("[错误] 终端启动失败\n"),
            palette.errorColor);
        return;
    }

    // 显示欢迎信息和工作目录
    QString workDir = m_workingDir.isEmpty()
                          ? QDir::currentPath()
                          : m_workingDir;
    QString typeName = (session.type == QStringLiteral("powershell")) ? "PowerShell" : "CMD";
    session.view->showWelcome(
        tr("=== %1 终端 ===\n工作目录: %2\n\n").arg(typeName).arg(workDir),
        palette.accentPrimary);

    // === 核心信号连接：后端 ↔ 视图 ===

    // stdout 数据 → 视图渲染（含 ANSI 解析）
    connect(session.backend, &TerminalBackend::readyReadStandardOutput,
            session.view, &TerminalView::appendOutput);

    // stderr 数据 → 视图渲染（P1-2: 使用红色标识错误输出）
    connect(session.backend, &TerminalBackend::readyReadStandardError,
            session.view, [view = session.view](const QByteArray& data) {
        // P1-2: stderr 使用红色显示，便于区分错误信息
        view->appendPlainText(QString::fromLocal8Bit(data), QColor(255, 100, 100));  // 柔和红色
    });

    // 进程退出 → 视图显示状态（颜色值捕获，避免悬空引用）
    const QColor exitErrorColor = palette.errorColor;
    const QColor exitNormalColor = palette.accentPrimary;
    connect(session.backend, &TerminalBackend::finished,
            session.view, [view = session.view, exitErrorColor, exitNormalColor](int code, bool crashed) {
        QColor color = crashed ? exitErrorColor : exitNormalColor;
        view->showWelcome(
            crashed ? tr("\n[终端异常退出]\n") : tr("\n[终端已退出 (%1)]\n").arg(code),
            color);
    });

    // 用户提交命令 → 后端执行
    connect(session.view, &TerminalView::commandSubmitted,
            session.backend, [backend = session.backend](const QString& cmd) {
        if (!cmd.isEmpty()) {
            backend->write((cmd + "\r\n").toLocal8Bit());
        } else {
            backend->write("\r\n");
        }
    });

    // 中断请求 → 后端发送 Ctrl+C
    connect(session.view, &TerminalView::interruptRequested,
            session.backend, &TerminalBackend::sendInterrupt);

    // 聚焦到输入区
    session.view->focusInputEnd();
    session.view->setFocus();
}

void EmbeddedTerminal::cleanupSession(TerminalSession& session)
{
    if (session.backend) {
        // P2-H01: 使用引用计数管理生命周期
        // release() 会在引用计数归零时自动 stop() 并销毁 backend
        // 共享 backend 的其他标签页仍可正常使用
        session.backend->release();
        session.backend = nullptr;
    }
}

// ============================================================
// ITerminalWidget 接口实现
// ============================================================

void EmbeddedTerminal::startSession()
{
    if (m_sessions.isEmpty()) {
        QString defaultType = ConfigManager::instance()
                                  .getValue("Terminal/type", QStringLiteral("powershell"))
                                  .toString();
        addTerminalTab(defaultType);
    }
}

void EmbeddedTerminal::terminateSession()
{
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        cleanupSession(it.value());
    }
    m_sessions.clear();
}

void EmbeddedTerminal::executeCommand(const QString& command)
{
    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it == m_sessions.end() || !it.value().backend) return;

    TerminalSession& session = it.value();
    if (!session.backend->isRunning()) {
        const auto& palette = ThemeManager::instance().currentPalette();
        session.view->showWelcome(tr("[错误] 终端进程未运行\n"), palette.errorColor);
        return;
    }

    // 通过视图显示命令（模拟用户输入），然后发送给后端
    session.view->showWelcome(command + "\n", ThemeManager::instance().currentPalette().accentHover);
    session.backend->write((command + "\r\n").toLocal8Bit());
}

void EmbeddedTerminal::setWorkingDirectory(const QString& dirPath)
{
    m_workingDir = dirPath;
    // 已运行的会话通过 cd 命令切换目录
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        TerminalSession& session = it.value();
        if (session.backend && session.backend->isRunning()) {
            if (session.type == QStringLiteral("powershell")) {
                session.backend->write(QStringLiteral("cd \"%1\"\n").arg(dirPath).toUtf8());
            } else {
                session.backend->write(QStringLiteral("cd /d \"%1\"\r\n").arg(dirPath).toLocal8Bit());
            }
        }
    }
}

bool EmbeddedTerminal::isRunning() const
{
    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it != m_sessions.end() && it.value().backend) {
        return it.value().backend->isRunning();
    }
    return false;
}

// ============================================================
// 标签页事件
// ============================================================

void EmbeddedTerminal::onTabChanged(int index)
{
    if (index >= 0 && index < m_sessionStack->count()) {
        m_sessionStack->setCurrentIndex(index);
    }

    // 切换标签时刷新搜索状态
    clearSearchHighlights();
    if (m_searchInput && !m_searchInput->text().isEmpty()) {
        onSearchTextChanged(m_searchInput->text());
    }

    // 聚焦到当前终端视图
    auto it = m_sessions.find(index);
    if (it != m_sessions.end() && it.value().view) {
        it.value().view->focusInputEnd();
        it.value().view->setFocus();
    }
}

void EmbeddedTerminal::onTabCloseRequested(int index)
{
    auto it = m_sessions.find(index);
    if (it == m_sessions.end()) return;

    cleanupSession(it.value());

    if (it.value().pageWidget) {
        m_sessionStack->removeWidget(it.value().pageWidget);
        it.value().pageWidget->deleteLater();
    }

    m_tabBar->removeTab(index);
    m_sessions.remove(index);

    // 重新映射索引
    QMap<int, TerminalSession> newSessions;
    for (int i = 0; i < m_tabBar->count(); ++i) {
        for (auto oldIt = m_sessions.begin(); oldIt != m_sessions.end(); ++oldIt) {
            if (oldIt.value().pageWidget && m_sessionStack->indexOf(oldIt.value().pageWidget) == i) {
                newSessions[i] = oldIt.value();
                break;
            }
        }
    }
    m_sessions = newSessions;

    emit tabCountChanged(m_tabBar->count());
}

// ============================================================
// 搜索功能（作用于 TerminalView 的文档）
// ============================================================

void EmbeddedTerminal::onSearchTextChanged(const QString& text)
{
    m_searchKeyword = text.trimmed();
    clearSearchHighlights();

    if (m_searchKeyword.isEmpty()) {
        if (m_searchResultLabel) m_searchResultLabel->clear();
        return;
    }

    highlightSearchResults(m_searchKeyword, QColor("#ffcc00"));
}

void EmbeddedTerminal::highlightSearchResults(const QString& keyword, const QColor& color)
{
    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it == m_sessions.end() || !it.value().view) return;

    QTextEdit* view = it.value().view;
    QTextDocument* doc = view->document();

    m_searchMatches.clear();
    m_currentSearchIndex = -1;

    QTextCursor searchCursor(doc);
    while (!(searchCursor = doc->find(keyword, searchCursor)).isNull()) {
        m_searchMatches.append(searchCursor);
    }

    for (const QTextCursor& match : m_searchMatches) {
        QTextCharFormat fmt;
        fmt.setBackground(color);
        fmt.setForeground(Qt::black);
        QTextCursor highlightCursor(match);
        highlightCursor.mergeCharFormat(fmt);
    }

    if (m_searchResultLabel) {
        if (m_searchMatches.isEmpty()) {
            m_searchResultLabel->setText(tr("无结果"));
        } else {
            m_searchResultLabel->setText(tr("%1/%2").arg(0).arg(m_searchMatches.size()));
        }
    }
}

void EmbeddedTerminal::clearSearchHighlights()
{
    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it == m_sessions.end() || !it.value().view) return;

    QTextDocument* doc = it.value().view->document();
    QTextCursor clearCursor(doc);
    clearCursor.select(QTextCursor::Document);
    QTextCharFormat defaultFmt;
    defaultFmt.setBackground(Qt::transparent);
    clearCursor.setCharFormat(defaultFmt);

    m_searchMatches.clear();
    m_currentSearchIndex = -1;
    if (m_searchResultLabel) m_searchResultLabel->clear();
}

void EmbeddedTerminal::onSearchNext()
{
    if (m_searchMatches.isEmpty()) return;

    m_currentSearchIndex = (m_currentSearchIndex + 1) % m_searchMatches.size();
    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it == m_sessions.end()) return;

    const QTextCursor& target = m_searchMatches[m_currentSearchIndex];
    it.value().view->setTextCursor(target);
    it.value().view->ensureCursorVisible();

    if (m_searchResultLabel) {
        m_searchResultLabel->setText(tr("%1/%2")
                                     .arg(m_currentSearchIndex + 1)
                                     .arg(m_searchMatches.size()));
    }
}

void EmbeddedTerminal::onSearchPrev()
{
    if (m_searchMatches.isEmpty()) return;

    m_currentSearchIndex = (m_currentSearchIndex - 1 + m_searchMatches.size()) % m_searchMatches.size();
    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it == m_sessions.end()) return;

    const QTextCursor& target = m_searchMatches[m_currentSearchIndex];
    it.value().view->setTextCursor(target);
    it.value().view->ensureCursorVisible();

    if (m_searchResultLabel) {
        m_searchResultLabel->setText(tr("%1/%2")
                                     .arg(m_currentSearchIndex + 1)
                                     .arg(m_searchMatches.size()));
    }
}

// ============================================================
// 事件过滤器 — 拦截 TerminalView 区域的快捷键
// ============================================================

bool EmbeddedTerminal::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        auto* textEdit = qobject_cast<TerminalView*>(obj);

        if (textEdit) {
            // Ctrl+A 全选
            if ((keyEvent->modifiers() & Qt::ControlModifier) &&
                keyEvent->key() == Qt::Key_A) {
                textEdit->selectAll();
                return true;
            }

            // Esc 关闭搜索栏
            if (keyEvent->key() == Qt::Key_Escape && m_searchBar && m_searchBar->isVisible()) {
                toggleSearchBar(false);
                return true;
            }

            // Enter 在搜索框中触发下一个
            if (keyEvent->key() == Qt::Key_Return && obj == m_searchInput) {
                onSearchNext();
                return true;
            }

            // Shift+Enter 触发上一个
            if ((keyEvent->modifiers() & Qt::ShiftModifier) &&
                keyEvent->key() == Qt::Key_Return && obj == m_searchInput) {
                onSearchPrev();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ============================================================
// 右键菜单
// ============================================================

void EmbeddedTerminal::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("contextMenu"));

    int idx = m_tabBar->currentIndex();
    auto it = m_sessions.find(idx);
    if (it == m_sessions.end() || !it.value().view) return;

    TerminalSession& session = it.value();
    TerminalView* view = session.view;
    bool hasSelection = view->textCursor().hasSelection();

    // 复制
    QAction* actCopy = menu.addAction(tr("复制          Ctrl+C"));
    actCopy->setEnabled(hasSelection);
    connect(actCopy, &QAction::triggered, this, [&]() {
        if (view->textCursor().hasSelection()) {
            QApplication::clipboard()->setText(view->textCursor().selectedText());
        }
    });

    // 粘贴
    QAction* actPaste = menu.addAction(tr("粘贴          Ctrl+V"));
    actPaste->setEnabled(!QApplication::clipboard()->text().isEmpty());
    connect(actPaste, &QAction::triggered, this, [this, &session]() {
        QString text = QApplication::clipboard()->text();
        if (!text.isEmpty() && session.backend && session.backend->isRunning()) {
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            text.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
            session.backend->write(text.toLocal8Bit());
        }
    });

    menu.addSeparator();

    // 全选
    QAction* actSelectAll = menu.addAction(tr("全选          Ctrl+A"));
    connect(actSelectAll, &QAction::triggered, this, [view]() {
        if (view) view->selectAll();
    });

    menu.addSeparator();

    // 查找
    QAction* actFind = menu.addAction(tr("查找...        Ctrl+F"));
    connect(actFind, &QAction::triggered, this, [this]() {
        toggleSearchBar(true);
    });

    menu.addSeparator();

    // 清屏
    QAction* actClear = menu.addAction(tr("清屏"));
    connect(actClear, &QAction::triggered, this, [view]() {
        if (view) view->clearScreen();
    });

    menu.addSeparator();

    // 新建终端
    QAction* actNewTerm = menu.addAction(tr("新建终端      Ctrl+Shift+`"));
    connect(actNewTerm, &QAction::triggered, this, [this]() {
        addTerminalTab(currentTerminalType());
    });

    // P2-H01: 克隆当前终端（共享同一 shell 会话到新标签页）
    QAction* actCloneTerm = menu.addAction(tr("克隆当前终端"));
    actCloneTerm->setEnabled(session.backend != nullptr && session.backend->isRunning());
    connect(actCloneTerm, &QAction::triggered, this, [this, sessionName = session.name]() {
        TerminalBackend* backend = currentBackend();
        if (backend) {
            // 克隆标签标题 = 原标签名 + " (clone)" 后缀
            QString cloneTitle = sessionName.isEmpty()
                                     ? QString()
                                     : sessionName + QStringLiteral(" (clone)");
            attachExistingBackend(backend, cloneTitle);
        }
    });

    menu.addSeparator();

    // 切换终端类型
    QString currentType = session.type;
    QString switchText = currentType == QStringLiteral("cmd")
                             ? tr("切换为 PowerShell")
                             : tr("切换为 CMD");
    QAction* actSwitch = menu.addAction(switchText);
    connect(actSwitch, &QAction::triggered, this, [this, currentType]() {
        addTerminalTab(currentType == QStringLiteral("cmd") ? QStringLiteral("powershell") : QStringLiteral("cmd"));
    });

    menu.exec(event->globalPos());
}

// ============================================================
// 主题联动
// ============================================================

void EmbeddedTerminal::applyTheme()
{
    auto& config = ConfigManager::instance();
    const auto& palette = ThemeManager::instance().currentPalette();

    // 检测当前是否为浅色主题（通过编辑器背景亮度判断）
    bool isLightMode = palette.bgEditor.lightness() > 128;

    // 根据主题模式选择终端配色
    QString tabBarBg, tabBarBorder, tabColor, tabBg, tabSelColor, tabSelBg,
            tabHoverColor, tabHoverBg, tabSelBorder;
    QString fgColorStr, bgColorStr, cursorColorStr;

    if (isLightMode) {
        // 浅色模式：使用接近编辑器的浅色调
        tabBarBg       = palette.bgTabBar.name();
        tabBarBorder   = palette.borderDefault.name();
        tabColor       = palette.fgSecondary.name();
        tabBg          = palette.bgTabInactive.name();
        tabSelColor    = palette.fgPrimary.name();
        tabSelBg       = palette.bgTabActive.name();
        tabHoverColor  = palette.fgSecondary.name();
        tabHoverBg     = palette.bgHover.name();
        tabSelBorder   = palette.bgTabActive.name();  // 底边与选中背景融合
        fgColorStr     = palette.fgPrimary.name();
        bgColorStr     = palette.bgEditor.name();
        cursorColorStr = palette.fgPrimary.name();
    } else {
        // 深色模式：VSCode 风格深色终端
        tabBarBg       = QStringLiteral("#252526");
        tabBarBorder   = QStringLiteral("#3c3c3c");
        tabColor       = QStringLiteral("#969696");
        tabBg          = QStringLiteral("#2d2d2d");
        tabSelColor    = QStringLiteral("#ffffff");
        tabSelBg       = QStringLiteral("#1e1e1e");
        tabHoverColor  = QStringLiteral("#cccccc");
        tabHoverBg     = QStringLiteral("#383838");
        tabSelBorder   = QStringLiteral("#1e1e1e");
        fgColorStr     = config.getValue("Terminal/fgColor", QStringLiteral("#cccccc")).toString();
        bgColorStr     = config.getValue("Terminal/bgColor", QStringLiteral("#1e1e1e")).toString();
        cursorColorStr = config.getValue("Terminal/cursorColor", QStringLiteral("#ffffff")).toString();
    }

    int fontSize = config.getValue("Terminal/fontSize", 12).toInt();
    QString fontFamily = config.getValue("Terminal/fontFamily", QStringLiteral("Consolas")).toString();

    QColor fgColor(fgColorStr);
    QColor bgColor(bgColorStr);
    QColor cursorColor(cursorColorStr);

    QFont font(fontFamily, fontSize);

    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().view) {
            it.value().view->setTerminalFont(font);
            it.value().view->setForegroundColor(fgColor);
            it.value().view->setBackgroundColor(bgColor);
            it.value().view->setCursorColor(cursorColor);
            it.value().view->style()->unpolish(it.value().view);
            it.value().view->style()->polish(it.value().view);
            it.value().view->update();
        }
    }

    if (m_tabBar) {
        m_tabBar->setStyleSheet(
            QStringLiteral(
                "QTabBar { background: %1; border-bottom: 1px solid %2; }"
                "QTabBar::tab { color: %3; font-size: 11px; padding: 5px 12px;"
                "            margin-right: 0; border: 1px solid transparent; border-bottom: none;"
                "            border-top-left-radius: 3px; border-top-right-radius: 3px;"
                "            background-color: %4; min-width: 70px; }"
                "QTabBar::tab:selected { color: %5; background-color: %6;"
                "                   border: 1px solid %7; border-bottom: 1px solid %8; }"
                "QTabBar::tab:hover:!selected { color: %9; background-color: %10;"
                "                           border: 1px solid %11; border-bottom: none; }"
                )
                .arg(tabBarBg, tabBarBorder,
                     tabColor, tabBg,
                     tabSelColor, tabSelBg, tabBarBorder, tabSelBorder,
                     tabHoverColor, tabHoverBg, palette.borderHover.name())
        );
        m_tabBar->update();
    }

    // 新建终端按钮样式
    if (m_addTabBtn) {
        QString btnColor = isLightMode ? palette.fgSecondary.name() : QStringLiteral("#969696");
        QString btnHoverBg = isLightMode ? palette.bgHover.name() : QStringLiteral("#383838");
        m_addTabBtn->setStyleSheet(
            QStringLiteral(
                "QPushButton { color: %1; font-size: 16px; font-weight: bold;"
                "              border: none; background: transparent; }"
                "QPushButton:hover { background: %2; border-radius: 3px; }"
                ).arg(btnColor, btnHoverBg)
        );
    }

    // 标签栏容器背景
    if (m_tabBarContainer) {
        m_tabBarContainer->setStyleSheet(
            QStringLiteral("QWidget#terminalTabBarContainer { background: %1; }")
                .arg(tabBarBg)
        );
    }

    if (m_searchBar) {
        m_searchBar->style()->unpolish(m_searchBar);
        m_searchBar->style()->polish(m_searchBar);
        m_searchBar->update();
    }
}
