#include "ui/sidebar/ExplorerPanel.h"
#include "Logger.hpp"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QDropEvent>
#include <QEvent>
#include <QColor>
#include <QFont>

ExplorerPanel::ExplorerPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* explorerLayout = new QVBoxLayout(this);
    explorerLayout->setContentsMargins(4, 6, 2, 2);
    explorerLayout->setSpacing(2);

    // 面板标题行
    m_panelTitle = new QLabel(tr("资源管理器"), this);
    m_panelTitle->setObjectName(QStringLiteral("panelTitle"));
    explorerLayout->addWidget(m_panelTitle);

    // 工具栏行（新建/刷新/折叠 + 路径显示）
    m_explorerHeader = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(m_explorerHeader);
    headerLayout->setContentsMargins(0, 1, 4, 1);
    headerLayout->setSpacing(2);

    m_btnNewFile = new QPushButton(QStringLiteral("+"), m_explorerHeader);
    m_btnNewFile->setToolTip(tr("新建文件"));
    m_btnNewFile->setFixedSize(22, 20);
    m_btnNewFile->setProperty("iconButton", true);
    m_btnNewFile->setFont(QFont("Segoe UI", 11, QFont::Bold));

    m_btnOpenFolder = new QPushButton(QString::fromUtf8("\xF0\x9F\x81\x81"), m_explorerHeader);  // 📁
    m_btnOpenFolder->setToolTip(tr("打开文件夹"));
    m_btnOpenFolder->setFixedSize(22, 20);
    m_btnOpenFolder->setProperty("iconButton", true);
    m_btnOpenFolder->setFont(QFont("Segoe UI", 10, QFont::Bold));

    m_btnRefresh = new QPushButton(QStringLiteral("\u21BB"), m_explorerHeader);
    m_btnRefresh->setToolTip(tr("刷新文件列表"));
    m_btnRefresh->setFixedSize(22, 20);
    m_btnRefresh->setProperty("iconButton", true);

    m_btnCollapseAll = new QPushButton(QStringLiteral("\u2261"), m_explorerHeader);
    m_btnCollapseAll->setToolTip(tr("折叠全部"));
    m_btnCollapseAll->setFixedSize(22, 20);
    m_btnCollapseAll->setProperty("iconButton", true);

    m_pathLabel = new QLabel(m_explorerHeader);
    m_pathLabel->setObjectName(QStringLiteral("pathLabel"));
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathLabel->setWordWrap(false);

    headerLayout->addWidget(m_btnNewFile);
    headerLayout->addWidget(m_btnOpenFolder);
    headerLayout->addWidget(m_btnRefresh);
    headerLayout->addWidget(m_btnCollapseAll);
    headerLayout->addWidget(m_pathLabel, 1); // stretch

    explorerLayout->addWidget(m_explorerHeader);

    m_fileTree = new QTreeWidget(this);
    m_fileTree->setObjectName(QStringLiteral("sideFileTree"));
    m_fileTree->setHeaderHidden(true);
    m_fileTree->setAnimated(true);
    m_fileTree->setIndentation(16);
    m_fileTree->setRootIsDecorated(true);
    m_fileTree->setSortingEnabled(false);
    // V1.9: 启用文件树内部拖拽（用于移动文件）
    m_fileTree->setDragEnabled(true);
    m_fileTree->setAcceptDrops(true);
    m_fileTree->setDropIndicatorShown(true);
    m_fileTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_fileTree->viewport()->installEventFilter(this);  // 拦截 viewport 的 drop 事件
    // 强制使用支持 emoji 的字体，确保文件图标正确渲染
    {
        QFont emojiFont = m_fileTree->font();
        emojiFont.setFamilies({QStringLiteral("Segoe UI Emoji"),
                               QStringLiteral("Apple Color Emoji"),
                               QStringLiteral("Noto Color Emoji")});
        m_fileTree->setFont(emojiFont);
    }
    explorerLayout->addWidget(m_fileTree);

    // === 信号连接 ===
    connect(m_fileTree, &QTreeWidget::itemDoubleClicked,
            this, &ExplorerPanel::onItemDoubleClicked);
    connect(m_fileTree, &QTreeWidget::itemClicked,
            this, &ExplorerPanel::onItemClicked);

    // 右键上下文菜单
    m_fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileTree, &QTreeWidget::customContextMenuRequested,
            this, &ExplorerPanel::onContextMenu);

    // 工具栏按钮
    connect(m_btnNewFile, &QPushButton::clicked,
            this, &ExplorerPanel::onNewFile);
    connect(m_btnOpenFolder, &QPushButton::clicked,
            this, &ExplorerPanel::onOpenFolderClicked);
    connect(m_btnRefresh, &QPushButton::clicked,
            this, &ExplorerPanel::onRefresh);
    connect(m_btnCollapseAll, &QPushButton::clicked,
            this, &ExplorerPanel::onCollapseAll);
}

void ExplorerPanel::setWorkspaceFolders(const QStringList& folders)
{
    m_workspaceFolders = folders;
    refreshFileList();
}

// ============================================================
// 文件树逻辑
// ============================================================

QString ExplorerPanel::fileIcon(const QString& suffix) const
{
    // 单字符 emoji 前缀，配合 QSS 中 font-family: "Segoe UI Emoji" 确保正确渲染
    if (suffix == QStringLiteral("py"))   return QString::fromUtf8("\xF0\x9F\x90\x8D"); // 🐍
    if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("cc") || suffix == QStringLiteral("cxx"))
        return QString::fromUtf8("\xF0\x9F\x94\xA7");                             // 🔧 C++
    if (suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp"))
        return QString::fromUtf8("\xF0\x9F\x93\x8B");                             // 📋 头文件
    if (suffix == QStringLiteral("md"))   return QString::fromUtf8("\xF0\x9F\x93\x96");// 📖
    if (suffix == QStringLiteral("json")) return QString::fromUtf8("\xF0\x9F\x93\xA6");// 📦 JSON
    if (suffix == QStringLiteral("txt"))  return QString::fromUtf8("\xF0\x9F\x93\x84");// 📄
    if (suffix == QStringLiteral("js"))   return QString::fromUtf8("\xF0\x9F\x94\xA5");// 🔥 JS
    if (suffix == QStringLiteral("ts"))   return QString::fromUtf8("\xF0\x9F\x92\x99");// 💙 TS
    if (suffix == QStringLiteral("html")) return QString::fromUtf8("\xF0\x9F\x8C\x90");// 🌐 HTML
    if (suffix == QStringLiteral("css"))  return QString::fromUtf8("\xF0\x9F\x8E\xA8");// 🎨 CSS
    if (suffix == QStringLiteral("qss"))  return QString::fromUtf8("\xE2\x9C\xA8");// ✨ QSS
    if (suffix == QStringLiteral("cmake"))return QString::fromUtf8("\xF0\x9F\x94\xA8");// 🚀 CMake
    if (suffix == QStringLiteral("pro"))  return QString::fromUtf8("\xF0\x9F\x93\xBB");// 📻 Qt
    if (suffix == QStringLiteral("go"))   return QString::fromUtf8("\xF0\x9F\x90\x98");// 🐘 Go
    if (suffix == QStringLiteral("java")) return QString::fromUtf8("\xE2\x98\x95"); // ☕ Java
    if (suffix == QStringLiteral("rs"))   return QString::fromUtf8("\xE2\x99\xBB"); // ♻ Rust
    if (suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml"))
        return QString::fromUtf8("\xF0\x9F\x93\x9D");                            // 📝 YAML
    if (suffix == QStringLiteral("ini"))  return QString::fromUtf8("\xE2\x9A\x99");// ⚙ INI
    return QString();
}

void ExplorerPanel::refreshFileList()
{
    m_fileTree->clear();

    // VSCode风格：没有打开文件夹时显示提示，不自动加载默认目录
    if (m_workspaceFolders.isEmpty()) {
        m_pathLabel->setText(tr("（未打开文件夹）"));
        m_pathLabel->setToolTip(QString());

        // 显示提示项
        auto* hintItem = new QTreeWidgetItem(m_fileTree);
        hintItem->setText(0, QString::fromUtf8("  \xF0\x9F\x81\x81  ") + tr("点击上方按钮打开文件夹"));
        hintItem->setData(0, Qt::UserRole, QString());
        hintItem->setData(0, Qt::UserRole + 1, QStringLiteral("hint"));
        hintItem->setFlags(hintItem->flags() & ~Qt::ItemIsSelectable);
        hintItem->setForeground(0, QColor(128, 128, 128));
        return;
    }

    // V1.9: 多文件夹工作区 — 每个文件夹作为根节点
    for (int i = 0; i < m_workspaceFolders.size(); ++i) {
        const QString& folderPath = m_workspaceFolders[i];
        QDir dir(folderPath);
        if (!dir.exists()) {
            LOG_DEBUG("[ExplorerPanel] 工作区文件夹不存在:" << folderPath);
            continue;
        }

        // 单文件夹时不显示根节点（直接展开内容），多文件夹时显示根节点
        if (m_workspaceFolders.size() == 1) {
            // 单文件夹模式：直接填充（兼容旧行为）
            populateFileTree(nullptr, dir);
        } else {
            // 多文件夹模式：每个文件夹作为根节点
            QTreeWidgetItem* rootItem = new QTreeWidgetItem(m_fileTree);
            rootItem->setText(0, QString::fromUtf8("\xF0\x9F\x81\x81 ") + dir.dirName());
            rootItem->setData(0, Qt::UserRole, folderPath);
            rootItem->setData(0, Qt::UserRole + 1, QStringLiteral("workspaceRoot"));
            rootItem->setData(0, Qt::UserRole + 2, i);  // 工作区索引
            rootItem->setToolTip(0, folderPath);
            rootItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            populateFileTree(rootItem, dir);
            rootItem->setExpanded(true);
        }
    }

    m_fileTree->expandAll();

    // 更新路径显示
    if (m_workspaceFolders.size() == 1) {
        QString displayPath = QDir(m_workspaceFolders.first()).absolutePath();
        if (displayPath.length() > 40) {
            displayPath = QStringLiteral("...") + displayPath.right(37);
        }
        m_pathLabel->setText(displayPath);
        m_pathLabel->setToolTip(QDir(m_workspaceFolders.first()).absolutePath());
    } else {
        m_pathLabel->setText(tr("工作区 (%1 个文件夹)").arg(m_workspaceFolders.size()));
        m_pathLabel->setToolTip(m_workspaceFolders.join(QStringLiteral("\n")));
    }

    LOG_DEBUG("[ExplorerPanel] 文件树加载完成: " << m_workspaceFolders.size() << " 个文件夹");
}

void ExplorerPanel::populateFileTree(QTreeWidgetItem* parentItem, const QDir& dir)
{
    // 先添加子目录
    QFileInfoList dirEntries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : dirEntries) {
        QTreeWidgetItem* dirItem = parentItem
            ? new QTreeWidgetItem(parentItem)
            : new QTreeWidgetItem(m_fileTree);
        dirItem->setText(0, QStringLiteral("\u25B6 ") + fi.fileName());
        dirItem->setData(0, Qt::UserRole, fi.absoluteFilePath());
        dirItem->setData(0, Qt::UserRole + 1, QStringLiteral("dir"));
        dirItem->setToolTip(0, fi.absoluteFilePath());
        dirItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);

        // 递归填充子目录
        populateFileTree(dirItem, QDir(fi.absoluteFilePath()));
    }

    // 再添加文件
    QFileInfoList fileEntries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : fileEntries) {
        QTreeWidgetItem* fileItem = parentItem
            ? new QTreeWidgetItem(parentItem)
            : new QTreeWidgetItem(m_fileTree);
        QString suffix = fi.suffix().toLower();
        fileItem->setText(0, fileIcon(suffix) + fi.fileName());
        fileItem->setData(0, Qt::UserRole, fi.absoluteFilePath());
        fileItem->setData(0, Qt::UserRole + 1, QStringLiteral("file"));
        fileItem->setToolTip(0, fi.absoluteFilePath());
    }
}

QTreeWidgetItem* ExplorerPanel::findTreeItemByPath(QTreeWidgetItem* parent, const QString& filePath) const
{
    int count = parent ? parent->childCount() : m_fileTree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem* item = parent ? parent->child(i) : m_fileTree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == filePath) {
            return item;
        }
        if (item->childCount() > 0) {
            QTreeWidgetItem* found = findTreeItemByPath(item, filePath);
            if (found) return found;
        }
    }
    return nullptr;
}

void ExplorerPanel::selectFileByPath(const QString& filePath)
{
    QTreeWidgetItem* item = findTreeItemByPath(nullptr, filePath);
    if (item) {
        m_fileTree->setCurrentItem(item);
        m_fileTree->scrollToItem(item);
        // 展开父节点
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            parent->setExpanded(true);
            parent = parent->parent();
        }
    } else {
        m_fileTree->clearSelection();
    }
}

// ============================================================
// 槽函数
// ============================================================

void ExplorerPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;
    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type == QStringLiteral("dir") || type == QStringLiteral("workspaceRoot")) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        emit fileOpenRequested(filePath);
    }
}

void ExplorerPanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)
    if (!item) return;
    // 单击文件夹时切换展开/折叠状态
    QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type == QStringLiteral("dir") || type == QStringLiteral("workspaceRoot")) {
        item->setExpanded(!item->isExpanded());
    }
}

void ExplorerPanel::onNewFile()
{
    emit fileCreateRequested();
}

void ExplorerPanel::onRefresh()
{
    refreshFileList();
}

void ExplorerPanel::onCollapseAll()
{
    m_fileTree->collapseAll();
}

void ExplorerPanel::onOpenFolderClicked()
{
    // 仅发射信号，QFileDialog 由 SideBar 处理（工作区状态归 SideBar 管理）
    emit openFolderClicked();
}

void ExplorerPanel::onContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_fileTree->itemAt(pos);
    QMenu menu(this);

    if (item) {
        QString type = item->data(0, Qt::UserRole + 1).toString();
        QString filePath = item->data(0, Qt::UserRole).toString();

        // 文件/文件夹通用操作
        QAction* actCopyPath = menu.addAction(tr("复制路径"));
        connect(actCopyPath, &QAction::triggered, this, [filePath]() {
            QApplication::clipboard()->setText(filePath);
        });

        if (type == QStringLiteral("file")) {
            // 文件特有操作
            QAction* actOpen = menu.addAction(tr("在编辑器中打开"));
            connect(actOpen, &QAction::triggered, this, [this, filePath]() {
                emit fileOpenRequested(filePath);
            });

            menu.addSeparator();

            QAction* actRename = menu.addAction(tr("重命名"));
            connect(actRename, &QAction::triggered, this, [this, filePath]() {
                emit fileRenameRequested(filePath);
            });

            QAction* actDelete = menu.addAction(tr("删除"));
            connect(actDelete, &QAction::triggered, this, [this, filePath]() {
                emit fileDeleteRequested(filePath);
            });
        } else if (type == QStringLiteral("dir")) {
            // 文件夹特有操作
            QAction* actOpenFolder = menu.addAction(tr("在文件管理器中打开"));
            connect(actOpenFolder, &QAction::triggered, this, [this, filePath]() {
                emit openInFolderRequested(filePath);
            });
        } else if (type == QStringLiteral("workspaceRoot")) {
            // V1.9: 工作区根节点特有操作
            int wsIndex = item->data(0, Qt::UserRole + 2).toInt();

            QAction* actOpenFolder = menu.addAction(tr("在文件管理器中打开"));
            connect(actOpenFolder, &QAction::triggered, this, [this, filePath]() {
                emit openInFolderRequested(filePath);
            });

            menu.addSeparator();

            QAction* actRemove = menu.addAction(tr("从工作区移除"));
            connect(actRemove, &QAction::triggered, this, [this, wsIndex]() {
                emit removeWorkspaceFolderRequested(wsIndex);
            });
        }

        menu.addSeparator();
    }

    // 始终可用的操作
    QAction* actNew = menu.addAction(tr("新建文件..."));
    connect(actNew, &QAction::triggered, this, &ExplorerPanel::fileCreateRequested);

    // V1.9: 新建文件夹
    QAction* actNewFolder = menu.addAction(tr("新建文件夹..."));
    connect(actNewFolder, &QAction::triggered, this, &ExplorerPanel::folderCreateRequested);

    menu.addSeparator();

    // V1.9: 添加文件夹到工作区
    QAction* actAddFolder = menu.addAction(tr("添加文件夹到工作区..."));
    connect(actAddFolder, &QAction::triggered, this, [this]() {
        emit addFolderToWorkspaceRequested();
    });

    QAction* actRefresh = menu.addAction(tr("刷新文件列表"));
    connect(actRefresh, &QAction::triggered, this, &ExplorerPanel::refreshFileList);

    menu.exec(m_fileTree->mapToGlobal(pos));
}

// ============================================================
// V1.9: 文件树拖拽移动文件
// ============================================================

bool ExplorerPanel::eventFilter(QObject* obj, QEvent* event)
{
    // 拦截文件树 viewport 的 Drop 事件，自定义移动逻辑
    if (obj == m_fileTree->viewport() && event->type() == QEvent::Drop) {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        if (handleTreeDropEvent(dropEvent)) {
            return true;  // 事件已处理，阻止 QTreeWidget 默认行为（默认会移动 item）
        }
    }
    return QWidget::eventFilter(obj, event);
}

bool ExplorerPanel::handleTreeDropEvent(QDropEvent* event)
{
    // 获取拖拽源（被拖拽的 item）
    QTreeWidgetItem* sourceItem = m_fileTree->currentItem();
    if (!sourceItem) return false;

    QString sourcePath = sourceItem->data(0, Qt::UserRole).toString();
    QString sourceType = sourceItem->data(0, Qt::UserRole + 1).toString();
    if (sourcePath.isEmpty() || sourceType != QStringLiteral("file")) {
        // 仅支持文件拖拽移动（文件夹移动复杂，暂不支持）
        event->ignore();
        return false;
    }

    // 获取放置目标
    QTreeWidgetItem* targetItem = m_fileTree->itemAt(event->position().toPoint());
    QString targetDir;

    if (targetItem) {
        QString targetType = targetItem->data(0, Qt::UserRole + 1).toString();
        if (targetType == QStringLiteral("dir")) {
            targetDir = targetItem->data(0, Qt::UserRole).toString();
        } else if (targetType == QStringLiteral("file")) {
            // 拖到文件上 → 使用其所在目录
            QString targetPath = targetItem->data(0, Qt::UserRole).toString();
            targetDir = QFileInfo(targetPath).absolutePath();
        }
    } else {
        // 拖到空白处 → 工作目录根（使用第一个工作区文件夹）
        if (!m_workspaceFolders.isEmpty()) {
            targetDir = m_workspaceFolders.first();
        }
    }

    if (targetDir.isEmpty()) {
        event->ignore();
        return false;
    }

    // 防止拖到自身所在目录（无意义操作）
    QString sourceDir = QFileInfo(sourcePath).absolutePath();
    if (QDir(sourceDir) == QDir(targetDir)) {
        event->ignore();
        return false;
    }

    // 防止拖到子目录（避免循环，简单起见不允许）
    QString targetAbs = QDir(targetDir).absolutePath();
    QString sourceAbs = QFileInfo(sourcePath).absoluteFilePath();
    if (sourceAbs.startsWith(targetAbs + QStringLiteral("/"))) {
        event->ignore();
        return false;
    }

    // 发射信号交由 SideBar/Widget 层处理实际移动
    emit fileMoveRequested(sourcePath, targetDir);
    event->accept();
    return true;
}
