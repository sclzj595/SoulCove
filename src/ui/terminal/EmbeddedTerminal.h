#ifndef EMBEDDEDTERMINAL_H
#define EMBEDDEDTERMINAL_H

#include "interfaces/ui/ITerminalWidget.h"
#include "ui/terminal/TerminalView.h"
#include "ui/terminal/TerminalBackend.h"

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabBar>
#include <QStackedWidget>
#include <QString>
#include <QMap>

/// @brief 单个终端会话（模块化架构）
struct TerminalSession {
    TerminalView*     view = nullptr;       // 统一视图（输入输出合一）
    TerminalBackend*  backend = nullptr;    // 进程后端
    QWidget*          pageWidget = nullptr; // 页面容器
    QString           name;
    QString           type;                 // "cmd" or "powershell"
};

/// @brief 内嵌终端组件（多标签页容器）
///
/// 架构：
///   EmbeddedTerminal（容器）
///   ├── QTabBar（标签管理）
///   ├── SearchBar（搜索功能）
///   └── QStackedWidget
///       └── TerminalSession[] × N
///           ├── TerminalView（统一视图：显示+内联输入）
///           └── TerminalBackend（进程管理）
///
/// 功能：多标签页、内容搜索、复制粘贴、主题联动、右键菜单
class EmbeddedTerminal : public QWidget, public ITerminalWidget
{
    Q_OBJECT

public:
    explicit EmbeddedTerminal(QWidget* parent = nullptr);
    ~EmbeddedTerminal() override;

    // === ITerminalWidget 接口实现 ===
    void startSession() override;
    void terminateSession() override;
    void executeCommand(const QString& command) override;
    void setWorkingDirectory(const QString& dirPath) override;
    bool isRunning() const override;
    QWidget* asWidget() override { return this; }

    /// 新建终端标签页
    void addTerminalTab(const QString& type = QStringLiteral("cmd"));

    /// 关闭当前终端标签页
    void closeCurrentTab();

    /// 获取当前终端类型
    QString currentTerminalType() const;

    /// @brief P2-H01: 获取当前活动终端的后端（用于编辑器联动发送命令）
    /// @return 当前标签页的 TerminalBackend 指针，无活动标签时返回 nullptr
    TerminalBackend* currentBackend() const;

    /// @brief P2-H01: 附加已存在的后端到新标签页（终端复用/克隆）
    /// 不创建新进程，而是让新标签页共享传入的 backend（同一 shell 会话）
    /// @param backend 已存在的终端后端（引用计数会自动 +1）
    /// @param tabTitle 标签页标题（空则自动生成带 (clone) 后缀的标题）
    void attachExistingBackend(TerminalBackend* backend, const QString& tabTitle = QString());

signals:
    /// 终端标签页数量变化
    void tabCountChanged(int count);

private slots:
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void applyTheme();

    // 搜索相关槽
    void onSearchTextChanged(const QString& text);
    void onSearchNext();
    void onSearchPrev();
    void toggleSearchBar(bool visible);

protected:
    /// 右键菜单
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // 搜索功能
    QWidget* createSearchBar();
    void highlightSearchResults(const QString& keyword, const QColor& color);
    void clearSearchHighlights();

    // 会话管理
    void setupSession(TerminalSession& session);
    void cleanupSession(TerminalSession& session);

    // P2-H01: 克隆会话 — 将已有 backend 的信号连接到新视图（共享终端）
    void setupClonedSession(TerminalSession& session, TerminalBackend* backend);

    // UI 组件
    QWidget*        m_tabBarContainer = nullptr;  // 标签栏容器（含TabBar + 新建按钮）
    QHBoxLayout*   m_tabBarLayout = nullptr;
    QTabBar*        m_tabBar;
    QPushButton*    m_addTabBtn = nullptr;        // 新建终端按钮
    QStackedWidget* m_sessionStack;
    QVBoxLayout*    m_layout;

    // 会话数据
    QMap<int, TerminalSession> m_sessions;
    QString        m_workingDir;
    int            m_sessionCounter = 0;

    // 搜索栏组件
    QWidget*       m_searchBar = nullptr;
    QLineEdit*     m_searchInput = nullptr;
    QLabel*        m_searchResultLabel = nullptr;
    QString        m_searchKeyword;
    QList<QTextCursor> m_searchMatches;
    int            m_currentSearchIndex = -1;
};

#endif // EMBEDDEDTERMINAL_H
