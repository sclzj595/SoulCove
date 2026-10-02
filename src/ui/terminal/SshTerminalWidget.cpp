#include "ui/terminal/SshTerminalWidget.h"
#include "core/remote/SshClient.h"
#include "core/remote/SshSessionManager.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"
#include "ui/dialog/ModernDialog.h"
#include "Logger.hpp"

#include <QScrollBar>
#include <QShowEvent>
#include <QResizeEvent>
#include <QFont>
#include <QRegularExpression>

// ============================================================
// 构造 / 析构
// ============================================================

SshTerminalWidget::SshTerminalWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sshTerminalWidget"));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // === 标签栏容器 ===
    m_tabBarContainer = new QWidget(this);
    m_tabBarContainer->setObjectName(QStringLiteral("sshTabBarContainer"));
    m_tabBarLayout = new QHBoxLayout(m_tabBarContainer);
    m_tabBarLayout->setContentsMargins(0, 0, 0, 0);
    m_tabBarLayout->setSpacing(0);

    m_tabBar = new QTabBar(m_tabBarContainer);
    m_tabBar->setObjectName(QStringLiteral("sshTabBar"));
    m_tabBar->setTabsClosable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setMinimumHeight(30);
    m_tabBar->setMaximumHeight(30);

    m_addTabBtn = new QPushButton(QStringLiteral("+"), m_tabBarContainer);
    m_addTabBtn->setObjectName(QStringLiteral("sshAddTabBtn"));
    m_addTabBtn->setFixedSize(30, 30);
    m_addTabBtn->setCursor(Qt::PointingHandCursor);
    m_addTabBtn->setToolTip(tr("新建SSH连接"));
    m_addTabBtn->setFlat(true);
    connect(m_addTabBtn, &QPushButton::clicked, this, &SshTerminalWidget::onAddTabClicked);

    m_tabBarLayout->addWidget(m_tabBar, 1);
    m_tabBarLayout->addWidget(m_addTabBtn);
    m_layout->addWidget(m_tabBarContainer);

    // === 会话堆栈 ===
    m_sessionStack = new QStackedWidget(this);
    m_layout->addWidget(m_sessionStack, 1);

    // === 空状态引导提示 ===
    m_emptyHintLabel = new QLabel(this);
    m_emptyHintLabel->setObjectName(QStringLiteral("sshEmptyHint"));
    m_emptyHintLabel->setAlignment(Qt::AlignCenter);
    m_emptyHintLabel->setTextFormat(Qt::RichText);
    m_emptyHintLabel->setOpenExternalLinks(false);
    m_emptyHintLabel->setWordWrap(true);
    connect(m_emptyHintLabel, &QLabel::linkActivated, this, &SshTerminalWidget::onAddTabClicked);
    updateEmptyHint();
    // 将空提示覆盖在会话堆栈上方
    m_emptyHintLabel->setParent(m_sessionStack);
    m_emptyHintLabel->setGeometry(m_sessionStack->rect());
    m_emptyHintLabel->raise();

    // === 状态栏 ===
    m_statusLabel = new QLabel(tr("未连接"), this);
    m_statusLabel->setObjectName(QStringLiteral("sshStatusLabel"));
    m_statusLabel->setFixedHeight(22);
    m_statusLabel->setContentsMargins(8, 0, 8, 0);
    m_layout->addWidget(m_statusLabel);

    // 信号连接
    connect(m_tabBar, &QTabBar::currentChanged, this, &SshTerminalWidget::onTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &SshTerminalWidget::onTabCloseRequested);

    // 主题
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &SshTerminalWidget::applyTheme);
    applyTheme();
}

SshTerminalWidget::~SshTerminalWidget()
{
    terminateSession();
}

// ============================================================
// ITerminalWidget 接口
// ============================================================

void SshTerminalWidget::startSession()
{
    // SSH 终端需要先配置连接，不自动启动
    emit configDialogRequested();
}

void SshTerminalWidget::terminateSession()
{
    for (auto& session : m_sessions) {
        cleanupSession(session);
    }
    m_sessions.clear();
}

void SshTerminalWidget::executeCommand(const QString& command)
{
    int idx = m_tabBar->currentIndex();
    if (idx < 0 || idx >= m_sessions.size()) return;

    SshRemoteSession& session = m_sessions[idx];
    if (session.client && session.shellChannelId >= 0) {
        session.client->writeShell(session.shellChannelId, (command + "\n").toUtf8());
    }
}

void SshTerminalWidget::setWorkingDirectory(const QString& dirPath)
{
    Q_UNUSED(dirPath)
    // SSH 终端的工作目录由远程服务器决定
}

bool SshTerminalWidget::isRunning() const
{
    int idx = m_tabBar->currentIndex();
    if (idx < 0 || idx >= m_sessions.size()) return false;
    return m_sessions[idx].client && m_sessions[idx].client->isConnected();
}

// ============================================================
// SSH 连接
// ============================================================

bool SshTerminalWidget::connectToHost(const SshConnectionConfig& config)
{
    m_sessionCounter++;

    // 创建 SSH 客户端
    auto* client = new SshClient(this);

    // 先建立连接
    if (!client->connect(config)) {
        QString err = client->lastError();
        delete client;
        m_statusLabel->setText(tr("连接失败: %1").arg(err));
        return false;
    }

    // 打开 Shell 通道
    int channelId = client->openShellChannel();
    if (channelId < 0) {
        QString err = client->lastError();
        client->disconnect();
        delete client;
        m_statusLabel->setText(tr("打开Shell失败: %1").arg(err));
        return false;
    }

    // 创建会话
    SshRemoteSession session;
    session.client = client;
    session.shellChannelId = channelId;
    session.connectionId = QStringLiteral("ssh_%1").arg(m_sessionCounter);
    session.displayName = config.name.isEmpty()
                              ? QStringLiteral("%1@%2").arg(config.username, config.host)
                              : config.name;

    // 创建页面
    session.pageWidget = new QWidget(m_sessionStack);
    auto* pageLayout = new QVBoxLayout(session.pageWidget);
    pageLayout->setContentsMargins(4, 4, 4, 4);
    pageLayout->setSpacing(0);

    session.view = new TerminalView(session.pageWidget);
    session.view->setObjectName(QStringLiteral("sshTerminalOutput"));
    pageLayout->addWidget(session.view, 1);

    m_sessionStack->addWidget(session.pageWidget);

    // 连接信号
    connect(client, &ISshClient::shellDataReady,
            this, &SshTerminalWidget::onShellDataReady);
    connect(client, &ISshClient::shellClosed,
            this, &SshTerminalWidget::onShellClosed);
    connect(client, &ISshClient::connectionStateChanged,
            this, &SshTerminalWidget::onConnectionStateChanged);

    // 用户输入 → SSH 通道
    connect(session.view, &TerminalView::commandSubmitted,
            this, [client, channelId](const QString& cmd) {
        if (client && client->isConnected()) {
            client->writeShell(channelId, (cmd + "\r\n").toUtf8());
        }
    });

    // 中断请求
    connect(session.view, &TerminalView::interruptRequested,
            this, [client]() {
        if (client && client->isConnected()) {
            client->writeShell(-1, QByteArray(1, 0x03));  // Ctrl+C
        }
    });

    // 添加标签页
    int index = m_tabBar->addTab(session.displayName);
    m_sessions.insert(index, session);
    m_tabBar->setCurrentIndex(index);

    // 显示欢迎信息
    const auto& palette = ThemeManager::instance().currentPalette();
    session.view->showWelcome(
        tr("=== SSH 连接: %1 ===\n主机: %2:%3\n\n").arg(session.displayName, config.host).arg(config.port),
        palette.accentPrimary
    );

    // P3-M01 子项2: tmux 会话持久化
    // 启用条件：用户在配置中勾选 useTmux，或未勾选但远程有 tmux 时提示用户
    bool useTmux = config.useTmux;
    QString tmuxName = config.tmuxSessionName;
    if (tmuxName.isEmpty() && !config.name.isEmpty()) {
        tmuxName = QStringLiteral("scnb_%1").arg(config.name);
    }

    QString tmuxPath = detectTmux(client);
    if (tmuxPath.isEmpty()) {
        // 远程未安装 tmux：跳过持久化
        if (useTmux) {
            session.view->showWelcome(
                tr("[tmux] 远程主机未安装 tmux，会话持久化不可用\n"),
                palette.fgSecondary);
            LOG_WARN("[SshTerminalWidget] 远程未安装 tmux，跳过持久化: " << config.name.toStdString());
        }
    } else {
        // 远程已安装 tmux
        if (!useTmux) {
            // 用户尚未启用 → 提示是否启用
            int ret = ModernDialog::question(
                this, tr("终端持久化"),
                tr("远程主机已安装 tmux。\n是否启用会话持久化？\n\n"
                   "启用后，断开重连时可恢复上次的终端会话。"));
            useTmux = (ret == ModernDialog::ROLE_ACCEPT);
        }
        if (useTmux) {
            // 尝试 attach 已有会话，不存在则 new
            // 通过 SSH 执行 `tmux has-session -t <name>` 判断会话是否存在
            QString checkCmd = QStringLiteral("tmux has-session -t '%1' 2>/dev/null && echo SCNB_TMUX_EXISTS").arg(tmuxName);
            QString checkResult = client->executeCommand(checkCmd, 3000);
            if (checkResult.contains(QStringLiteral("SCNB_TMUX_EXISTS"))) {
                // 会话已存在 → attach
                attachTmuxSession(tmuxName);
                session.view->showWelcome(
                    tr("[tmux] 已附加到会话: %1\n").arg(tmuxName),
                    palette.accentPrimary);
            } else {
                // 会话不存在 → new
                startTmuxSession(tmuxName);
                session.view->showWelcome(
                    tr("[tmux] 已新建会话: %1\n").arg(tmuxName),
                    palette.accentPrimary);
            }
            // 记录到会话结构（写回 m_sessions）
            // 注意：此时 m_sessions[index] 已插入，需同步状态
            m_sessions[index].useTmux = true;
            m_sessions[index].tmuxSessionName = tmuxName;
            LOG_INFO("[SshTerminalWidget] tmux 持久化已启用: " << tmuxName.toStdString());

            // 回写到会话配置（持久化用户选择）
            SshConnectionConfig updated = config;
            updated.useTmux = true;
            updated.tmuxSessionName = tmuxName;
            SshSessionManager::instance().saveConfig(updated);
        }
    }

    m_statusLabel->setText(tr("已连接: %1").arg(session.displayName));
    applyTheme();
    updateEmptyHint();

    return true;
}

// ============================================================
// P3-M01 子项2: 远程终端持久化（tmux）
// ============================================================

QString SshTerminalWidget::detectTmux(SshClient* client) const
{
    if (!client || !client->isConnected()) return QString();
    // 执行 `which tmux` 检测，返回值首行去除换行
    QString out = client->executeCommand(QStringLiteral("which tmux 2>/dev/null"), 3000);
    out = out.trimmed();
    // 兜底：某些系统 which 无输出时尝试 command -v
    if (out.isEmpty() || out.contains(QStringLiteral("no tmux"), Qt::CaseInsensitive)) {
        out = client->executeCommand(QStringLiteral("command -v tmux 2>/dev/null"), 3000).trimmed();
    }
    // 校验：路径不为空且不包含错误信息
    if (!out.isEmpty() && !out.contains(QStringLiteral("not found"), Qt::CaseInsensitive)) {
        LOG_INFO("[SshTerminalWidget] 检测到 tmux: " << out.toStdString());
        return out;
    }
    return QString();
}

void SshTerminalWidget::attachTmuxSession(const QString& sessionName)
{
    if (sessionName.isEmpty()) {
        LOG_WARN("[SshTerminalWidget] attachTmuxSession: sessionName 为空");
        return;
    }
    int idx = m_tabBar->currentIndex();
    if (idx < 0 || idx >= m_sessions.size()) return;
    const auto& session = m_sessions[idx];
    if (!session.client || session.shellChannelId < 0) return;

    // 向 Shell 通道发送 `tmux attach -t <name>` 命令
    // 使用 -d 选项：detach 其他客户端，避免冲突
    QString cmd = QStringLiteral("tmux attach -d -t '%1'\r\n").arg(sessionName);
    session.client->writeShell(session.shellChannelId, cmd.toUtf8());
    LOG_INFO("[SshTerminalWidget] 已发送 tmux attach 命令: " << sessionName.toStdString());
}

void SshTerminalWidget::startTmuxSession(const QString& sessionName)
{
    if (sessionName.isEmpty()) {
        LOG_WARN("[SshTerminalWidget] startTmuxSession: sessionName 为空");
        return;
    }
    int idx = m_tabBar->currentIndex();
    if (idx < 0 || idx >= m_sessions.size()) return;
    const auto& session = m_sessions[idx];
    if (!session.client || session.shellChannelId < 0) return;

    // 向 Shell 通道发送 `tmux new -s <name>` 命令
    QString cmd = QStringLiteral("tmux new -s '%1'\r\n").arg(sessionName);
    session.client->writeShell(session.shellChannelId, cmd.toUtf8());
    LOG_INFO("[SshTerminalWidget] 已发送 tmux new 命令: " << sessionName.toStdString());
}

void SshTerminalWidget::closeTab(int index)
{
    onTabCloseRequested(index);
}

// ============================================================
// 标签页事件
// ============================================================

void SshTerminalWidget::onTabChanged(int index)
{
    if (index >= 0 && index < m_sessionStack->count()) {
        m_sessionStack->setCurrentIndex(index);
    }

    if (index >= 0 && index < m_sessions.size()) {
        auto& session = m_sessions[index];
        if (session.view) {
            session.view->focusInputEnd();
            session.view->setFocus();
        }
        if (session.client) {
            m_statusLabel->setText(
                session.client->isConnected()
                    ? tr("已连接: %1").arg(session.displayName)
                    : tr("已断开: %1").arg(session.displayName));
        }
    }
}

void SshTerminalWidget::onTabCloseRequested(int index)
{
    if (index < 0 || index >= m_sessions.size()) return;

    SshRemoteSession& session = m_sessions[index];
    cleanupSession(session);

    if (session.pageWidget) {
        m_sessionStack->removeWidget(session.pageWidget);
        session.pageWidget->deleteLater();
    }

    m_tabBar->removeTab(index);
    m_sessions.removeAt(index);

    updateEmptyHint();

    if (m_sessions.isEmpty()) {
        m_statusLabel->setText(tr("未连接"));
        // 允许再次自动弹出配置
        m_hasShownConfig = false;
    }
}

void SshTerminalWidget::onAddTabClicked()
{
    emit configDialogRequested();
}

// ============================================================
// SSH 事件
// ============================================================

void SshTerminalWidget::onShellDataReady(int channelId, const QByteArray& data)
{
    SshRemoteSession* session = findSessionByChannel(channelId);
    if (session && session->view) {
        session->view->appendOutput(data);
    }
}

void SshTerminalWidget::onShellClosed(int channelId)
{
    SshRemoteSession* session = findSessionByChannel(channelId);
    if (!session) return;

    const auto& palette = ThemeManager::instance().currentPalette();
    if (session->view) {
        session->view->showWelcome(tr("\n[SSH连接已断开]\n"), palette.errorColor);
    }
    m_statusLabel->setText(tr("已断开: %1").arg(session->displayName));
}

void SshTerminalWidget::onConnectionStateChanged(bool connected)
{
    auto* client = qobject_cast<SshClient*>(sender());
    if (!client) return;

    // 查找对应会话
    for (auto& session : m_sessions) {
        if (session.client == client) {
            if (!connected) {
                if (session.view) {
                    const auto& palette = ThemeManager::instance().currentPalette();
                    session.view->showWelcome(tr("\n[SSH连接已断开]\n"), palette.errorColor);
                }
                m_statusLabel->setText(tr("已断开: %1").arg(session.displayName));
            }
            emit connectionStatusChanged(session.connectionId, connected);
            break;
        }
    }
}

// ============================================================
// 会话管理
// ============================================================

void SshTerminalWidget::setupSession(SshRemoteSession& session)
{
    Q_UNUSED(session)
}

void SshTerminalWidget::cleanupSession(SshRemoteSession& session)
{
    if (session.client) {
        session.client->disconnect();
        session.client->deleteLater();
        session.client = nullptr;
    }
    session.shellChannelId = -1;
}

SshRemoteSession* SshTerminalWidget::findSessionByChannel(int channelId)
{
    for (auto& session : m_sessions) {
        if (session.shellChannelId == channelId) return &session;
    }
    return nullptr;
}

// ============================================================
// 主题
// ============================================================

void SshTerminalWidget::applyTheme()
{
    const auto& palette = ThemeManager::instance().currentPalette();
    bool isLight = palette.bgEditor.lightness() > 128;

    QString tabBarBg = isLight ? palette.bgTabBar.name() : QStringLiteral("#252526");
    QString tabBarBorder = isLight ? palette.borderDefault.name() : QStringLiteral("#3c3c3c");
    QString tabColor = isLight ? palette.fgSecondary.name() : QStringLiteral("#969696");
    QString tabBg = isLight ? palette.bgTabInactive.name() : QStringLiteral("#2d2d2d");
    QString tabSelColor = isLight ? palette.fgPrimary.name() : QStringLiteral("#ffffff");
    QString tabSelBg = isLight ? palette.bgTabActive.name() : QStringLiteral("#1e1e1e");
    QString tabHoverColor = isLight ? palette.fgSecondary.name() : QStringLiteral("#cccccc");
    QString tabHoverBg = isLight ? palette.bgHover.name() : QStringLiteral("#383838");

    m_tabBar->setStyleSheet(
        QStringLiteral(
            "QTabBar { background: %1; border-bottom: 1px solid %2; }"
            "QTabBar::tab { color: %3; font-size: 11px; padding: 5px 12px;"
            "            border: 1px solid transparent; border-bottom: none;"
            "            border-top-left-radius: 3px; border-top-right-radius: 3px;"
            "            background-color: %4; min-width: 70px; }"
            "QTabBar::tab:selected { color: %5; background-color: %6;"
            "                   border: 1px solid %2; border-bottom: 1px solid %6; }"
            "QTabBar::tab:hover:!selected { color: %7; background-color: %8; }"
        ).arg(tabBarBg, tabBarBorder, tabColor, tabBg,
              tabSelColor, tabSelBg, tabHoverColor, tabHoverBg)
    );

    // 新建按钮样式
    QString btnColor = isLight ? palette.fgSecondary.name() : QStringLiteral("#969696");
    QString btnHoverBg = isLight ? palette.bgHover.name() : QStringLiteral("#383838");
    m_addTabBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton { color: %1; font-size: 16px; font-weight: bold;"
            "              border: none; background: transparent; }"
            "QPushButton:hover { background: %2; border-radius: 3px; }"
        ).arg(btnColor, btnHoverBg)
    );

    // 容器背景
    m_tabBarContainer->setStyleSheet(
        QStringLiteral("QWidget#sshTabBarContainer { background: %1; }").arg(tabBarBg)
    );

    // 状态栏
    QString statusFg = isLight ? palette.fgSecondary.name() : QStringLiteral("#888888");
    QString statusBg = isLight ? palette.bgEditor.name() : QStringLiteral("#1e1e1e");
    m_statusLabel->setStyleSheet(
        QStringLiteral(
            "QLabel { color: %1; background: %2; font-size: 11px; border-top: 1px solid %3; }"
        ).arg(statusFg, statusBg, tabBarBorder)
    );

    // 终端视图样式
    QFont font(
        ConfigManager::instance().getValue("Terminal/fontFamily", QStringLiteral("Consolas")).toString(),
        ConfigManager::instance().getValue("Terminal/fontSize", 12).toInt()
    );

    QColor fgColor = isLight ? palette.fgPrimary : QColor(QStringLiteral("#cccccc"));
    QColor bgColor = isLight ? palette.bgEditor : QColor(QStringLiteral("#1e1e1e"));

    for (auto& session : m_sessions) {
        if (session.view) {
            session.view->setTerminalFont(font);
            session.view->setForegroundColor(fgColor);
            session.view->setBackgroundColor(bgColor);
        }
    }
}

// ============================================================
// 空状态引导 + showEvent
// ============================================================

void SshTerminalWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // 首次显示且无会话时，自动弹出配置对话框（延迟100ms避免阻塞UI）
    if (!m_hasShownConfig && m_sessions.isEmpty()) {
        m_hasShownConfig = true;
        QTimer::singleShot(100, this, &SshTerminalWidget::onAddTabClicked);
    }

    // 更新空提示位置和可见性
    updateEmptyHint();
}

void SshTerminalWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // 窗口大小变化时更新空提示位置
    if (m_emptyHintLabel && m_sessionStack) {
        m_emptyHintLabel->setGeometry(m_sessionStack->rect());
    }
}

void SshTerminalWidget::updateEmptyHint()
{
    if (m_emptyHintLabel && m_sessionStack) {
        bool isEmpty = m_sessions.isEmpty();
        m_emptyHintLabel->setVisible(isEmpty);
        if (isEmpty) {
            // 调整大小覆盖整个会话区域
            m_emptyHintLabel->setGeometry(m_sessionStack->rect());

            const auto& palette = ThemeManager::instance().currentPalette();
            bool isLight = palette.bgEditor.lightness() > 128;
            QString fgColor = isLight ? palette.fgSecondary.name() : QStringLiteral("#888888");
            QString accentColor = palette.accentPrimary.name();
            QString linkStyle = QStringLiteral("color: %1; text-decoration: none; font-weight: bold;")
                                      .arg(accentColor);

            m_emptyHintLabel->setText(
                QStringLiteral(
                    "<div style='text-align:center;'>"
                    "<p style='font-size:28px; margin-bottom:8px;'>&#128272;</p>"
                    "<h2 style='margin:0 0 12px 0; color:%1;'>SSH 远程终端</h2>"
                    "<p style='margin:4px 0; color:%1;'>连接到远程服务器，在编辑器中直接操作远程 Shell</p>"
                    "<p style='margin:16px 0 8px 0;'>"
                    "  <a href='connect' style='%3'>+ 新建 SSH 连接</a>"
                    "</p>"
                    "<p style='margin:4px 0; color:%1; font-size:11px;'>"
                    "快捷键 <b>Ctrl+Shift+`</b> &nbsp;|&nbsp; 点击上方 <b>+</b> 按钮"
                    "</p>"
                    "</div>"
                ).arg(fgColor, linkStyle)
            );
            m_emptyHintLabel->raise();
        }
    }
}
