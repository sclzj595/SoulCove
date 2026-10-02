#include "ui/snippet/SnippetManagerDialog.h"
#include "core/snippet/SnippetManager.h"
#include "ui/dialog/ModernDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

SnippetManagerDialog::SnippetManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("代码片段管理"));
    setMinimumSize(820, 520);
    setupUI();
    refreshList();
}

void SnippetManagerDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // === 中央：左列表 + 右详情（用 QSplitter 可调）===
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // 左侧：片段列表
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto* listTitle = new QLabel(tr("片段列表"), leftWidget);
    listTitle->setObjectName(QStringLiteral("settingsSectionTitle"));
    leftLayout->addWidget(listTitle);

    m_listWidget = new QListWidget(leftWidget);
    m_listWidget->setObjectName(QStringLiteral("sideFileList"));
    leftLayout->addWidget(m_listWidget);

    m_countLabel = new QLabel(leftWidget);
    m_countLabel->setObjectName(QStringLiteral("settingsHint"));
    leftLayout->addWidget(m_countLabel);

    splitter->addWidget(leftWidget);

    // 右侧：详情编辑区
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    auto* detailTitle = new QLabel(tr("详情编辑"), rightWidget);
    detailTitle->setObjectName(QStringLiteral("settingsSectionTitle"));
    rightLayout->addWidget(detailTitle);

    auto* formWidget = new QWidget(rightWidget);
    auto* form = new QFormLayout(formWidget);
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(6);

    m_nameEdit = new QLineEdit(formWidget);
    m_nameEdit->setPlaceholderText(tr("显示名称"));
    form->addRow(tr("名称:"), m_nameEdit);

    m_prefixEdit = new QLineEdit(formWidget);
    m_prefixEdit->setPlaceholderText(tr("触发前缀（输入后通过命令面板插入）"));
    form->addRow(tr("前缀:"), m_prefixEdit);

    m_languageCombo = new QComboBox(formWidget);
    m_languageCombo->addItem(tr("通用"),     QStringLiteral("all"));
    m_languageCombo->addItem(QStringLiteral("C++"),         QStringLiteral("cpp"));
    m_languageCombo->addItem(QStringLiteral("Python"),      QStringLiteral("python"));
    m_languageCombo->addItem(QStringLiteral("JavaScript"),  QStringLiteral("javascript"));
    m_languageCombo->addItem(QStringLiteral("HTML"),        QStringLiteral("html"));
    m_languageCombo->addItem(QStringLiteral("CSS"),         QStringLiteral("css"));
    m_languageCombo->addItem(QStringLiteral("Markdown"),    QStringLiteral("markdown"));
    form->addRow(tr("语言:"), m_languageCombo);

    m_descEdit = new QLineEdit(formWidget);
    m_descEdit->setPlaceholderText(tr("简短描述"));
    form->addRow(tr("描述:"), m_descEdit);

    m_bodyEdit = new QTextEdit(formWidget);
    m_bodyEdit->setPlaceholderText(tr("片段内容，支持 $1 $2 ${1:default} $0 $SELECTION 占位符"));
    m_bodyEdit->setMinimumHeight(180);
    form->addRow(tr("内容:"), m_bodyEdit);

    rightLayout->addWidget(formWidget);

    m_hintLabel = new QLabel(tr("占位符: $1 $2 为跳转位置, $0 为最终光标, $SELECTION 为选中文本"), rightWidget);
    m_hintLabel->setObjectName(QStringLiteral("settingsHint"));
    m_hintLabel->setWordWrap(true);
    rightLayout->addWidget(m_hintLabel);

    rightLayout->addStretch();

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter, 1);

    // === 底部按钮区 ===
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);

    m_btnAdd           = new QPushButton(tr("新增"), this);
    m_btnDelete        = new QPushButton(tr("删除"), this);
    m_btnApply         = new QPushButton(tr("应用编辑"), this);
    m_btnImport        = new QPushButton(tr("导入"), this);
    m_btnExport        = new QPushButton(tr("导出"), this);
    m_btnImportVscode  = new QPushButton(tr("导入 VSCode"), this);
    m_btnExportVscode  = new QPushButton(tr("导出 VSCode"), this);
    m_btnClose         = new QPushButton(tr("关闭"), this);

    m_btnApply->setDefault(true);

    btnLayout->addWidget(m_btnAdd);
    btnLayout->addWidget(m_btnDelete);
    btnLayout->addWidget(m_btnApply);
    btnLayout->addSpacing(12);
    btnLayout->addWidget(m_btnImport);
    btnLayout->addWidget(m_btnExport);
    btnLayout->addWidget(m_btnImportVscode);
    btnLayout->addWidget(m_btnExportVscode);
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnClose);

    mainLayout->addLayout(btnLayout);

    // === 信号连接 ===
    connect(m_listWidget, &QListWidget::currentRowChanged,
            this, &SnippetManagerDialog::onSelectionChanged);
    connect(m_btnAdd, &QPushButton::clicked, this, &SnippetManagerDialog::onAddSnippet);
    connect(m_btnDelete, &QPushButton::clicked, this, &SnippetManagerDialog::onDeleteSnippet);
    connect(m_btnApply, &QPushButton::clicked, this, &SnippetManagerDialog::onApplyEdit);
    connect(m_btnImport, &QPushButton::clicked, this, &SnippetManagerDialog::onImportJson);
    connect(m_btnExport, &QPushButton::clicked, this, &SnippetManagerDialog::onExportJson);
    connect(m_btnImportVscode, &QPushButton::clicked, this, &SnippetManagerDialog::onImportVscode);
    connect(m_btnExportVscode, &QPushButton::clicked, this, &SnippetManagerDialog::onExportVscode);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);
}

void SnippetManagerDialog::refreshList()
{
    m_listWidget->blockSignals(true);
    m_listWidget->clear();

    auto& sm = SnippetManager::instance();
    QList<CodeSnippet> snippets = sm.allSnippets();
    for (const CodeSnippet& s : snippets) {
        // 显示: 名称 [语言/前缀] 标签
        QString text = QStringLiteral("%1  [%2 | %3]").arg(s.name, s.language, s.prefix);
        auto* item = new QListWidgetItem(text, m_listWidget);
        item->setData(Qt::UserRole, s.id);   // 携带 ID 便于定位
        m_listWidget->addItem(item);
    }

    m_countLabel->setText(tr("共 %1 个片段").arg(snippets.size()));
    m_listWidget->blockSignals(false);

    if (m_listWidget->count() > 0)
        m_listWidget->setCurrentRow(0);
    else
        clearDetail();
}

void SnippetManagerDialog::loadDetail(const CodeSnippet& s)
{
    m_currentId = s.id;
    m_nameEdit->setText(s.name);
    m_prefixEdit->setText(s.prefix);
    m_descEdit->setText(s.description);
    m_bodyEdit->setPlainText(s.body);

    int idx = m_languageCombo->findData(s.language);
    m_languageCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void SnippetManagerDialog::clearDetail()
{
    m_currentId.clear();
    m_nameEdit->clear();
    m_prefixEdit->clear();
    m_descEdit->clear();
    m_bodyEdit->clear();
    m_languageCombo->setCurrentIndex(0);
}

bool SnippetManagerDialog::applyCurrentEdit()
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        ModernDialog::warning(this, tr("代码片段"), tr("名称不能为空"));
        return false;
    }

    CodeSnippet s;
    s.id          = m_currentId;
    s.name        = name;
    s.prefix      = m_prefixEdit->text().trimmed();
    s.description = m_descEdit->text();
    s.language    = m_languageCombo->currentData().toString();
    s.body        = m_bodyEdit->toPlainText();

    auto& sm = SnippetManager::instance();
    if (s.id.isEmpty()) {
        // 新建
        sm.addSnippet(s);
    } else if (sm.snippet(s.id).id.isEmpty()) {
        // ID 不存在，按新增处理
        s.id.clear();
        sm.addSnippet(s);
    } else {
        sm.updateSnippet(s);
    }

    // 注：m_currentId 由调用方的 refreshList() → onSelectionChanged 重新定位
    return true;
}

void SnippetManagerDialog::notifyChanged()
{
    emit snippetChanged();
}

void SnippetManagerDialog::onSelectionChanged()
{
    int row = m_listWidget->currentRow();
    if (row < 0) {
        clearDetail();
        return;
    }

    QString id = m_listWidget->item(row)->data(Qt::UserRole).toString();
    CodeSnippet s = SnippetManager::instance().snippet(id);
    if (!s.id.isEmpty())
        loadDetail(s);
    else
        clearDetail();
}

void SnippetManagerDialog::onAddSnippet()
{
    // 清空右侧编辑区进入新建模式，用户填写后点「应用编辑」保存
    clearDetail();
    m_nameEdit->setFocus();
}

void SnippetManagerDialog::onDeleteSnippet()
{
    int row = m_listWidget->currentRow();
    if (row < 0) {
        ModernDialog::information(this, tr("代码片段"), tr("请先选择要删除的片段"));
        return;
    }

    QString id = m_listWidget->item(row)->data(Qt::UserRole).toString();
    QString name = m_listWidget->item(row)->text();

    if (ModernDialog::question(this, tr("删除片段"), tr("确认删除 \"%1\" ?").arg(name))
        == ModernDialog::ROLE_ACCEPT) {
        SnippetManager::instance().removeSnippet(id);
        refreshList();
        notifyChanged();
    }
}

void SnippetManagerDialog::onApplyEdit()
{
    if (applyCurrentEdit()) {
        refreshList();
        notifyChanged();
    }
}

void SnippetManagerDialog::onImportJson()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("导入代码片段（内部 JSON 格式）"), QString(),
        tr("JSON 文件 (*.json)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ModernDialog::warning(this, tr("导入失败"), tr("无法读取文件: %1").arg(file.errorString()));
        return;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        ModernDialog::warning(this, tr("导入失败"), tr("JSON 解析失败: %1").arg(err.errorString()));
        return;
    }

    auto& sm = SnippetManager::instance();
    int count = 0;
    for (const QJsonValue& v : doc.array()) {
        QJsonObject obj = v.toObject();
        CodeSnippet s;
        s.id.clear();   // 重新生成 ID，避免覆盖现有
        s.name         = obj.value(QStringLiteral("name")).toString();
        s.description  = obj.value(QStringLiteral("description")).toString();
        s.language     = obj.value(QStringLiteral("language")).toString(QStringLiteral("all"));
        s.prefix       = obj.value(QStringLiteral("prefix")).toString();
        s.body         = obj.value(QStringLiteral("body")).toString();
        s.shortcut     = obj.value(QStringLiteral("shortcut")).toString();
        if (!s.name.isEmpty()) {
            sm.addSnippet(s);
            ++count;
        }
    }

    refreshList();
    notifyChanged();
    ModernDialog::information(this, tr("导入完成"), tr("成功导入 %1 个片段").arg(count));
}

void SnippetManagerDialog::onExportJson()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("导出代码片段（内部 JSON 格式）"), QStringLiteral("snippets.json"),
        tr("JSON 文件 (*.json)"));
    if (path.isEmpty()) return;

    QJsonArray arr;
    for (const CodeSnippet& s : SnippetManager::instance().allSnippets()) {
        QJsonObject obj;
        obj[QStringLiteral("id")]           = s.id;
        obj[QStringLiteral("name")]         = s.name;
        obj[QStringLiteral("description")]  = s.description;
        obj[QStringLiteral("language")]     = s.language;
        obj[QStringLiteral("prefix")]       = s.prefix;
        obj[QStringLiteral("body")]         = s.body;
        obj[QStringLiteral("shortcut")]     = s.shortcut;
        obj[QStringLiteral("createdTime")]  = s.createdTime.toString(Qt::ISODate);
        obj[QStringLiteral("modifiedTime")] = s.modifiedTime.toString(Qt::ISODate);
        arr.append(obj);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ModernDialog::warning(this, tr("导出失败"), tr("无法写入文件: %1").arg(file.errorString()));
        return;
    }
    QJsonDocument doc(arr);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    ModernDialog::information(this, tr("导出完成"), tr("已导出 %1 个片段到\n%2").arg(arr.size()).arg(path));
}

void SnippetManagerDialog::onImportVscode()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("导入 VSCode 代码片段"), QString(),
        tr("JSON 文件 (*.json)"));
    if (path.isEmpty()) return;

    // 选择目标语言（默认 all）
    bool ok = false;
    QStringList langs;
    langs << tr("all (通用)") << QStringLiteral("cpp") << QStringLiteral("python")
          << QStringLiteral("javascript") << QStringLiteral("html")
          << QStringLiteral("css") << QStringLiteral("markdown");
    QString picked = ModernDialog::getItem(this, tr("选择导入语言"),
        tr("将这些片段标记为哪种语言?"), langs, 0, &ok);
    if (!ok || picked.isEmpty()) return;

    QString lang;
    if (picked.contains(QStringLiteral("(")))
        lang = QStringLiteral("all");
    else
        lang = picked.trimmed();

    if (SnippetManager::instance().importFromVscodeJson(path, lang)) {
        refreshList();
        notifyChanged();
        ModernDialog::information(this, tr("导入完成"),
            tr("已从 VSCode 格式导入片段 (语言: %1)").arg(lang));
    } else {
        ModernDialog::warning(this, tr("导入失败"), tr("未能从该文件导入片段，请检查格式"));
    }
}

void SnippetManagerDialog::onExportVscode()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("导出 VSCode 代码片段"), QStringLiteral("snippets-vscode.json"),
        tr("JSON 文件 (*.json)"));
    if (path.isEmpty()) return;

    // 可选按语言导出（默认导出全部）
    bool ok = false;
    QStringList langs;
    langs << tr("all (全部)") << QStringLiteral("cpp") << QStringLiteral("python")
          << QStringLiteral("javascript") << QStringLiteral("html")
          << QStringLiteral("css") << QStringLiteral("markdown");
    QString picked = ModernDialog::getItem(this, tr("选择导出范围"),
        tr("导出哪种语言的片段?"), langs, 0, &ok);
    if (!ok || picked.isEmpty()) return;

    // 选「全部」时传空字符串，由导出函数导出所有片段
    QString lang = (picked.contains(QStringLiteral("("))) ? QString() : picked.trimmed();

    if (SnippetManager::instance().exportToVscodeJson(path, lang)) {
        ModernDialog::information(this, tr("导出完成"), tr("已导出 VSCode 格式片段到\n%1").arg(path));
    } else {
        ModernDialog::warning(this, tr("导出失败"), tr("未能导出片段，可能没有符合条件的片段"));
    }
}
