#ifndef DEBUGVIEW_H
#define DEBUGVIEW_H

#include "core/debug/DebugManager.h"  // DebugState / DebugManager

#include <QWidget>

class QTableWidget;
class QListWidget;
class QToolButton;
class QLabel;
class DebugManager;

/// @brief 调试视图面板（P3-M04 子项3）
///
/// 布局：
/// ┌─────────────────────────────────────────┐
/// │ 工具栏: ▶开始/继续 ▶|跳过 →进入 ←跳出 ⏹停止 │
/// ├─────────────────────────────────────────┤
/// │ 变量表（变量名 | 值 | 类型）              │
/// ├─────────────────────────────────────────┤
/// │ 调用栈列表                                │
/// │ 断点列表                                  │
/// └─────────────────────────────────────────┘
///
/// 设计说明：
/// - 自含 UI + 通过 DebugManager 信号驱动刷新
/// - 不直接调用 GDB，所有调试操作委托给 DebugManager
/// - 由 Widget 创建并注入 DebugManager 实例（支持 DI 便于测试）
class DebugView : public QWidget
{
    Q_OBJECT

public:
    explicit DebugView(QWidget* parent = nullptr);

    /// @brief 注入 DebugManager 实例（必须调用，否则按钮无响应）
    void setDebugManager(DebugManager* manager);

    /// @brief 刷新按钮启用状态（基于当前 DebugState）
    void updateButtonStates();

signals:
    /// @brief 用户点击「开始调试」请求（Widget 弹文件选择 / 取当前可执行文件）
    void startDebugRequested();

    /// @brief 调试器跳转到 file:line（编辑器聚焦 + 高亮当前行）
    /// @param file 源文件路径
    /// @param line 行号（1-based）
    void jumpToLocationRequested(const QString& file, int line);

public slots:
    /// @brief 添加一个断点到断点列表 UI
    void addBreakpointToList(const QString& file, int line);

    /// @brief 从断点列表 UI 移除断点
    void removeBreakpointFromList(const QString& file, int line);

    /// @brief 清空断点列表 UI
    void clearBreakpointList();

private slots:
    // 工具栏按钮槽
    void onStartOrContinueClicked();
    void onStepOverClicked();
    void onStepIntoClicked();
    void onStepOutClicked();
    void onStopClicked();

    // DebugManager 信号槽
    void onStateChanged(DebugState state);
    void onBreakpointHit(const QString& file, int line);
    void onOutputMessage(const QString& msg);
    void onVariablesReady(const QVariantList& vars);

private:
    // === UI 组件 ===
    QToolButton* m_btnStartContinue = nullptr;
    QToolButton* m_btnStepOver      = nullptr;
    QToolButton* m_btnStepInto      = nullptr;
    QToolButton* m_btnStepOut       = nullptr;
    QToolButton* m_btnStop          = nullptr;
    QLabel*      m_stateLabel       = nullptr;        // 状态文字
    QTableWidget* m_varsTable       = nullptr;        // 变量查看表
    QListWidget*  m_callStackList   = nullptr;        // 调用栈
    QListWidget*  m_breakpointList  = nullptr;        // 断点列表

    DebugManager* m_debugManager = nullptr;            // 非拥有指针
    DebugState    m_state = DebugState::Stopped;
};

#endif // DEBUGVIEW_H
