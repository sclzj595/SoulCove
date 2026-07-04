#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QContextMenuEvent>

/// @brief 自定义标题栏组件（VSCode风格）
/// 布局：[图标|标题|新建打开保存|弹簧|—□✕]
/// 工具按钮集成在标题栏内，消除独立工具栏行
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget* parent = nullptr);

    /// 设置窗口标题
    void setTitle(const QString& title);

    /// 获取工具按钮（供外部连接信号槽）
    QPushButton* newButton() const { return m_btnNew; }
    QPushButton* openButton() const { return m_btnOpen; }
    QPushButton* saveButton() const { return m_btnSave; }
    QPushButton* settingsButton() const { return m_btnSettings; }
    /// P3-M05: 视图菜单按钮（含语言切换子菜单）
    QPushButton* viewButton() const { return m_btnView; }

    /// 获取窗口控制按钮
    QPushButton* minimizeButton() const { return m_btnMinimize; }
    QPushButton* maximizeButton() const { return m_btnMaximize; }
    QPushButton* closeButton() const { return m_btnClose; }

    /// 更新最大化按钮图标（跟随窗口状态）
    void updateMaximizeIcon(bool isMaximized);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

    // 右键菜单信号
    void openFolderRequested();
    void openFileRequested();
    void newFileRequested();
    void saveRequested();       // 保存文件（菜单触发）
    void refreshRequested();
    void quitRequested();
    // P2-H04: 工作区持久化菜单信号
    void saveWorkspaceRequested();  // 保存工作区到 .scnb-workspace 文件
    void openWorkspaceRequested();  // 从 .scnb-workspace 文件打开工作区
    // P3-M01 子项4: 挂载远程工作区（文件菜单触发）
    void mountRemoteWorkspaceRequested();

    // P3-M05: 国际化语言切换信号
    /// 语言切换请求 — 参数为语言代码（"zh_CN" / "en_US" / "system"）
    void languageChangeRequested(const QString& langCode);

protected:
    /// @brief 右键上下文菜单事件
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // 左侧
    QLabel*      m_labelIcon;
    QLabel*      m_labelTitle;

    // 工具按钮
    QPushButton* m_btnNew;
    QPushButton* m_btnOpen;
    QPushButton* m_btnSave;
    QPushButton* m_btnView;      ///< P3-M05: 视图菜单按钮
    QPushButton* m_btnSettings;

    // 窗口控制
    QPushButton* m_btnMinimize;
    QPushButton* m_btnMaximize;
    QPushButton* m_btnClose;
};

#endif // TITLEBAR_H
