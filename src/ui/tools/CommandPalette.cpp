#include "ui/tools/CommandPalette.h"
#include "core/config/ThemeManager.h"

#include <QKeyEvent>
#include <QApplication>
#include <QScreen>
#include <QFont>

CommandPalette::CommandPalette(QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("commandPalette"));
    setFixedSize(560, 380);
    setAttribute(Qt::WA_TranslucentBackground);

    // 整体布局
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 搜索框（顶部）
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("paletteSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("输入命令..."));
    m_searchEdit->setMinimumHeight(40);
    layout->addWidget(m_searchEdit);

    // 结果列表（下方）
    m_resultList = new QListWidget(this);
    m_resultList->setObjectName(QStringLiteral("paletteResultList"));
    m_resultList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_resultList, 1);

    // 应用当前主题样式
    applyTheme();

    // 监听主题切换 — 切换时实时刷新样式
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &CommandPalette::applyTheme);

    // 安装事件过滤器：点击外部区域时自动隐藏
    qApp->installEventFilter(this);

    // 连接信号槽
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &CommandPalette::onSearchTextChanged);
    connect(m_resultList, &QListWidget::itemActivated,
            this, &CommandPalette::onItemActivated);
}

void CommandPalette::registerCommand(const CommandItem& cmd)
{
    m_allCommands.append(cmd);
}

void CommandPalette::showPalette()
{
    if (m_allCommands.isEmpty()) return;

    // 清空搜索框，显示所有命令
    m_searchEdit->clear();
    onSearchTextChanged(QString());

    // 移动到屏幕中央偏上位置
    if (parentWidget()) {
        QRect parentGeo = parentWidget()->geometry();
        int x = parentGeo.x() + (parentGeo.width() - width()) / 2;
        int y = parentGeo.y() + (parentGeo.height() - height()) / 3;  // 偏上
        move(x, y);
    }

    show();
    raise();
    activateWindow();
    m_searchEdit->setFocus();

    // 默认选中第一项
    if (m_resultList->count() > 0) {
        m_resultList->setCurrentRow(0);
    }
}

void CommandPalette::hidePalette()
{
    hide();
    m_searchEdit->clear();
}

bool CommandPalette::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        // 点击面板外部区域则隐藏
        auto* widget = qobject_cast<QWidget*>(obj);
        if (widget && !isAncestorOf(widget)) {
            hidePalette();
            return true;
        }
    }
    return QFrame::eventFilter(obj, event);
}

void CommandPalette::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        hidePalette();
        break;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_resultList->currentItem()) {
            onItemActivated(m_resultList->currentItem());
        }
        break;

    case Qt::Key_Up:
        if (m_resultList->currentRow() > 0) {
            m_resultList->setCurrentRow(m_resultList->currentRow() - 1);
        }
        break;

    case Qt::Key_Down:
        if (m_resultList->currentRow() < m_resultList->count() - 1) {
            m_resultList->setCurrentRow(m_resultList->currentRow() + 1);
        }
        break;

    default:
        QFrame::keyPressEvent(event);
        break;
    }
}

/// @brief 搜索文本变化时进行模糊匹配并更新结果列表
void CommandPalette::onSearchTextChanged(const QString& text)
{
    m_resultList->clear();

    QString lowerText = text.toLower();

    for (const auto& cmd : m_allCommands) {
        // 对 title / id / category 都做模糊匹配
        bool match = false;
        if (lowerText.isEmpty()) {
            match = true;
        } else {
            QString lowerTitle = cmd.title.toLower();
            QString lowerId = cmd.id.toLower();
            QString lowerCategory = cmd.category.toLower();

            // 简单子串模糊匹配
            if (lowerTitle.contains(lowerText) ||
                lowerId.contains(lowerText) ||
                lowerCategory.contains(lowerText)) {
                match = true;
            }
        }

        if (match) {
            // 显示格式: "标题 | 分类 | 快捷键"
            QString displayText = cmd.title;
            if (!cmd.shortcut.isEmpty())
                displayText += QStringLiteral("  |  ") + cmd.shortcut;
            displayText += QStringLiteral("  [") + cmd.category + QStringLiteral("]");

            auto* item = new QListWidgetItem(displayText, m_resultList);
            item->setData(Qt::UserRole, cmd.id);  // 存储命令ID

            // 简单高亮匹配文字
            if (!lowerText.isEmpty()) {
                // 使用富文本高亮（简单方式：用HTML标记匹配部分）
                QString richTitle = cmd.title;
                int pos = 0;
                while ((pos = cmd.title.toLower().indexOf(lowerText, pos)) != -1) {
                    // QListWidgetItem 不直接支持富文本，
                    // 这里通过设置 tooltip 或保持纯文本即可
                    pos++;
                }
                item->setToolTip(cmd.title + QStringLiteral("\n") +
                                 tr("分类: ") + cmd.category +
                                 (cmd.shortcut.isEmpty() ? QString() :
                                  tr("\n快捷键: ") + cmd.shortcut));
            }
        }
    }

    // 自动选中第一项
    if (m_resultList->count() > 0 && !m_resultList->currentItem()) {
        m_resultList->setCurrentRow(0);
    }
}

/// @brief 列表项被激活（双击或回车）时触发对应命令
void CommandPalette::onItemActivated(QListWidgetItem* item)
{
    if (!item) return;

    QString commandId = item->data(Qt::UserRole).toString();
    emit commandTriggered(commandId);
    hidePalette();
}

void CommandPalette::applyTheme()
{
    const auto& palette = ThemeManager::instance().currentPalette();
    const QString accentColor = palette.accentPrimary.name();

    // 根据主题明暗自动切换面板配色
    // 亮色主题：bgEditor 较亮（lightness > 128）
    const bool isLightTheme = palette.bgEditor.lightness() > 128;

    if (isLightTheme) {
        // ===== 浅色模式配色 =====
        setStyleSheet(
            QStringLiteral(
                "QFrame#commandPalette {"
                "   background-color: #ffffff;"
                "   border: 1px solid #e0e0e0;"
                "   border-radius: 8px;"
                "}"
                "QLineEdit#paletteSearchEdit {"
                "   background-color: #f3f3f3;"
                "   color: #333333;"
                "   border: none;"
                "   padding: 6px 12px;"
                "   font-size: 14px;"
                "   border-bottom: 1px solid #e0e0e0;"
                "}"
                "QLineEdit#paletteSearchEdit:focus {"
                "   border-bottom: 2px solid %1;"
                "}"
                "QListWidget#paletteResultList {"
                "   background-color: #ffffff;"
                "   color: #333333;"
                "   border: none;"
                "   outline: none;"
                "   font-size: 13px;"
                "}"
                "QListWidget#paletteResultList::item {"
                "   padding: 6px 12px;"
                "   border-bottom: 1px solid #f0f0f0;"
                "}"
                "QListWidget#paletteResultList::item:selected {"
                "   background-color: %1;"
                "   color: #ffffff;"
                "}"
                "QListWidget#paletteResultList::item:hover:!selected {"
                "   background-color: #f5f5f5;"
                "}"
                ).arg(accentColor)
        );
    } else {
        // ===== 暗色模式配色 =====
        setStyleSheet(
            QStringLiteral(
                "QFrame#commandPalette {"
                "   background-color: #252526;"
                "   border: 1px solid #3c3c3c;"
                "   border-radius: 8px;"
                "}"
                "QLineEdit#paletteSearchEdit {"
                "   background-color: #3c3c3c;"
                "   color: #cccccc;"
                "   border: none;"
                "   padding: 6px 12px;"
                "   font-size: 14px;"
                "   border-bottom: 1px solid #3c3c3c;"
                "}"
                "QLineEdit#paletteSearchEdit:focus {"
                "   border-bottom: 2px solid %1;"
                "}"
                "QListWidget#paletteResultList {"
                "   background-color: #252526;"
                "   color: #cccccc;"
                "   border: none;"
                "   outline: none;"
                "   font-size: 13px;"
                "}"
                "QListWidget#paletteResultList::item {"
                "   padding: 6px 12px;"
                "   border-bottom: 1px solid #2d2d2d;"
                "}"
                "QListWidget#paletteResultList::item:selected {"
                "   background-color: %1;"
                "   color: #ffffff;"
                "}"
                "QListWidget#paletteResultList::item:hover:!selected {"
                "   background-color: #2a2d2e;"
                "}"
                ).arg(accentColor)
        );
    }
}
