#ifndef SQLITEBROWSER_H
#define SQLITEBROWSER_H

#include <QWidget>
#include <QSqlDatabase>
#include <QLabel>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QComboBox>
#include <QSpinBox>

/// @brief SQLite 数据库浏览器组件
///
/// 打开 .db/.sqlite/.sqlite3 文件时显示数据库浏览器，
/// 支持查看表结构、浏览数据、执行自定义 SQL 查询。
///
/// UI 布局：
/// ┌─ SQLite 浏览器 ───────────────────────┐
/// │ ┌── 表列表 ──┐ ┌── 数据内容 ───────┐ │
/// │ │ ▼ users   │ │ id | name | email │ │
/// │ │ ▼ orders  │ │ 1  | Alice| a@b.c │ │
/// │ └───────────┘ └────────────────────┘ │
/// │ ┌── 表结构 (DDL) ──────────────────┐ │
/// │ │ CREATE TABLE users (...);        │ │
/// │ └──────────────────────────────────┘ │
/// │ SQL: [查询框] [▶执行] [🔄刷新]      │
/// │ 状态: 已连接 | 3张表 | users 3行     │
/// └──────────────────────────────────────┘
class SqliteBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit SqliteBrowser(QWidget* parent = nullptr);
    ~SqliteBrowser();

    /// @brief 打开数据库文件
    /// @param dbPath 数据库文件完整路径
    /// @return 是否打开成功
    bool openDatabase(const QString& dbPath);

    /// @brief 关闭当前数据库连接
    void closeDatabase();

    /// @brief 当前是否有已打开的数据库
    bool isOpen() const;

    /// @brief 获取当前打开的数据库文件路径
    QString currentPath() const;

private slots:
    /// @brief 左侧表树选中项变化
    void onTableSelected(QTreeWidgetItem* item, int column);

    /// @brief 刷新按钮点击
    void onRefresh();

    /// @brief 执行自定义 SQL 查询
    void onExecuteQuery();

private:
    /// @brief 加载所有表名到左侧树
    void loadTableList();

    /// @brief 加载指定表的数据到右侧表格
    /// @param tableName 表名
    /// @param limit 行数限制（分页用，默认500）
    void loadTableData(const QString& tableName, int limit = 500);

    /// @brief 显示指定表的 CREATE 语句（DDL）
    void showTableInfo(const QString& tableName);

    /// @brief 更新状态栏文字
    void updateStatus(const QString& message);

    /// @brief 构建UI布局
    void setupUI();

    // === 数据库连接 ===
    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_dbConnectionName;

    // === UI 组件 ===
    QLabel*       m_statusLabel;
    QTreeWidget*  m_tableTree;               // 左侧表/索引列表
    QTableWidget* m_dataTable;              // 右侧数据内容表格
    QTextEdit*    m_schemaView;             // CREATE TABLE DDL 视图
    QLineEdit*    m_queryEdit;              // SQL 查询输入框
    QPushButton*  m_executeBtn;             // 执行按钮
    QPushButton*  m_refreshBtn;             // 刷新按钮
    QComboBox*    m_pageCombo;              // 分页下拉框
    QLabel*       m_pageLabel;              // 分页标签
    QSpinBox*     m_pageSizeSpin;           // 每页行数设置
    QString       m_currentTable;           // 当前选中的表名
    int           m_pageOffset = 0;         // 分页偏移量

    /// @brief 查询表的总行数（用于分页计算）
    int rowCount(const QString& tableName);
};

#endif // SQLITEBROWSER_H
