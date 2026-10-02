#include "core/editor/CodeFoldingManager.h"
#include "core/config/ThemeManager.h"

#include <QTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QPainter>
#include <QPalette>
#include <QFont>
#include <QRegularExpression>

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

    // 1. 扫描 {} 配对折叠（原有逻辑）
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
                    region.type = Brace;
                    m_foldRegions.append(region);
                    m_foldableBlocks.append(block.blockNumber());
                }
            }
        }
        block = block.next();
    }

    // 2. P3-M03 子项2: 扫描 #region / #endregion（C#/VS 风格）
    scanRegionMarkers();

    // 3. P3-M03 子项2: 扫描 // {{{ / // }}} 和 // region: / // endregion: 自定义标记
    scanCustomMarkers();
}

// P3-M03 子项2: 扫描 #region / #endregion 标记
// 匹配：行首（允许前导空白）的 #region 和 #endregion，# 后允许空格
// 标记名可选（如 "#region Public Methods"），仅作起始/终止配对用
void CodeFoldingManager::scanRegionMarkers()
{
    static const QRegularExpression regionRe(QStringLiteral("^\\s*#\\s*region\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression endregionRe(QStringLiteral("^\\s*#\\s*endregion\\b"),
                                                QRegularExpression::CaseInsensitiveOption);

    // 用栈配对 #region / #endregion
    QList<int> regionStack;  // 起始块号栈
    QTextBlock block = m_editor->document()->firstBlock();
    while (block.isValid()) {
        QString text = block.text();
        if (text.contains(regionRe)) {
            regionStack.push_back(block.blockNumber());
        } else if (text.contains(endregionRe)) {
            if (!regionStack.isEmpty()) {
                int startBlock = regionStack.takeLast();
                // 仅当结束块在起始块之后才记录（避免单行 region）
                if (block.blockNumber() > startBlock) {
                    FoldRegion region;
                    region.startBlock = startBlock;
                    region.endBlock = block.blockNumber();
                    region.folded = false;
                    region.type = Region;
                    m_foldRegions.append(region);
                    m_foldableBlocks.append(startBlock);
                }
            }
        }
        block = block.next();
    }
    // 未配对的 #region（栈中剩余）：不记录折叠区域（与 VSCode 行为一致）
}

// P3-M03 子项2: 扫描自定义折叠标记
// 支持：
//   - // {{{ 和 // }}}（Vim/VSCode 风格）
//   - // region: 和 // endregion:（注释风格折叠标记）
//   - /* {{{ */ 和 /* }}} */（块注释风格，简化处理）
void CodeFoldingManager::scanCustomMarkers()
{
    // 单行注释风格：// {{{ / // region: 起始，// }}} / // endregion: 结束
    static const QRegularExpression startRe(
        QStringLiteral("//\\s*(\\{\\{\\{|region:)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression endRe(
        QStringLiteral("//\\s*(\\}\\}\\}|endregion:)"),
        QRegularExpression::CaseInsensitiveOption);

    QList<int> markerStack;  // 起始块号栈
    QTextBlock block = m_editor->document()->firstBlock();
    while (block.isValid()) {
        QString text = block.text();
        // 起始标记必须在结束标记之前检查（同行不视为折叠区域）
        if (text.contains(startRe)) {
            markerStack.push_back(block.blockNumber());
        } else if (text.contains(endRe)) {
            if (!markerStack.isEmpty()) {
                int startBlock = markerStack.takeLast();
                if (block.blockNumber() > startBlock) {
                    FoldRegion region;
                    region.startBlock = startBlock;
                    region.endBlock = block.blockNumber();
                    region.folded = false;
                    region.type = Comment;
                    m_foldRegions.append(region);
                    m_foldableBlocks.append(startBlock);
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

const CodeFoldingManager::FoldRegion* CodeFoldingManager::findFoldRegionConst(int blockNumber) const
{
    for (const auto& region : m_foldRegions) {
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

    // P3-M03 子项2: 通知外部折叠状态变化（type 强转 int 避免 Q_ENUM 注册要求）
    emit foldStateChanged(blockNumber, region->folded, static_cast<int>(region->type));
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

// P3-M03 子项2: 查询指定块的折叠区域类型
CodeFoldingManager::FoldRegionType CodeFoldingManager::foldRegionType(int blockNumber) const
{
    const FoldRegion* region = findFoldRegionConst(blockNumber);
    return region ? region->type : Brace;
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

// P0 C02-2: 统计已折叠区域数量，作为行号栏缓存失效签名
int CodeFoldingManager::foldedBlockCount() const
{
    int count = 0;
    for (const FoldRegion& r : m_foldRegions) {
        if (r.folded) ++count;
    }
    return count;
}

void CodeFoldingManager::paintFoldIcon(QPainter& painter, int blockNumber,
                                       int iconX, int top, int fontHeight,
                                       int editorSize)
{
    if (!isFoldable(blockNumber)) return;

    const auto& palette = ThemeManager::instance().currentPalette();

    int iconSize = m_foldIconSize;
    int iconY = static_cast<int>(top + (fontHeight - iconSize) / 2);

    // P3-M03 子项2: 根据折叠区域类型用不同颜色区分图标背景
    // Brace（默认灰）/ Region（蓝紫，#region）/ Comment（绿，// {{{）
    FoldRegionType type = foldRegionType(blockNumber);
    QColor bgColor = palette.borderDefault;  // 默认背景色（Brace）
    QColor fgColor = palette.fgPrimary;
    switch (type) {
    case Region:
        bgColor = QColor(100, 150, 220, 180);  // 蓝色（#region 标记）
        fgColor = QColor(255, 255, 255);
        break;
    case Comment:
        bgColor = QColor(120, 180, 120, 180);  // 绿色（// {{{ 自定义标记）
        fgColor = QColor(255, 255, 255);
        break;
    case Indentation:
        bgColor = QColor(200, 170, 120, 180);  // 棕黄（缩进折叠，预留）
        fgColor = palette.fgPrimary;
        break;
    case Brace:
    default:
        break;  // 使用默认 palette 颜色
    }

    // 绘制图标背景方块
    painter.fillRect(iconX, iconY, iconSize, iconSize, bgColor);

    // 绘制图标符号：折叠状态显示 ▶，展开状态显示 ▼
    painter.setPen(fgColor);
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
