#include "ui/tools/SqliteBrowser.h"
#include "Logger.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include "ui/dialog/ModernDialog.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlField>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>

// ============================================================
// 构造 / 析构
// ============================================================

SqliteBrowser::SqliteBrowser(QWidget* parent)
    : QWidget(parent)
    , m_db(QSqlDatabase())  // 空数据库（延迟初始化）
{
    setObjectName(QStringLiteral("sqliteBrowser"));
    setupUI();
}

SqliteBrowser::~SqliteBrowser()
{
    closeDatabase();
}

// ============================================================
// UI 布局
// ============================================================

void SqliteBrowser::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);

    // 标题栏：数据库名称 + 刷新按钮
    auto* titleLayout = new QHBoxLayout();
    m_statusLabel = new QLabel(tr("未打开数据库"), this);
    m_statusLabel->setObjectName(QStringLiteral("panelTitle"));
    titleLayout->addWidget(m_statusLabel);

    m_refreshBtn = new QPushButton(tr("刷新"), this);
    m_refreshBtn->setFixedSize(60, 24);
    m_refreshBtn->setObjectName(QStringLiteral("btnResetSection"));
    titleLayout->addWidget(m_refreshBtn);
    titleLayout->addStretch();

    mainLayout->addLayout(titleLayout);

    // 主水平分割器：左侧表树 | 右侧数据+DDL
    auto* hSplitter = new QSplitter(Qt::Horizontal, this);
    hSplitter->setStretchFactor(0, 0);
    hSplitter->setStretchFactor(1, 1);

    // === 左侧：表列表 ===
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    m_tableTree = new QTreeWidget(this);
    m_tableTree->setObjectName(QStringLiteral("sideFileTree"));
    m_tableTree->setHeaderHidden(true);
    m_tableTree->setAnimated(true);
    m_tableTree->setColumnCount(1);
    m_tableTree->setFixedWidth(200);
    leftLayout->addWidget(m_tableTree);

    hSplitter->addWidget(leftWidget);

    // === 右侧：数据表格 + DDL + SQL查询 ===
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(4, 0, 0, 0);
    rightLayout->setSpacing(2);

    // 数据表格
    m_dataTable = new QTableWidget(this);
    m_dataTable->setAlternatingRowColors(true);
    m_dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 只读浏览
    m_dataTable->horizontalHeader()->setStretchLastSection(true);
    m_dataTable->verticalHeader()->setVisible(false);
    rightLayout->addWidget(m_dataTable, 1);

    // 分页导航
    auto* pageLayout = new QHBoxLayout();
    pageLayout->setSpacing(4);

    auto* btnPrev = new QPushButton(tr("上一页"), this);
    btnPrev->setFixedSize(70, 24);
    auto* btnNext = new QPushButton(tr("下一页"), this);
    btnNext->setFixedSize(70, 24);
    m_pageLabel = new QLabel(this);
    m_pageCombo = new QComboBox(this);
    m_pageCombo->setFixedWidth(80);

    pageLayout->addWidget(btnPrev);
    pageLayout->addWidget(btnNext);
    pageLayout->addWidget(m_pageLabel);
    pageLayout->addWidget(m_pageCombo);
    pageLayout->addStretch();

    // 每页行数设置
    pageLayout->addWidget(new QLabel(tr("每页:"), this));
    m_pageSizeSpin = new QSpinBox(this);
    m_pageSizeSpin->setRange(50, 5000);
    m_pageSizeSpin->setValue(500);
    m_pageSizeSpin->setSingleStep(100);
    m_pageSizeSpin->setSuffix(QStringLiteral(" 行"));
    m_pageSizeSpin->setFixedWidth(90);
    pageLayout->addWidget(m_pageSizeSpin);

    rightLayout->addLayout(pageLayout);

    // DDL 视图（CREATE TABLE 语句）
    m_schemaView = new QTextEdit(this);
    m_schemaView->setObjectName(QStringLiteral("settingsHint"));
    m_schemaView->setReadOnly(true);
    m_schemaView->setMaximumHeight(120);
    m_schemaView->setFont(QFont("Consolas", 9));
    rightLayout->addWidget(m_schemaView);

    // SQL 查询输入区
    auto* sqlLayout = new QHBoxLayout();
    sqlLayout->setSpacing(4);
    sqlLayout->addWidget(new QLabel(tr("SQL:"), this));
    m_queryEdit = new QLineEdit(this);
    m_queryEdit->setPlaceholderText(tr("输入自定义 SQL 查询... (如 SELECT * FROM users LIMIT 10)"));
    sqlLayout->addWidget(m_queryEdit, 1);

    m_executeBtn = new QPushButton(tr("执行▶"), this);
    m_executeBtn->setFixedWidth(70);
    m_executeBtn->setObjectName(QStringLiteral("btnResetSection"));
    sqlLayout->addWidget(m_executeBtn);

    rightLayout->addLayout(sqlLayout);

    // 底部状态标签
    auto* statusLayout = new QHBoxLayout();
    auto* bottomStatusLabel = new QLabel(this);
    bottomStatusLabel->setObjectName(QStringLiteral("settingsHint"));
    statusLayout->addWidget(bottomStatusLabel, 1);
    rightLayout->addLayout(statusLayout);

    hSplitter->addWidget(rightWidget);
    hSplitter->setSizes({180, 600});

    mainLayout->addWidget(hSplitter, 1);

    // 信号连接
    connect(m_tableTree, &QTreeWidget::itemDoubleClicked,
            this, &SqliteBrowser::onTableSelected);
    connect(m_refreshBtn, &QPushButton::clicked,
            this, &SqliteBrowser::onRefresh);
    connect(m_executeBtn, &QPushButton::clicked,
            this, &SqliteBrowser::onExecuteQuery);
    connect(m_queryEdit, &QLineEdit::returnPressed,
            this, &SqliteBrowser::onExecuteQuery);
    connect(btnPrev, &QPushButton::clicked, this, [this]() {
        if (m_pageOffset > 0) {
            m_pageOffset -= m_pageSizeSpin->value();
            loadTableData(m_currentTable, m_pageSizeSpin->value());
        }
    });
    connect(btnNext, &QPushButton::clicked, this, [this]() {
        m_pageOffset += m_pageSizeSpin->value();
        loadTableData(m_currentTable, m_pageSizeSpin->value());
    });
}

// ============================================================
// 数据库操作
// ============================================================

bool SqliteBrowser::openDatabase(const QString& dbPath)
{
    closeDatabase();

    // 使用唯一连接名（基于文件路径的哈希）
    m_dbConnectionName = QStringLiteral("sqlite_browser_%1").arg(qHash(dbPath));

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_dbConnectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        ModernDialog::warning(this, tr("打开失败"),
            tr("无法打开数据库文件:\n%1\n\n错误: %2")
                .arg(dbPath).arg(m_db.lastError().text()));
        return false;
    }

    m_dbPath = dbPath;
    m_statusLabel->setText(tr("SQLite: %1").arg(QFileInfo(dbPath).fileName()));

    loadTableList();

    LOG_DEBUG("[SqliteBrowser] 数据库已打开:" << dbPath);
    return true;
}

void SqliteBrowser::closeDatabase()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    if (!m_dbConnectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_dbConnectionName);
        m_dbConnectionName.clear();
    }
    m_dbPath.clear();
    m_currentTable.clear();
    m_tableTree->clear();
    m_dataTable->setRowCount(0);
    m_dataTable->setColumnCount(0);
    m_schemaView->clear();
    m_pageOffset = 0;
    m_statusLabel->setText(tr("未打开数据库"));
}

bool SqliteBrowser::isOpen() const
{
    return m_db.isOpen();
}

QString SqliteBrowser::currentPath() const
{
    return m_dbPath;
}

// ============================================================
// 表加载与数据展示
// ============================================================

void SqliteBrowser::loadTableList()
{
    m_tableTree->clear();

    if (!isOpen()) return;

    // 查询所有表和视图
    QSqlQuery query(m_db);
    query.exec(QStringLiteral(
        "SELECT name, type FROM sqlite_master WHERE type IN ('table', 'view') ORDER BY name"
    ));

    while (query.next()) {
        QString name = query.value(0).toString();
        QString type = query.value(1).toString();

        auto* item = new QTreeWidgetItem(m_tableTree);
        item->setText(0, (type == QStringLiteral("view") ? QStringLiteral("👁 ") : QStringLiteral("📋 ")) + name);
        item->setData(0, Qt::UserRole, name);
        item->setData(0, Qt::UserRole + 1, type);
    }

    updateStatus(tr("已连接 | %1 张表/视图").arg(m_tableTree->topLevelItemCount()));
}

void SqliteBrowser::loadTableData(const QString& tableName, int limit)
{
    m_dataTable->setRowCount(0);
    m_dataTable->setColumnCount(0);

    if (!isOpen() || tableName.isEmpty()) return;

    QElapsedTimer timer;
    timer.start();

    QSqlQuery query(m_db);
    QString sql = QStringLiteral("SELECT * FROM [%1] LIMIT %2 OFFSET %3")
                      .arg(tableName)
                      .arg(limit)
                      .arg(m_pageOffset);

    if (!query.exec(sql)) {
        updateStatus(tr("❌ 查询失败: %1").arg(query.lastError().text()));
        return;
    }

    // 设置列头
    QSqlRecord record = query.record();
    int colCount = record.count();
    m_dataTable->setColumnCount(colCount);
    QStringList headers;
    for (int i = 0; i < colCount; ++i) {
        headers << record.fieldName(i);
    }
    m_dataTable->setHorizontalHeaderLabels(headers);

    // 填充数据
    int row = 0;
    while (query.next()) {
        m_dataTable->insertRow(row);
        for (int i = 0; i < colCount; ++i) {
            QVariant val = query.value(i);
            QString displayText;

            if (val.isNull()) {
                displayText = QStringLiteral("(NULL)");
                m_dataTable->setItem(row, i, new QTableWidgetItem(displayText));
                m_dataTable->item(row, i)->setForeground(QColor(128, 128, 128));
            } else if (val.typeId() == QVariant::ByteArray) {
                // BLOB 类型显示为十六进制
                QByteArray blob = val.toByteArray();
                displayText = QStringLiteral("[BLOB %1 字节]").arg(blob.size());
                m_dataTable->setItem(row, i, new QTableWidgetItem(displayText));
                m_dataTable->item(row, i)->setForeground(QColor(0, 120, 215));
            } else {
                displayText = val.toString();
                // 截断过长文本
                if (displayText.length() > 200) {
                    displayText = displayText.left(200) + QStringLiteral("...");
                }
                m_dataTable->setItem(row, i, new QTableWidgetItem(displayText));
            }
        }
        ++row;
    }

    // 更新分页信息
    int totalRows = rowCount(tableName);
    int currentPage = m_pageOffset / limit + 1;
    int totalPages = (totalRows + limit - 1) / limit;
    m_pageLabel->setText(tr("第 %1/%2 页 | 共 %3 行")
                              .arg(currentPage)
                              .arg(totalPages > 0 ? totalPages : 1)
                              .arg(totalRows));

    // 更新状态
    qint64 elapsed = timer.elapsed();
    updateStatus(tr("✅ 查询成功 | 返回 %1 行 | 耗时 %2ms").arg(row).arg(elapsed));

    // 加载 DDL
    showTableInfo(tableName);
}

void SqliteBrowser::showTableInfo(const QString& tableName)
{
    if (!isOpen() || tableName.isEmpty()) {
        m_schemaView->clear();
        return;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT sql FROM sqlite_master WHERE name = ?"));
    query.addBindValue(tableName);

    if (query.exec() && query.next()) {
        QString ddl = query.value(0).toString();
        m_schemaView->setPlainText(ddl);
    } else {
        m_schemaView->setPlainText(tr("-- 无法获取表结构"));
    }
}

int SqliteBrowser::rowCount(const QString& tableName)
{
    if (!isOpen() || tableName.isEmpty()) return 0;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM [%1]").arg(tableName));
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

void SqliteBrowser::updateStatus(const QString& message)
{
    m_statusLabel->setToolTip(message);
    // 可以在底部显示更详细的状态信息
}

// ============================================================
// 槽函数
// ============================================================

void SqliteBrowser::onTableSelected(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;

    QString tableName = item->data(0, Qt::UserRole).toString();
    if (tableName.isEmpty()) return;

    m_currentTable = tableName;
    m_pageOffset = 0;
    loadTableData(tableName, m_pageSizeSpin->value());
}

void SqliteBrowser::onRefresh()
{
    loadTableList();
    if (!m_currentTable.isEmpty()) {
        loadTableData(m_currentTable, m_pageSizeSpin->value());
    }
}

void SqliteBrowser::onExecuteQuery()
{
    QString sql = m_queryEdit->text().trimmed();
    if (sql.isEmpty()) return;

    if (!isOpen()) {
        ModernDialog::warning(this, tr("错误"), tr("请先打开数据库文件"));
        return;
    }

    QElapsedTimer timer;
    timer.start();

    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        updateStatus(tr("❌ SQL 执行失败: %1").arg(query.lastError().text()));
        m_schemaView->setPlainText(tr("错误: %1").arg(query.lastError().text()));
        return;
    }

    // 判断是否为 SELECT 查询（有结果集）
    if (query.isSelect()) {
        m_dataTable->setRowCount(0);
        m_dataTable->setColumnCount(0);

        QSqlRecord record = query.record();
        int colCount = record.count();
        m_dataTable->setColumnCount(colCount);

        QStringList headers;
        for (int i = 0; i < colCount; ++i) {
            headers << record.fieldName(i);
        }
        m_dataTable->setHorizontalHeaderLabels(headers);

        int row = 0;
        while (query.next()) {
            m_dataTable->insertRow(row);
            for (int i = 0; i < colCount; ++i) {
                QVariant val = query.value(i);
                QString displayText = val.isNull() ? QStringLiteral("(NULL)") : val.toString();
                if (displayText.length() > 200) {
                    displayText = displayText.left(200) + QStringLiteral("...");
                }
                m_dataTable->setItem(row, i, new QTableWidgetItem(displayText));
            }
            ++row;
        }

        qint64 elapsed = timer.elapsed();
        updateStatus(tr("✅ 自定义 SQL 执行成功 | 返回 %1 行 | 耗时 %2ms").arg(row).arg(elapsed));
        m_schemaView->setPlainText(sql);
    } else {
        // INSERT / UPDATE / DELETE 等
        int affectedRows = query.numRowsAffected();
        qint64 elapsed = timer.elapsed();
        updateStatus(tr("✅ SQL 执行成功 | 影响行数: %1 | 耗时 %2ms").arg(affectedRows).arg(elapsed));
        m_schemaView->setPlainText(tr("%1;\n-- 影响行数: %2").arg(sql).arg(affectedRows));

        // 如果修改了当前表，刷新数据
        if (!m_currentTable.isEmpty()) {
            loadTableData(m_currentTable, m_pageSizeSpin->value());
        }
    }
}
