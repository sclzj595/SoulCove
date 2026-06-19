#include "ui/markdown/MdTocPanel.h"
#include "core/config/ThemeManager.h"

#include <QHeaderView>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QDebug>

// ============================================================
// 构造函数
// ============================================================

MdTocPanel::MdTocPanel(QWidget* parent)
    : QTreeWidget(parent)
{
    // 基本设置
    setHeaderHidden(true);              // 隐藏表头
    setRootIsDecorated(false);          // 隐藏根节点装饰
    setAlternatingRowColors(false);     // 不使用交替行颜色
    setSelectionMode(SingleSelection);   // 单选模式
    setAnimated(true);                  // 启用动画效果

    // 列设置（单列：标题文本）
    setColumnCount(1);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);

    // 样式设置（跟随主题，适配亮/暗模式）
    applyStyleSheet();

    // 连接点击信号
    connect(this, &QTreeWidget::itemClicked,
            this, &MdTocPanel::onItemClicked);

    // 监听主题切换，动态刷新样式
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MdTocPanel::updateTheme);

    // 设置工具提示
    setToolTip(tr("目录导航 - 点击标题跳转"));
}

// ============================================================
// 公共接口
// ============================================================

void MdTocPanel::updateToc(const QList<TocEntry>& entries)
{
    // 保存当前选中项的anchorId（用于恢复选择）
    QString currentAnchor;
    QTreeWidgetItem* currentSelected = currentItem();  // 使用不同的变量名避免冲突
    if (currentSelected && m_itemAnchorMap.contains(currentSelected)) {
        currentAnchor = m_itemAnchorMap[currentSelected];
    }

    // 清空现有内容
    clear();
    m_itemAnchorMap.clear();
    m_itemLineMap.clear();

    if (entries.isEmpty()) {
        // 显示空状态提示
        auto* emptyItem = new QTreeWidgetItem(this);
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
            addTopLevelItem(item);
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
            setCurrentItem(item);
        }
    }

    // 展开所有项
    expandAll();
}

void MdTocPanel::clearToc()
{
    clear();
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

    if (bestMatch && bestMatch != currentItem()) {
        // 使用blockSignals防止触发点击事件
        blockSignals(true);
        setCurrentItem(bestMatch);
        blockSignals(false);

        // 确保可见
        scrollToItem(bestMatch, QTreeWidget::PositionAtCenter);
    }
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
    ).arg(bg, fgDim, hoverBg, selBg, accent, scrollHandle, scrollHandleHover));
}

void MdTocPanel::updateTheme()
{
    applyStyleSheet();
}
