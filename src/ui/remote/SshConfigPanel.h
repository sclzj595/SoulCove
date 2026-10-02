#ifndef SSHCONFIGPANEL_H
#define SSHCONFIGPANEL_H

#include "interfaces/remote/ISshClient.h"

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QGroupBox>

/// @brief SSH 连接配置面板（内嵌标签页，对标 SettingsPage 布局风格）
///
/// 布局：左侧已保存连接列表 + 右侧连接配置表单
/// 作为 EditorTabBar 的自定义标签页打开，不弹独立窗口
class SshConfigPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SshConfigPanel(QWidget* parent = nullptr);

    /// 获取当前表单中的配置
    SshConnectionConfig currentConfig() const;

signals:
    /// 请求连接
    void connectRequested(const SshConnectionConfig& config);

    /// 配置已保存
    void configSaved(const SshConnectionConfig& config);

private slots:
    void onConnectClicked();
    void onSaveClicked();
    void onDeleteClicked();
    void onSavedSelectionChanged();
    void onAuthMethodChanged(int index);
    void onTestConnectionClicked();
    void onDeployLspClicked();   // P3-M01 子项3: 部署 LSP 到远程

private:
    void setupUi();
    void loadSavedConnections();
    void populateForm(const SshConnectionConfig& config);
    SshConnectionConfig gatherConfig() const;
    void applyTheme();
    /// 显示底部状态消息
    void showStatus(const QString& msg, bool isError);

    // 左侧：已保存连接列表
    QListWidget*   m_savedList = nullptr;
    QPushButton*   m_deleteBtn = nullptr;
    QLabel*        m_listTitleLabel = nullptr;

    // 右侧：连接配置表单
    QLineEdit*     m_nameEdit = nullptr;
    QLineEdit*     m_hostEdit = nullptr;
    QSpinBox*      m_portSpin = nullptr;
    QLineEdit*     m_userEdit = nullptr;
    QComboBox*     m_authCombo = nullptr;
    QLineEdit*     m_passwordEdit = nullptr;
    QLineEdit*     m_keyPathEdit = nullptr;
    QPushButton*   m_keyBrowseBtn = nullptr;
    QLineEdit*     m_passphraseEdit = nullptr;
    QSpinBox*      m_timeoutSpin = nullptr;
    QSpinBox*      m_keepaliveSpin = nullptr;

    // 操作按钮
    QPushButton*   m_testBtn = nullptr;
    QPushButton*   m_saveBtn = nullptr;
    QPushButton*   m_connectBtn = nullptr;
    QPushButton*   m_deployLspBtn = nullptr;  // P3-M01 子项3: 部署 LSP 到远程

    // 状态提示
    QLabel*        m_statusLabel = nullptr;
};

#endif // SSHCONFIGPANEL_H
