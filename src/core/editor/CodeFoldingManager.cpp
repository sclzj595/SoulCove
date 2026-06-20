#include "core/editor/CodeFoldingManager.h"
#include "core/config/ThemeManager.h"

#include <QTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QPainter>
#include <QPalette>
#include <QFont>

// ========== 构造 ==========

CodeFoldingManager::CodeFoldingManager(QTextEdit* editor, QObject* parent)
    : QObject(parent)
    , m_editor(editor)
{
}

// ========== 折叠区域扫描 ==========

void CodeFoldingManager::scanFoldRegions()
{
    m_foldRegions.clear();
    m_foldableBlocks.clear();

    QTextBlock block = m_editor->document()->firstBlock();
    while (block.isValid()) {
        QString text = block.text();
        // 查找该行中 { 的位置（跳过字符串/注释中的 { 简化处理）
        int bracePos = -1;
        bool inString = false;
        QChar stringChar;
        for (int i = 0; i < text.size(); ++i) {
            QChar ch = text[i];
            if (inString) {
                if (ch == stringChar && (i == 0 || text[i-1] != '\\')) inString = false;
            } else {
                if (ch == '"' || ch == '\'') { inString = true; stringChar = ch; }
                else if (ch == '/' && i + 1 < text.size() && text[i+1] == '/') break;  // 行注释
                else if (ch == '{') { bracePos = i; break; }
            }
        }

        if (bracePos >= 0) {
            // 查找匹配的 }
            int startPos = block.position() + bracePos;
            int endPos = m_findMatchingBracket ? m_findMatchingBracket(startPos) : -1;
            if (endPos >= 0) {
                QTextBlock endBlock = m_editor->document()->findBlock(endPos);
                if (endBlock.isValid() && endBlock.blockNumber() > block.blockNumber()) {
                    FoldRegion region;
                    region.startBlock = block.blockNumber();
                    region.endBlock = endBlock.blockNumber();
                    region.folded = false;
                    m_foldRegions.append(region);
                    m_foldableBlocks.append(block.blockNumber());
                }
            }
        }
        block = block.next();
    }
}

// ========== 折叠状态查询/操作 ==========

CodeFoldingManager::FoldRegion* CodeFoldingManager::findFoldRegion(int blockNumber)
{
    for (auto& region : m_foldRegions) {
        if (region.startBlock == blockNumber) return &region;
    }
    return nullptr;
}

void CodeFoldingManager::applyFoldState()
{
    // 遍历所有折叠区域，隐藏/显示块
    QTextBlock block = m_editor->document()->firstBlock();
    while (block.isValid()) {
        bool shouldHide = false;
        for (const auto& region : m_foldRegions) {
            if (region.folded && block.blockNumber() > region.startBlock &&
                block.blockNumber() <= region.endBlock) {
                shouldHide = true;
                break;
            }
        }
        block.setVisible(!shouldHide);
        block = block.next();
    }

    // 触发布局更新
    m_editor->document()->markContentsDirty(0, m_editor->document()->characterCount());
    if (m_requestUpdate) m_requestUpdate();
    m_editor->viewport()->update();
}

void CodeFoldingManager::toggleFold(int blockNumber)
{
    FoldRegion* region = findFoldRegion(blockNumber);
    if (!region) return;

    region->folded = !region->folded;
    applyFoldState();
}

bool CodeFoldingManager::isFoldable(int blockNumber) const
{
    for (const auto& region : m_foldRegions) {
        if (region.startBlock == blockNumber) return true;
    }
    return false;
}

bool CodeFoldingManager::isFolded(int blockNumber) const
{
    for (const auto& region : m_foldRegions) {
        if (region.startBlock == blockNumber) return region.folded;
    }
    return false;
}

// ========== 行号区交互 ==========

void CodeFoldingManager::onLineNumberAreaClicked(const QPoint& pos, int areaWidth,
                                                 const QTextCursor& cursor)
{
    // 折叠图标绘制在行号区右侧，尺寸 m_foldIconSize
    int iconX = areaWidth - m_foldIconSize - 2;

    if (cursor.isNull()) return;
    int blockNumber = cursor.blockNumber();

    // 检查是否点击在折叠图标区域
    if (pos.x() >= iconX && pos.x() <= iconX + m_foldIconSize) {
        if (isFoldable(blockNumber)) {
            toggleFold(blockNumber);
        }
    }
}

void CodeFoldingManager::paintFoldIcon(QPainter& painter, int blockNumber,
                                       int iconX, int top, int fontHeight,
                                       int editorSize)
{
    if (!isFoldable(blockNumber)) return;

    const auto& palette = ThemeManager::instance().currentPalette();

    int iconSize = m_foldIconSize;
    int iconY = static_cast<int>(top + (fontHeight - iconSize) / 2);

    // 绘制图标背景方块
    painter.fillRect(iconX, iconY, iconSize, iconSize, palette.borderDefault);

    // 绘制图标符号：折叠状态显示 ▶，展开状态显示 ▼
    painter.setPen(palette.fgPrimary);
    QFont iconFont = painter.font();
    iconFont.setPointSize(qMax(6, editorSize - 3));
    painter.setFont(iconFont);

    if (isFolded(blockNumber)) {
        painter.drawText(iconX, iconY, iconSize, iconSize,
                        Qt::AlignCenter, QStringLiteral("\u25B6"));
    } else {
        painter.drawText(iconX, iconY, iconSize, iconSize,
                        Qt::AlignCenter, QStringLiteral("\u25BC"));
    }
}
