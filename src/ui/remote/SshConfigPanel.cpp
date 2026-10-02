#include "ui/remote/SshConfigPanel.h"
#include "core/remote/SshClient.h"
#include "core/remote/SftpClient.h"
#include "core/remote/SshSessionManager.h"
#include "core/remote/RemoteLspDeployer.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"
#include "ui/dialog/ModernDialog.h"

#include <QFormLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>

// ============================================================
// 构造
// ============================================================

SshConfigPanel::SshConfigPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sshConfigPanel"));
    setupUi();
    loadSavedConnections();
    applyTheme();

    // 主题切换时刷新样式
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &SshConfigPanel::applyTheme);
}

// ============================================================
// UI 布局（对标 SettingsPage 卡片式风格）
// ============================================================

void SshConfigPanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 12);
    mainLayout->setSpacing(0);

    // ========== 左侧：已保存连接列表 ==========
    auto* leftWidget = new QWidget(this);
    leftWidget->setObjectName(QStringLiteral("sshConfigLeft"));
    leftWidget->setFixedWidth(220);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 8, 0);
    leftLayout->setSpacing(8);

    m_listTitleLabel = new QLabel(tr("已保存的连接"), leftWidget);
    m_listTitleLabel->setObjectName(QStringLiteral("sshConfigListTitle"));

    m_savedList = new QListWidget(leftWidget);
    m_savedList->setObjectName(QStringLiteral("sshConfigList"));
    m_savedList->setAlternatingRowColors(true);
    connect(m_savedList, &QListWidget::itemSelectionChanged,
            this, &SshConfigPanel::onSavedSelectionChanged);

    // 双击快速连接
    connect(m_savedList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) {
        onConnectClicked();
    });

    auto* delBtnLayout = new QHBoxLayout();
    delBtnLayout->setContentsMargins(0, 0, 0, 0);
    m_deleteBtn = new QPushButton(tr("删除"), leftWidget);
    m_deleteBtn->setObjectName(QStringLiteral("sshDeleteBtn"));
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setFixedWidth(60);
    connect(m_deleteBtn, &QPushButton::clicked, this, &SshConfigPanel::onDeleteClicked);
    delBtnLayout->addStretch();
    delBtnLayout->addWidget(m_deleteBtn);

    leftLayout->addWidget(m_listTitleLabel);
    leftLayout->addWidget(m_savedList, 1);
    leftLayout->addLayout(delBtnLayout);

    // ========== 右侧：连接配置表单 ==========
    auto* rightWidget = new QWidget(this);
    rightWidget->setObjectName(QStringLiteral("sshConfigRight"));
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(24, 4, 8, 4);
    rightLayout->setSpacing(10);

    // 标题
    auto* titleLabel = new QLabel(tr("新建 SSH 连接"), rightWidget);
    titleLabel->setObjectName(QStringLiteral("sshConfigTitle"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    // --- 连接设置卡片 ---
    auto* formGroup = new QGroupBox(tr("连接设置"), rightWidget);
    formGroup->setObjectName(QStringLiteral("sshConfigCard"));
    auto* formLayout = new QFormLayout(formGroup);
    formLayout->setSpacing(10);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_nameEdit = new QLineEdit(formGroup);
    m_nameEdit->setPlaceholderText(tr("连接别名（可选）"));
    m_nameEdit->setMinimumWidth(280);
    formLayout->addRow(tr("别名:"), m_nameEdit);

    auto* hostRow = new QHBoxLayout();
    hostRow->setSpacing(6);
    m_hostEdit = new QLineEdit(formGroup);
    m_hostEdit->setPlaceholderText(tr("主机地址或 IP"));
    m_portSpin = new QSpinBox(formGroup);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(22);
    m_portSpin->setFixedWidth(70);
    hostRow->addWidget(m_hostEdit, 1);
    hostRow->addWidget(new QLabel(tr("端口:"), formGroup));
    hostRow->addWidget(m_portSpin);
    formLayout->addRow(tr("主机:"), hostRow);

    m_userEdit = new QLineEdit(formGroup);
    m_userEdit->setPlaceholderText(tr("用户名"));
    formLayout->addRow(tr("用户名:"), m_userEdit);

    m_authCombo = new QComboBox(formGroup);
    m_authCombo->addItem(tr("密码认证"), static_cast<int>(SshConnectionConfig::Password));
    m_authCombo->addItem(tr("公钥认证"), static_cast<int>(SshConnectionConfig::PublicKey));
    connect(m_authCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SshConfigPanel::onAuthMethodChanged);
    formLayout->addRow(tr("认证方式:"), m_authCombo);

    // 密码认证区域
    m_passwordEdit = new QLineEdit(formGroup);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("登录密码"));
    formLayout->addRow(tr("密码:"), m_passwordEdit);

    // 公钥认证区域
    auto* keyRow = new QHBoxLayout();
    keyRow->setSpacing(6);
    m_keyPathEdit = new QLineEdit(formGroup);
    m_keyPathEdit->setPlaceholderText(tr("~/.ssh/id_rsa"));
    m_keyBrowseBtn = new QPushButton(tr("浏览..."), formGroup);
    m_keyBrowseBtn->setFixedWidth(64);
    connect(m_keyBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("选择私钥文件"),
                                                     QString(),
                                                     tr("所有文件 (*.*)"));
        if (!path.isEmpty()) m_keyPathEdit->setText(path);
    });
    keyRow->addWidget(m_keyPathEdit, 1);
    keyRow->addWidget(m_keyBrowseBtn);
    formLayout->addRow(tr("私钥文件:"), keyRow);

    m_passphraseEdit = new QLineEdit(formGroup);
    m_passphraseEdit->setEchoMode(QLineEdit::Password);
    m_passphraseEdit->setPlaceholderText(tr("私钥密码（可选）"));
    formLayout->addRow(tr("私钥密码:"), m_passphraseEdit);

    // 高级设置行
    auto* advRow = new QHBoxLayout();
    advRow->setSpacing(12);
    m_timeoutSpin = new QSpinBox(formGroup);
    m_timeoutSpin->setRange(3000, 60000);
    m_timeoutSpin->setValue(10000);
    m_timeoutSpin->setSingleStep(1000);
    m_timeoutSpin->setSuffix(tr(" ms"));
    m_timeoutSpin->setFixedWidth(110);
    m_keepaliveSpin = new QSpinBox(formGroup);
    m_keepaliveSpin->setRange(0, 300);
    m_keepaliveSpin->setValue(30);
    m_keepaliveSpin->setSuffix(tr(" s"));
    m_keepaliveSpin->setFixedWidth(90);
    advRow->addWidget(new QLabel(tr("超时:"), formGroup));
    advRow->addWidget(m_timeoutSpin);
    advRow->addWidget(new QLabel(tr("心跳:"), formGroup));
    advRow->addWidget(m_keepaliveSpin);
    advRow->addStretch();
    formLayout->addRow(tr("高级:"), advRow);

    // --- 操作按钮 + 状态提示 ---
    auto* btnBar = new QHBoxLayout();
    btnBar->setSpacing(10);

    m_testBtn = new QPushButton(tr("\u27A4 测试连接"), rightWidget);
    m_testBtn->setObjectName(QStringLiteral("sshTestBtn"));
    connect(m_testBtn, &QPushButton::clicked, this, &SshConfigPanel::onTestConnectionClicked);

    m_saveBtn = new QPushButton(tr("保存配置"), rightWidget);
    m_saveBtn->setObjectName(QStringLiteral("sshSaveBtn"));
    connect(m_saveBtn, &QPushButton::clicked, this, &SshConfigPanel::onSaveClicked);

    m_connectBtn = new QPushButton(tr("\u2192 连接"), rightWidget);
    m_connectBtn->setObjectName(QStringLiteral("sshConnectBtn"));
    m_connectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_connectBtn, &QPushButton::clicked, this, &SshConfigPanel::onConnectClicked);

    // P3-M01 子项3: 部署 LSP 按钮
    m_deployLspBtn = new QPushButton(tr("\u2191 部署 LSP"), rightWidget);
    m_deployLspBtn->setObjectName(QStringLiteral("sshDeployLspBtn"));
    m_deployLspBtn->setToolTip(tr("检测并部署 clangd 到远程主机"));
    connect(m_deployLspBtn, &QPushButton::clicked, this, &SshConfigPanel::onDeployLspClicked);

    btnBar->addWidget(m_testBtn);
    btnBar->addWidget(m_saveBtn);
    btnBar->addWidget(m_deployLspBtn);
    btnBar->addStretch();
    btnBar->addWidget(m_connectBtn);

    // 状态提示
    m_statusLabel = new QLabel(rightWidget);
    m_statusLabel->setObjectName(QStringLiteral("sshStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();

    // 组装右侧
    rightLayout->addWidget(titleLabel);
    rightLayout->addWidget(formGroup, 1);
    rightLayout->addLayout(btnBar);
    rightLayout->addWidget(m_statusLabel);

    // 左右分割
    mainLayout->addWidget(leftWidget);
    mainLayout->addWidget(rightWidget, 1);

    // 初始状态：显示密码认证，隐藏公钥
    onAuthMethodChanged(0);
}

// ============================================================
// 槽函数
// ============================================================

void SshConfigPanel::onConnectClicked()
{
    SshConnectionConfig config = gatherConfig();
    if (config.host.isEmpty() || config.username.isEmpty()) {
        showStatus(tr("请填写主机地址和用户名"), true);
        return;
    }
    showStatus(tr("正在连接..."), false);
    emit connectRequested(config);
}

void SshConfigPanel::onSaveClicked()
{
    SshConnectionConfig config = gatherConfig();
    if (config.host.isEmpty() || config.username.isEmpty()) {
        showStatus(tr("请填写主机地址和用户名"), true);
        return;
    }

    if (config.name.isEmpty()) {
        config.name = QStringLiteral("%1@%2").arg(config.username, config.host);
    }
    m_nameEdit->setText(config.name);  // 回显自动生成的别名

    SshSessionManager::instance().saveConfig(config);
    loadSavedConnections();  // 刷新列表
    showStatus(tr("配置已保存: %1").arg(config.name), false);
    emit configSaved(config);
}

void SshConfigPanel::onDeleteClicked()
{
    auto items = m_savedList->selectedItems();
    if (items.isEmpty()) return;

    QString name = items.first()->text();
    SshSessionManager::instance().removeSavedConfig(name);
    loadSavedConnections();
    showStatus(tr("已删除: %1").arg(name), false);
}

void SshConfigPanel::onSavedSelectionChanged()
{
    auto items = m_savedList->selectedItems();
    m_deleteBtn->setEnabled(!items.isEmpty());

    if (!items.isEmpty()) {
        populateForm(SshSessionManager::instance().savedConfig(items.first()->text()));
    }
}

void SshConfigPanel::onAuthMethodChanged(int index)
{
    bool isPassword = (index == 0);

    m_passwordEdit->setVisible(isPassword);
    m_keyPathEdit->setVisible(!isPassword);
    m_keyBrowseBtn->setVisible(!isPassword);
    m_passphraseEdit->setVisible(!isPassword);

    // 更新 formLayout 中对应 label 的可见性
    auto* formLayout = qobject_cast<QFormLayout*>(
        m_passwordEdit->parentWidget()->layout());
    if (formLayout) {
        for (int i = 0; i < formLayout->rowCount(); ++i) {
            auto* fieldItem = formLayout->itemAt(i, QFormLayout::FieldRole);
            if (fieldItem) {
                QWidget* w = fieldItem->widget();
                if (w == m_passwordEdit) {
                    if (auto* li = formLayout->itemAt(i, QFormLayout::LabelRole))
                        if (li->widget()) li->widget()->setVisible(isPassword);
                }
                if (w == m_keyPathEdit || w == m_passphraseEdit) {
                    if (auto* li = formLayout->itemAt(i, QFormLayout::LabelRole))
                        if (li->widget()) li->widget()->setVisible(!isPassword);
                }
            }
        }
    }
}

void SshConfigPanel::onTestConnectionClicked()
{
    SshConnectionConfig config = gatherConfig();
    if (config.host.isEmpty() || config.username.isEmpty()) {
        showStatus(tr("请填写主机地址和用户名"), true);
        return;
    }

    m_testBtn->setEnabled(false);
    m_testBtn->setText(tr("测试中..."));
    showStatus(tr("正在测试连接..."), false);

    auto* client = new SshClient(this);
    bool ok = client->connect(config);

    if (ok) {
        showStatus(tr("连接测试成功! \u2713"), false);
        client->disconnect();
    } else {
        showStatus(tr("连接失败: %1").arg(client->lastError()), true);
    }
    client->deleteLater();

    m_testBtn->setEnabled(true);
    m_testBtn->setText(tr("\u27A4 测试连接"));
}

// ============================================================
// P3-M01 子项3: 部署 LSP 到远程
// ============================================================

void SshConfigPanel::onDeployLspClicked()
{
    SshConnectionConfig config = gatherConfig();
    if (config.host.isEmpty() || config.username.isEmpty()) {
        showStatus(tr("请填写主机地址和用户名"), true);
        return;
    }

    m_deployLspBtn->setEnabled(false);
    m_deployLspBtn->setText(tr("部署中..."));
    showStatus(tr("正在连接远程主机..."), false);

    // 建立 SSH + SFTP 连接
    auto* client = new SshClient(this);
    if (!client->connect(config)) {
        showStatus(tr("连接失败: %1").arg(client->lastError()), true);
        client->deleteLater();
        m_deployLspBtn->setEnabled(true);
        m_deployLspBtn->setText(tr("\u2191 部署 LSP"));
        return;
    }

    SftpClient sftp(client);
    if (!sftp.init()) {
        showStatus(tr("SFTP 初始化失败: %1").arg(sftp.lastError()), true);
        client->disconnect();
        client->deleteLater();
        m_deployLspBtn->setEnabled(true);
        m_deployLspBtn->setText(tr("\u2191 部署 LSP"));
        return;
    }

    // 1. 检测远程 clangd
    RemoteLspDeployer deployer;
    QString existing = deployer.checkClangdInstalled(*client);
    if (!existing.isEmpty()) {
        // 已安装：保存路径到会话配置（key: SSH/<name>/remoteClangdPath）
        ConfigManager::instance().setValue(
            QStringLiteral("SSH/%1/remoteClangdPath").arg(config.name), existing);
        showStatus(tr("远程已安装 clangd: %1").arg(existing), false);
        client->disconnect();
        client->deleteLater();
        m_deployLspBtn->setEnabled(true);
        m_deployLspBtn->setText(tr("\u2191 部署 LSP"));
        return;
    }

    // 2. 未安装：提示是否上传本地 clangd
    int ret = ModernDialog::question(
        this, tr("部署 LSP"),
        tr("远程主机未检测到 clangd。\n是否上传本地 clangd 二进制到远程？\n\n"
           "（部署目录：~/.local/share/scnb/clangd）"));
    if (ret != ModernDialog::ROLE_ACCEPT) {
        showStatus(tr("已取消部署"), false);
        client->disconnect();
        client->deleteLater();
        m_deployLspBtn->setEnabled(true);
        m_deployLspBtn->setText(tr("\u2191 部署 LSP"));
        return;
    }

    // 3. 执行部署
    QString err;
    QString remotePath = deployer.deploy(*client, sftp, err);
    if (remotePath.isEmpty()) {
        showStatus(tr("部署失败: %1").arg(err), true);
    } else {
        // 4. 持久化远程 clangd 路径到会话配置
        if (config.name.isEmpty()) {
            config.name = QStringLiteral("%1@%2").arg(config.username, config.host);
        }
        ConfigManager::instance().setValue(
            QStringLiteral("SSH/%1/remoteClangdPath").arg(config.name), remotePath);
        showStatus(tr("部署成功: %1").arg(remotePath), false);
    }

    client->disconnect();
    client->deleteLater();
    m_deployLspBtn->setEnabled(true);
    m_deployLspBtn->setText(tr("\u2191 部署 LSP"));
}

// ============================================================
// 辅助方法
// ============================================================

void SshConfigPanel::loadSavedConnections()
{
    m_savedList->clear();
    for (const auto& config : SshSessionManager::instance().savedConfigs()) {
        m_savedList->addItem(config.name);
    }
}

void SshConfigPanel::populateForm(const SshConnectionConfig& config)
{
    // 阻止信号循环触发
    m_savedList->blockSignals(true);
    m_authCombo->blockSignals(true);

    m_nameEdit->setText(config.name);
    m_hostEdit->setText(config.host);
    m_portSpin->setValue(config.port);
    m_userEdit->setText(config.username);
    m_passwordEdit->setText(config.password);
    m_keyPathEdit->setText(config.privateKeyPath);
    m_passphraseEdit->setText(config.passphrase);
    m_timeoutSpin->setValue(config.connectTimeout);
    m_keepaliveSpin->setValue(config.keepaliveInterval);

    int authIdx = (config.authMethod == SshConnectionConfig::PublicKey) ? 1 : 0;
    m_authCombo->setCurrentIndex(authIdx);

    m_savedList->blockSignals(false);
    m_authCombo->blockSignals(false);
    onAuthMethodChanged(authIdx);
}

SshConnectionConfig SshConfigPanel::gatherConfig() const
{
    SshConnectionConfig config;
    config.name = m_nameEdit->text().trimmed();
    config.host = m_hostEdit->text().trimmed();
    config.port = m_portSpin->value();
    config.username = m_userEdit->text().trimmed();
    config.password = m_passwordEdit->text();
    config.privateKeyPath = m_keyPathEdit->text().trimmed();
    config.passphrase = m_passphraseEdit->text();
    config.connectTimeout = m_timeoutSpin->value();
    config.keepaliveInterval = m_keepaliveSpin->value();
    config.authMethod = static_cast<SshConnectionConfig::AuthMethod>(
        m_authCombo->currentData().toInt()
    );
    return config;
}

SshConnectionConfig SshConfigPanel::currentConfig() const
{
    return gatherConfig();
}

/// @brief 显示底部状态消息
void SshConfigPanel::showStatus(const QString& msg, bool isError)
{
    m_statusLabel->setText(msg);
    const auto& palette = ThemeManager::instance().currentPalette();
    m_statusLabel->setStyleSheet(isError
        ? QStringLiteral("color: %1; font-size:11px; padding:4px 8px;")
              .arg(palette.errorColor.name())
        : QStringLiteral("color: %1; font-size:11px; padding:4px 8px;")
              .arg(palette.accentPrimary.name()));
    m_statusLabel->show();
}

// ============================================================
// 主题适配
// ============================================================

void SshConfigPanel::applyTheme()
{
    const auto& p = ThemeManager::instance().currentPalette();
    bool isLight = p.bgEditor.lightness() > 128;

    QString bg       = isLight ? QStringLiteral("#ffffff") : QStringLiteral("#252526");
    QString fg       = isLight ? p.fgPrimary.name()     : QStringLiteral("#cccccc");
    QString subFg    = isLight ? p.fgSecondary.name()   : QStringLiteral("#888888");
    QString border   = isLight ? p.borderDefault.name()  : QStringLiteral("#3c3c3c");
    QString cardBg   = isLight ? QStringLiteral("#fafafa") : QStringLiteral("#2d2d2d");
    QString accent   = p.accentPrimary.name();
    QString inputBg  = isLight ? QStringLiteral("#ffffff") : QStringLiteral("#1e1e1e");
    QString hoverBg  = isLight ? p.bgHover.name()        : QStringLiteral("#383838");

    setStyleSheet(QStringLiteral(
        "QWidget#sshConfigPanel { background: %1; color: %2; }"
        "QWidget#sshConfigLeft { background: %3; border-right: 1px solid %4; border-radius: 6px; }"
        "QWidget#sshConfigRight { background: transparent; }"
        "QLabel#sshConfigListTitle { color: %5; font-size: 13px; font-weight: bold; margin-bottom:4px; }"
        "QLabel#sshConfigTitle { color: %2; }"
        "QGroupBox#sshConfigCard { background: %6; color: %2; border: 1px solid %4;"
        "  border-radius: 6px; margin-top: 12px; padding-top: 18px; }"
        "QGroupBox#sshConfigCard::title { subcontrol-origin: margin; left: 14px; color: %5; }"
        "QLineEdit { background: %7; color: %2; border: 1px solid %4; border-radius: 4px; "
        "  padding: 6px 10px; selection-background-color: %8; }"
        " QLineEdit:focus { border-color: %9; }"
        "QSpinBox { background: %7; color: %2; border: 1px solid %4; border-radius: 4px; padding: 4px; }"
        "QComboBox { background: %7; color: %2; border: 1px solid %4; border-radius: 4px; padding: 5px 10px; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; "
        "  border-right: 5px solid transparent; border-top: 5px solid %5; margin-right: 6px; }"
        "QListView { background: %7; color: %2; border: 1px solid %4; outline: none; }"
        "QPushButton { color: %2; background: %7; border: 1px solid %4; border-radius: 4px; "
        "  padding: 6px 16px; font-size: 12px; }"
        "QPushButton:hover { background: %10; border-color: %9; }"
        "QPushButton:pressed { background: %9; color: #ffffff; }"
        "QPushButton#sshConnectBtn { background: %9; color: #ffffff; border: none; font-weight: bold; padding: 6px 24px; }"
        "QPushButton#sshConnectBtn:hover { opacity: 0.88; }"
        "QPushButton#sshTestBtn { color: %9; }"
        "QPushButton#sshDeployLspBtn { color: %9; }"
        "QPushButton#sshDeleteBtn { color: #c44; border-color: #553333; }"
        "QPushButton#sshDeleteBtn:hover { background: #442222; color: #f66; }"
        "QListWidget#sshConfigList { background: %7; color: %2; border: 1px solid %4; border-radius: 4px; }"
        "QListWidget#sshConfigList::item { padding: 6px 10px; border-radius: 3px; }"
        "QListWidget#sshConfigList::item:selected { background: %9; color: #ffffff; }"
        "QListWidget#sshConfigList::item:hover:!selected { background: %10; }"
    ).arg(bg, fg, bg, border, subFg, cardBg, inputBg, accent, accent, hoverBg));

    // 列表标题
    if (m_listTitleLabel) {
        m_listTitleLabel->setStyleSheet(QStringLiteral("font-weight:bold; color:%1;").arg(subFg));
    }
}
