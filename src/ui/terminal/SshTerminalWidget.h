#ifndef SSHWIDGET_H
#define SSHWIDGET_H

#include "interfaces/ui/ITerminalWidget.h"
#include "interfaces/remote/ISshClient.h"
#include "ui/terminal/TerminalView.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QTabBar>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

class SshClient;

/// @brief SSH 远程终端会话
struct SshRemoteSession {
    SshClient*   client = nullptr;
    int          shellChannelId = -1;
    TerminalView* view = nullptr;
    QWidget*     pageWidget = nullptr;
    QString      connectionId;
    QString      displayName;
};

/// @brief SSH 远程终端组件
///
/// 架构：
///   SshTerminalWidget (ITerminalWidget)
///   ├── 标签栏（多远程终端切换）
///   ├── QStackedWidget
///   │   └── SshRemoteSession[] × N
///   │       ├── TerminalView（终端视图）
///   │       └── SshClient（SSH连接）
///   └── 状态栏（连接状态指示）
class SshTerminalWidget : public QWidget, public ITerminalWidget
{
    Q_OBJECT

public:
    explicit SshTerminalWidget(QWidget* parent = nullptr);
    ~SshTerminalWidget() override;

    // === ITerminalWidget 接口实现 ===
    void startSession() override;
    void terminateSession() override;
    void executeCommand(const QString& command) override;
    void setWorkingDirectory(const QString& dirPath) override;
    bool isRunning() const override;
    QWidget* asWidget() override { return this; }

    /// 连接到远程主机并打开 Shell
    bool connectToHost(const SshConnectionConfig& config);

    /// 关闭指定标签页
    void closeTab(int index);

signals:
    /// 请求打开连接配置对话框
    void configDialogRequested();

    /// 连接状态变化
    void connectionStatusChanged(const QString& connectionId, bool connected);

private slots:
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onShellDataReady(int channelId, const QByteArray& data);
    void onShellClosed(int channelId);
    void onConnectionStateChanged(bool connected);
    void applyTheme();
    void onAddTabClicked();

private:
    void setupSession(SshRemoteSession& session);
    void cleanupSession(SshRemoteSession& session);
    SshRemoteSession* findSessionByChannel(int channelId);
    /// 更新空状态引导提示（无会话时显示，有会话时隐藏）
    void updateEmptyHint();

    // UI
    QWidget*        m_tabBarContainer = nullptr;
    QHBoxLayout*   m_tabBarLayout = nullptr;
    QTabBar*        m_tabBar = nullptr;
    QPushButton*    m_addTabBtn = nullptr;
    QStackedWidget* m_sessionStack = nullptr;
    QLabel*         m_statusLabel = nullptr;
    QLabel*         m_emptyHintLabel = nullptr;  // 空状态引导提示
    QVBoxLayout*    m_layout = nullptr;

    // 会话数据
    QList<SshRemoteSession> m_sessions;
    int m_sessionCounter = 0;
    bool m_hasShownConfig = false;  // 是否已弹出过配置对话框（避免重复弹）

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
};

#endif // SSHWIDGET_H
