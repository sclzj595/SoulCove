#include "ui/markdown/MdTocPanel.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QDebug>
#include <QPushButton>
#include <QTreeWidget>
#include <QSet>

// ============================================================
// 构造函数
// ============================================================

MdTocPanel::MdTocPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ===== 顶部按钮栏：全部展开 / 全部折叠 =====
    auto* btnBar = new QWidget(this);
    auto* btnLayout = new QHBoxLayout(btnBar);
    btnLayout->setContentsMargins(6, 4, 6, 4);
    btnLayout->setSpacing(4);

    m_btnExpandAll = new QPushButton(tr("全部展开"), btnBar);
    m_btnCollapseAll = new QPushButton(tr("全部折叠"), btnBar);
    m_btnExpandAll->setObjectName(QStringLiteral("mdTocBtn"));
    m_btnCollapseAll->setObjectName(QStringLiteral("mdTocBtn"));
    m_btnExpandAll->setCursor(Qt::PointingHandCursor);
    m_btnCollapseAll->setCursor(Qt::PointingHandCursor);
    m_btnExpandAll->setFlat(true);
    m_btnCollapseAll->setFlat(true);

    auto* btnSpacer = new QWidget();
    btnSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    btnLayout->addWidget(m_btnExpandAll);
    btnLayout->addWidget(m_btnCollapseAll);
    btnLayout->addWidget(btnSpacer);

    layout->addWidget(btnBar);

    // ===== 内部树控件 =====
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);              // 隐藏表头
    m_tree->setRootIsDecorated(true);           // 显示根节点装饰（折叠三角）
    m_tree->setAlternatingRowColors(false);     // 不使用交替行颜色
    m_tree->setSelectionMode(QTreeWidget::SingleSelection);
    m_tree->setAnimated(true);                  // 启用动画效果
    m_tree->setUniformRowHeights(true);

    // 列设置（单列：标题文本）
    m_tree->setColumnCount(1);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    layout->addWidget(m_tree, 1);

    // 样式设置（跟随主题，适配亮/暗模式）
    applyStyleSheet();

    // 连接点击信号
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &MdTocPanel::onItemClicked);

    // 连接折叠/展开信号（持久化状态）
    connect(m_tree, &QTreeWidget::itemExpanded,
            this, &MdTocPanel::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemCollapsed,
            this, &MdTocPanel::onItemCollapsed);

    // 按钮信号
    connect(m_btnExpandAll, &QPushButton::clicked, this, &MdTocPanel::onExpandAll);
    connect(m_btnCollapseAll, &QPushButton::clicked, this, &MdTocPanel::onCollapseAll);

    // 监听主题切换，动态刷新样式
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MdTocPanel::updateTheme);

    // 设置工具提示
    setToolTip(tr("目录导航 - 点击标题跳转，点击三角图标折叠/展开"));
}

// ============================================================
// 公共接口
// ============================================================

void MdTocPanel::updateToc(const QList<TocEntry>& entries)
{
    // 保存当前选中项的anchorId（用于恢复选择）
    QString currentAnchor;
    QTreeWidgetItem* currentSelected = m_tree->currentItem();
    if (currentSelected && m_itemAnchorMap.contains(currentSelected)) {
        currentAnchor = m_itemAnchorMap[currentSelected];
    }

    // 清空现有内容
    m_tree->clear();
    m_itemAnchorMap.clear();
    m_itemLineMap.clear();

    if (entries.isEmpty()) {
        // 显示空状态提示
        auto* emptyItem = new QTreeWidgetItem(m_tree);
        emptyItem->setText(0, tr("无标题"));
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
        emptyItem->setForeground(0, QColor(136, 136, 136));
        return;
    }

    // 构建树形结构（使用栈追踪父节点）
    QVector<QTreeWidgetItem*> parentStack;  // 每个层级的父节点
    parentStack.resize(7);  // index 0-6 (0 unused, 1-6 for H1-H6)

    for (const auto& entry : entries) {
        int level = qBound(1, entry.level, 6);

        // 找到合适的父节点（当前级别或更高级别的最近祖先）
        QTreeWidgetItem* parent = nullptr;
        for (int i = level - 1; i >= 1; --i) {
            if (parentStack[i] != nullptr) {
                parent = parentStack[i];
                break;
            }
        }

        // 创建节点
        QTreeWidgetItem* item = createTocItem(entry, parent);
        if (!parent) {
            m_tree->addTopLevelItem(item);
        }

        // 更新当前级别的父节点
        parentStack[level] = item;

        // 清除更低级别的父节点缓存（因为新节点打断了它们）
        for (int i = level + 1; i <= 6; ++i) {
            parentStack[i] = nullptr;
        }

        // 存储映射关系
        m_itemAnchorMap[item] = entry.anchorId;
        m_itemLineMap[item] = entry.lineNumber;

        // 恢复之前选中的项
        if (!currentAnchor.isEmpty() && entry.anchorId == currentAnchor) {
            m_tree->setCurrentItem(item);
        }
    }

    // 恢复该文件的折叠状态（若无历史记录则默认全部展开）
    restoreCollapsedState();
}

void MdTocPanel::clearToc()
{
    m_tree->clear();
    m_itemAnchorMap.clear();
    m_itemLineMap.clear();
}

void MdTocPanel::highlightActiveItem(int lineNumber)
{
    // 查找最接近当前行号的标题项
    QTreeWidgetItem* bestMatch = nullptr;
    int bestDiff = INT_MAX;

    for (auto it = m_itemLineMap.constBegin(); it != m_itemLineMap.constEnd(); ++it) {
        int diff = qAbs(it.value() - lineNumber);
        if (diff < bestDiff && it.value() <= lineNumber) {
            bestDiff = diff;
            bestMatch = it.key();
        }
    }

    if (bestMatch && bestMatch != m_tree->currentItem()) {
        // 使用blockSignals防止触发点击事件
        m_tree->blockSignals(true);
        m_tree->setCurrentItem(bestMatch);
        m_tree->blockSignals(false);

        // 确保可见
        m_tree->scrollToItem(bestMatch, QTreeWidget::PositionAtCenter);
    }
}

void MdTocPanel::setFilePath(const QString& filePath)
{
    if (m_filePath != filePath) {
        m_filePath = filePath;
    }
}

void MdTocPanel::expandAll()
{
    m_tree->expandAll();
    saveCollapsedState();
}

void MdTocPanel::collapseAll()
{
    m_tree->collapseAll();
    saveCollapsedState();
}

// ============================================================
// 私有槽
// ============================================================

void MdTocPanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item || !m_itemAnchorMap.contains(item)) return;

    QString anchorId = m_itemAnchorMap[item];
    int lineNum = m_itemLineMap[item];

    emit tocItemClicked(anchorId, lineNum);
}

void MdTocPanel::updateTheme()
{
    applyStyleSheet();
}

void MdTocPanel::onItemExpanded(QTreeWidgetItem* item)
{
    Q_UNUSED(item)
    saveCollapsedState();
}

void MdTocPanel::onItemCollapsed(QTreeWidgetItem* item)
{
    Q_UNUSED(item)
    saveCollapsedState();
}

void MdTocPanel::onExpandAll()
{
    expandAll();
}

void MdTocPanel::onCollapseAll()
{
    collapseAll();
}

// ============================================================
// 私有方法
// ============================================================

QTreeWidgetItem* MdTocPanel::createTocItem(const TocEntry& entry, QTreeWidgetItem* parent)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, entry.title);
    item->setToolTip(0, QStringLiteral("H%1: %2\n行 %3").arg(entry.level).arg(entry.title).arg(entry.lineNumber + 1));

    // 设置图标（根据级别）
    item->setIcon(0, iconForLevel(entry.level));

    // 根据级别设置字体大小和缩进
    QFont font = item->font(0);
    if (entry.level == 1) {
        font.setBold(true);
        font.setPointSize(14);
    } else if (entry.level == 2) {
        font.setBold(true);
        font.setPointSize(13);
    } else if (entry.level == 3) {
        font.setPointSize(12);
    } else {
        font.setPointSize(11);
    }
    item->setFont(0, font);

    return item;
}

QIcon MdTocPanel::iconForLevel(int level) const
{
    switch (level) {
    case 1:
        return QIcon();
    case 2:
        return QIcon();
    default:
        return QIcon();
    }
}

void MdTocPanel::applyStyleSheet()
{
    const auto& p = ThemeManager::instance().currentPalette();
    auto c = [](const QColor& color) -> QString {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };

    QString fgDim = c(p.fgSecondary);
    QString bg = c(p.bgSideBar);
    QString accent = c(p.accentPrimary);
    QString hoverBg = c(p.bgHover.isValid() ? p.bgHover : QColor(255, 255, 255, 12));
    QString selBg = c(p.selectionBg);
    QString btnBg = c(p.bgInput.isValid() ? p.bgInput : p.bgSideBar);

    QString scrollHandle = QStringLiteral("rgba(%1,%2,%3,0.25)")
        .arg(p.accentPrimary.red()).arg(p.accentPrimary.green()).arg(p.accentPrimary.blue());
    QString scrollHandleHover = QStringLiteral("rgba(%1,%2,%3,0.45)")
        .arg(p.accentPrimary.red()).arg(p.accentPrimary.green()).arg(p.accentPrimary.blue());

    setStyleSheet(QStringLiteral(
        "QTreeWidget { border: none; background: %1; font-size: 13px; }"
        "QTreeWidget::item { padding: 4px 8px; border-radius: 4px; margin: 1px 2px; color: %2; }"
        "QTreeWidget::item:hover { background-color: %3; color: %2; }"
        "QTreeWidget::item:selected { background-color: %4; color: #ffffff; }"
        "QTreeWidget::item:selected:active { background-color: %4; border-left: 3px solid %5; padding-left: 5px; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: %6; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: %7; }"
        "QPushButton#mdTocBtn {"
        "  border: none; padding: 3px 10px; border-radius: 3px;"
        "  background: %8; color: %2; font-size: 11px;"
        "}"
        "QPushButton#mdTocBtn:hover { background: %3; color: #fff; }"
    ).arg(bg, fgDim, hoverBg, selBg, accent, scrollHandle, scrollHandleHover, btnBg));
}

void MdTocPanel::saveCollapsedState() const
{
    if (m_filePath.isEmpty()) return;  // 无文件路径则不持久化

    // 收集当前折叠的节点行号
    QList<int> collapsedLines;
    for (auto it = m_itemLineMap.constBegin(); it != m_itemLineMap.constEnd(); ++it) {
        QTreeWidgetItem* item = it.key();
        // 仅有子节点的项才可能处于折叠状态；top-level + 有子节点
        if (item->childCount() > 0 && !item->isExpanded()) {
            collapsedLines.append(it.value());
        }
    }

    // 加载已有状态，更新当前文件，写回
    auto state = ConfigManager::instance().mdTocCollapsedState();
    state.insert(m_filePath, collapsedLines);
    ConfigManager::instance().setMdTocCollapsedState(state);
}

void MdTocPanel::restoreCollapsedState()
{
    if (m_filePath.isEmpty()) {
        // 无文件路径 → 默认全部展开
        m_tree->expandAll();
        return;
    }

    auto state = ConfigManager::instance().mdTocCollapsedState();
    if (!state.contains(m_filePath)) {
        // 无历史记录 → 默认全部展开
        m_tree->expandAll();
        return;
    }

    const QList<int> collapsedLines = state.value(m_filePath);
    QSet<int> collapsedSet(collapsedLines.begin(), collapsedLines.end());

    // 先全部展开，再折叠指定行号对应的节点
    m_tree->expandAll();

    // 阻塞信号避免触发 itemCollapsed → saveCollapsedState 回环
    m_tree->blockSignals(true);
    for (auto it = m_itemLineMap.constBegin(); it != m_itemLineMap.constEnd(); ++it) {
        if (collapsedSet.contains(it.value()) && it.key()->childCount() > 0) {
            m_tree->collapseItem(it.key());
        }
    }
    m_tree->blockSignals(false);
}
