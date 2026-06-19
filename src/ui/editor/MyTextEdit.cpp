#include "ui/editor/MyTextEdit.h"
#include "ui/editor/TextCompleter.h"       // 具体类仅在cpp中使用（用于isChineseChar等静态方法）
#include "ui/editor/LineNumberArea.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"
#include "core/editor/CodeSyntaxHighlighter.h"
#include "Logger.hpp"

#include <QRegExp>
#include <QSet>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QColor>
#include <QWidget>
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QClipboard>
#include <QRegularExpression>
#include <QGuiApplication>
#include <QFontInfo>
#include <algorithm>

// 默认可见 实际要根据配置文件来修改
MyTextEdit::MyTextEdit(QWidget *parent) : QTextEdit(parent), lineNumersVisible(true)
{
    // 样式由全局QSS (ThemeManager) 控制，不设内联样式

    // 事件过滤器的方式来重写事件  拦截Tab Esc
    installEventFilter(this);

    // 行号相关
    lineNumberArea = new LineNumberArea(this);

    // 行号相关信号 滑动滚动条和文本信号变化 要和行号更新
    connect(this->verticalScrollBar(), &QScrollBar::valueChanged, this, &MyTextEdit::updateLineNumberArea);
    connect(this, &QTextEdit::textChanged, this, &MyTextEdit::updateLineNumberArea);
    connect(this, &QTextEdit::cursorPositionChanged, this, &MyTextEdit::updateLineNumberArea);

    // 滚动条值变化时同步更新迷你地图视口指示器位置
    connect(this->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (m_minimapVisible) updateMinimap();
    });

    // 配置补全延迟定时器：单次触发，间隔100ms（避免输入时频繁更新补全列表）
    m_completionTimer.setSingleShot(true);
    m_completionTimer.setInterval(100);

    connect(&m_completionTimer, &QTimer::timeout, this, &MyTextEdit::updateCompletion);
    // 新增逻辑 连接文本变化信号到自定义处理器 (中间层 用于控制更新逻辑)
    connect(this, &QTextEdit::textChanged, this, &MyTextEdit::handleTextChanged);
    connect(this, &QTextEdit::cursorPositionChanged, this, &MyTextEdit::cursorPositionChangedInternal);

    // ========== 迷你地图初始化 (M7) ==========
    m_minimapWidget = new QWidget(this);
    m_minimapWidget->setObjectName(QStringLiteral("minimap"));
    m_minimapWidget->setFixedWidth(80);
    m_minimapWidget->setCursor(Qt::PointingHandCursor);

    // 迷你地图点击事件 → 跳转编辑器位置
    m_minimapWidget->installEventFilter(this);

    // 延迟更新定时器（文本变更时延迟200ms再更新，避免频繁重绘）
    m_minimapUpdateTimer.setSingleShot(true);
    m_minimapUpdateTimer.setInterval(200);
    connect(&m_minimapUpdateTimer, &QTimer::timeout, this, &MyTextEdit::updateMinimap);

    // 加载缩进配置（tabSize / indentStyle）
    loadIndentConfig();

    // 监听配置变更（设置页修改 tabSize/indentStyle 时实时生效）
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, [this](const QString& key) {
        if (key == QStringLiteral("Editor/tabSize") ||
            key == QStringLiteral("Editor/indentStyle")) {
            loadIndentConfig();
        }
    });

    // ========== L16: 鼠标悬停 LSP hover 初始化 ==========
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(500);  // 500ms 防抖
    connect(&m_hoverTimer, &QTimer::timeout, this, [this]() {
        // 定时器触发时，获取鼠标位置对应的文本光标位置
        QTextCursor cursor = cursorForPosition(m_lastHoverPos);
        if (cursor.isNull()) return;
        int line = cursor.blockNumber();
        int col = cursor.columnNumber();
        emit lspHoverRequested(line, col);
    });
    setMouseTracking(true);  // 启用鼠标追踪，即使不按键也能收到 mouseMoveEvent
}

// 中间函数 防止补全操作引起递归更新 触发补全更新
/**
 * @brief 文本变化中间处理器
 * @details 作为QTextEdit::textChanged信号的接收者，用于控制补全更新逻辑：
 *          - 通过m_ignoreNextUpdate标志防止补全插入文本引发的递归更新
 *          - 通知补全组件文本变化，并启动延迟定时器更新补全列表
 */
void MyTextEdit::handleTextChanged()
{
    // 如果标记成忽略下一次 重置标志 return 起到阻断递归
    if (m_ignoreNextUpdate) {
        m_ignoreNextUpdate = false;
        return;
    }

    // 补全器未初始化时静默跳过，不打印警告避免刷屏
    if (!m_completer) return;

    // 获取上下文
    auto context = m_completer->getCurrentContext();
    QString prefix = context.first;

    // 触发补全
    if (prefix.length() >= m_completer->getMinPrefixLen()) {
        emit textChangedForCompletion();
        m_completionTimer.start();
    } else {
        m_completer->hideCompletion();
    }

    // 触发迷你地图延迟更新 (M7)
    if (m_minimapVisible)
        m_minimapUpdateTimer.start();

    // 代码折叠：文本变更时重新扫描折叠区域（防抖，避免频繁扫描）
    // 仅在补全器初始化后才扫描（避免 setPlainText 时触发）
    static QTimer* foldScanTimer = nullptr;
    if (!foldScanTimer) {
        foldScanTimer = new QTimer(this);
        foldScanTimer->setSingleShot(true);
        foldScanTimer->setInterval(500);
        connect(foldScanTimer, &QTimer::timeout, this, [this]() {
            scanFoldRegions();
            updateLineNumberArea();
        });
    }
    foldScanTimer->start();
}

/**
 * @brief 定时器触发的补全更新函数
 * @details 当延迟定时器超时后，更新单词列表并通知补全组件刷新列表
 *          增加m_ignoreNextUpdate判断，防止在忽略更新状态下执行
 */
void MyTextEdit::updateCompletion() {
    // 补全组件存在 不处于忽略更新状态 
    if (m_completer && !m_ignoreNextUpdate) {
        auto context = m_completer->getCurrentContext();
        if (context.first.length() >= 2) {
            updateWordList();
            m_completer->updateCompletionList();
        }
        else    m_completer->hideCompletion();
    }
}

/**
 * @brief 设置补全组件（通过ICompleter接口）
 * @param completer 新的补全组件实例（接口指针）
 * @details 替换当前补全组件，旧组件会被安全销毁，新组件将关联到当前文本编辑器
 */
void MyTextEdit::setCompleter(ICompleter* completer)
{
    // 销毁旧补全组件
    if (m_completer) {
        auto* oldWidget = m_completer->asWidget();
        if (oldWidget) oldWidget->deleteLater();
    }
    m_completer = completer;
    // 关联新的补全组件到文本编辑器
    if (m_completer) {
        m_completer->bindEditor(this);
    }
}

void MyTextEdit::enableSyntaxHighlighting(const QString& fileSuffix)
{
    if (!m_syntaxHighlighter) {
        m_syntaxHighlighter = new CodeSyntaxHighlighter(document());
    }
    m_syntaxHighlighter->setupRules(fileSuffix);
}

void MyTextEdit::disableSyntaxHighlighting()
{
    if (m_syntaxHighlighter) {
        m_syntaxHighlighter->deleteLater();
        m_syntaxHighlighter = nullptr;
    }
}

void MyTextEdit::updateSyntaxHighlightColors()
{
    if (m_syntaxHighlighter) {
        m_syntaxHighlighter->updateThemeColors();
    }
}

void MyTextEdit::setSemanticSymbols(const QList<QVariantMap>& symbols)
{
    if (m_syntaxHighlighter) {
        m_syntaxHighlighter->setSemanticSymbols(symbols);
    }
}

void MyTextEdit::clearSemanticSymbols()
{
    if (m_syntaxHighlighter) {
        m_syntaxHighlighter->clearSemanticSymbols();
    }
}

void MyTextEdit::setExternalSymbols(const QList<QPair<QString, QString>>& symbols)
{
    if (m_syntaxHighlighter) {
        m_syntaxHighlighter->setExternalSymbols(symbols);
    }
}

void MyTextEdit::updateWordList()
{
    // 从textEdit提取历史记录
    QString text = this->toPlainText();
    QTextCursor cursor = this->textCursor();
    QStringList wordList;

    // // 正则来获取  提取单词
    // QRegExp wordRegex("\\b[\\w\\p{Han}]+\\b");
    // int pos = 0;
    // // 保证位置有效
    // while ((pos = wordRegex.indexIn(text, pos)) != -1) {
    //     wordList << wordRegex.cap();        // cap返回当前匹配的内容
    //     pos += wordRegex.matchedLength();   // 移动到下一个匹配的位置
    // }
    // 使用支持中文的正则表达式
    QRegularExpression wordRegex(
        R"(([\w\p{Han}]+))"  // 匹配单词字符和汉字
    );
    QRegularExpressionMatchIterator it = wordRegex.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        wordList << match.captured();
    }
    
    // 添加单字中文（增强中文补全）
    for (int i = 0; i < text.length(); i++) {
        QChar ch = text[i];
        if (TextCompleter::isChineseChar(ch)) {
            wordList << QString(ch);
        }
    }

    // 排序 字典序 去重
    // wordList = wordList.toSet().toList();
    QSet<QString> uniqueWords(wordList.begin(), wordList.end());
    wordList = QStringList(uniqueWords.begin(), uniqueWords.end());
    // 不用排序了
    // std::sort(wordList.begin(), wordList.end());     

    // 进补全提示框
    if (m_completer)    // 要改参数
        m_completer->setWordList(text, cursor.position());
    m_wordList = wordList;
}

void MyTextEdit::focusOutEvent(QFocusEvent* event) {
    if (m_completer && m_completer->isCompletionVisible()) {
        // 检查新焦点是否在补全框内
        if (!m_completer->isCompleterFocused()) {
            m_completer->hideCompletion();
        } else {
            // 确保补全框保持激活状态
            QTimer::singleShot(0, m_completer->asWidget(), &QWidget::activateWindow);
        }
    }
    QTextEdit::focusOutEvent(event);
}

/// @brief 处理输入
/// @param event 输入事件对象
/// 输入文字的时候 触发该事件  
/// 重写事件 --> 输入内容之后 自动补全列表 及时更新
void MyTextEdit::inputMethodEvent(QInputMethodEvent* event) {
    // 首先调用基类的默认 确保正常工作
    QTextEdit::inputMethodEvent(event);

    // 补全提示框已经init 输入之后就更新补全列表 要不能处于忽略状态
    if (m_completer && !m_ignoreNextUpdate) {
        // 先更新列表 再触发
        // updateWordList();
        // m_completer->updateCompletionList();
        m_completionTimer.start();
    }   
}

/**
 * @brief 事件过滤器，用于拦截并处理特定事件
 * @param obj 事件来源对象
 * @param event 事件对象
 * @return true表示事件已处理，false表示继续传递给其他对象
 * 
 * 这里主要用于处理Tab键与补全列表的交互：
 * 当补全列表显示时，按下Tab键会触发补全选择，而不是默认的插入制表符
 */
bool MyTextEdit::eventFilter(QObject *obj, QEvent* event) {
    // 判断是否按下
    if (event->type() == QEvent::KeyPress) {
        // obj --> keyEvent 继承基类 隐式转换
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        // 按下tab 补全列表 出现提示框  将事件转发给补全组件处理
        if (keyEvent->key() == Qt::Key_Tab && m_completer && m_completer->isCompletionVisible() && m_completer->itemCount() > 0) {
            // tab 键盘事件特殊处理
            QCoreApplication::sendEvent(m_completer->asWidget(), keyEvent);
            // 处理完成了就不向下传递  避免默认情况
            return true;
        }
        // 处理ESC键隐藏补全框
        if (keyEvent->key() == Qt::Key_Escape && m_completer && m_completer->isCompletionVisible())
        {
            m_completer->hideCompletion();
            return true;
        }
    }

    // 迷你地图鼠标事件处理 (M7)
    if (obj == m_minimapWidget) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            onMinimapClicked(mouseEvent);
            return true;
        }
        if (event->type() == QEvent::Paint) {
            auto* paintEvent = static_cast<QPaintEvent*>(event);
            paintMinimapEvent(paintEvent);
            return true;
        }
    }
    // 不做特殊的就是默认 插入缩进
    return QTextEdit::eventFilter(obj, event);
}

// 发出信号 光标移动更新
void MyTextEdit::cursorPositionChangedInternal()
{
    emit cursorPositionChangedSignal();

    // 补全框可见 光标移动更新
    if (m_completer && m_completer->isCompletionVisible()) {
        // 延迟 避免频繁刷新
        m_completionTimer.start();
    }
}

// void MyTextEdit::textChangedForCompletion()
// {
// }

// void MyTextEdit::completionHidden()
// {
//     if (m_completer && m_completer->isVisible()) {
//         m_completer->hideCompletion();
//         emit completionHidden(); // 发出隐藏信号
//     }
//     QTextEdit::focusOutEvent(event);
// }



void MyTextEdit::wheelEvent(QWheelEvent *event)
{
    // Ctrl + 滚轮缩放字体（使用标准 modifiers 检测，无需维护状态标志）
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0)
            fontZoomIn();
        else if (event->angleDelta().y() < 0)
            fontZoomOut();
        event->accept();
    } else {
        QTextEdit::wheelEvent(event);
    }
}

// ========== 自动缩进实现 ==========

void MyTextEdit::loadIndentConfig()
{
    auto& config = ConfigManager::instance();
    m_tabSize = config.getValue("Editor/tabSize", 4).toInt();
    QString style = config.getValue("Editor/indentStyle", QStringLiteral("spaces")).toString();
    m_useSpaces = (style != QStringLiteral("tabs"));

    // 设置 Tab 停止宽度（视觉上 1 个 Tab = tabSize 个字符宽）
    QFontMetrics fm(font());
    setTabStopDistance(fm.horizontalAdvance(' ') * m_tabSize);
}

QString MyTextEdit::currentLineIndent() const
{
    // 获取当前光标所在行的前导空白（空格 + Tab）
    QTextCursor cursor = textCursor();
    QTextBlock block = cursor.block();
    if (!block.isValid()) return QString();

    QString lineText = block.text();
    QString indent;
    for (const QChar& ch : lineText) {
        if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t'))
            indent += ch;
        else
            break;
    }
    return indent;
}

void MyTextEdit::insertIndent()
{
    // 根据配置插入空格或 Tab 字符
    if (m_useSpaces) {
        insertPlainText(QString(m_tabSize, QLatin1Char(' ')));
    } else {
        insertPlainText(QLatin1String("\t"));
    }
}

// 优化 keyPressEvent 函数
void MyTextEdit::keyPressEvent(QKeyEvent *event)
{
    int key = event->key();

    // ====== T17: 多光标编辑模式 ======
    if (m_multiCursorMode && !m_secondaryCursors.isEmpty()) {
        // Esc: 清除所有次级光标
        if (key == Qt::Key_Escape) {
            clearSecondaryCursors();
            event->accept();
            return;
        }

        // 字符输入（无 Ctrl/Alt/Meta 修饰）
        if (!event->text().isEmpty() && event->text().size() == 1 &&
            !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            QChar ch = event->text().at(0);
            applyToAllCursors(ch);
            event->accept();
            return;
        }

        // Backspace: 所有光标删除前一个字符
        if (key == Qt::Key_Backspace) {
            backspaceAllCursors();
            event->accept();
            return;
        }

        // Delete: 所有光标删除后一个字符
        if (key == Qt::Key_Delete) {
            deleteAllCursors();
            event->accept();
            return;
        }

        // Enter: 所有光标插入换行
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            applyToAllCursors(QChar('\n'));
            event->accept();
            return;
        }

        // 方向键: 移动所有光标
        if (key == Qt::Key_Left || key == Qt::Key_Right ||
            key == Qt::Key_Up || key == Qt::Key_Down ||
            key == Qt::Key_Home || key == Qt::Key_End) {
            QTextCursor::MoveOperation op = QTextCursor::NoMove;
            switch (key) {
                case Qt::Key_Left:  op = QTextCursor::Left; break;
                case Qt::Key_Right: op = QTextCursor::Right; break;
                case Qt::Key_Up:    op = QTextCursor::Up; break;
                case Qt::Key_Down:  op = QTextCursor::Down; break;
                case Qt::Key_Home:  op = QTextCursor::StartOfLine; break;
                case Qt::Key_End:   op = QTextCursor::EndOfLine; break;
                default: break;
            }
            QTextCursor::MoveMode mode = (event->modifiers() & Qt::ShiftModifier)
                ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
            moveAllCursors(op, mode);
            event->accept();
            return;
        }

        // 其他按键（Ctrl+C 等组合键）fallthrough 到正常处理
    }

    // ====== 快捷键已迁移至 ShortcutFilter (qApp eventFilter) 统一管理 ======
    // Ctrl+S/D/I/F/H 等不再由编辑器层处理，由 ShortcutFilter 拦截分发
    // 这解耦了编辑器与快捷键逻辑，符合 Command + Filter + Observer 设计模式

    // ====== Enter: 自动缩进 ======
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // 补全框可见时，Enter 选择补全项（不触发自动缩进）
        if (m_completer && m_completer->isCompletionVisible() && m_completer->hasCurrentItem()) {
            QCoreApplication::sendEvent(m_completer->asWidget(), event);
            return;
        }

        // 1. 获取当前行文本（用于智能缩进判断）
        QTextCursor cursor = textCursor();
        QString lineText = cursor.block().text();
        // 去掉前导空白后的行内容
        QString trimmedLeft = lineText.trimmed();

        // 2. 执行默认换行（基类插入 \n）
        QTextEdit::keyPressEvent(event);

        // 3. 复制当前行的前导缩进到新行
        QString indent = currentLineIndent();
        if (!indent.isEmpty()) {
            textCursor().insertText(indent);
        }

        // 4. 智能缩进：如果上一行以 { [ ( : 结尾，额外增加一级缩进
        if (!trimmedLeft.isEmpty()) {
            QChar lastChar = trimmedLeft.at(trimmedLeft.length() - 1);
            if (lastChar == QLatin1Char('{') ||
                lastChar == QLatin1Char('[') ||
                lastChar == QLatin1Char('(') ||
                lastChar == QLatin1Char(':')) {
                insertIndent();  // 插入 tabSize 个空格或一个 Tab
            }
        }

        m_completionTimer.start();
        highlightMatchingBracket();
        return;
    }

    // ====== Tab: 空格/制表符切换 ======
    if (key == Qt::Key_Tab) {
        // 补全框可见时，Tab 选择补全项
        if (m_completer && m_completer->isCompletionVisible() && m_completer->itemCount() > 0) {
            QCoreApplication::sendEvent(m_completer->asWidget(), event);
            return;
        }
        // 配置为空格缩进时，插入空格而非 Tab 字符
        if (m_useSpaces) {
            insertPlainText(QString(m_tabSize, QLatin1Char(' ')));
        } else {
            QTextEdit::keyPressEvent(event);
        }
        m_completionTimer.start();
        return;
    }

    // 删除操作
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        QTextEdit::keyPressEvent(event);
        m_completionTimer.start();
        return;
    }

    // 处理补全框可见时的按键事件
    if (m_completer && m_completer->isCompletionVisible()) {
        // 上下键在补全框内选择
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            QCoreApplication::sendEvent(m_completer->asWidget(), event);
            return;
        }
        // ESC键隐藏补全框
        else if (key == Qt::Key_Escape) {
            m_completer->hideCompletion();
            return;
        }
        // 左右键隐藏补全框并移动光标
        else if (key == Qt::Key_Left || key == Qt::Key_Right) {
            m_completer->hideCompletion();
        }
    }

    // 正常处理其他按键
    QTextEdit::keyPressEvent(event);

    // M8: Ctrl+Space 触发 LSP 补全请求
    if (key == Qt::Key_Space && (event->modifiers() & Qt::ControlModifier)) {
        requestLspCompletion();
        return;
    }

    // 更新单词列表并触发补全（排除修饰键）
    if (key != Qt::Key_Control &&
        key != Qt::Key_Shift &&
        key != Qt::Key_Alt &&
        key != Qt::Key_Meta) {
        m_completionTimer.start();
    }

    // 括号匹配高亮
    highlightMatchingBracket();
}

void MyTextEdit::keyReleaseEvent(QKeyEvent *event)
{
    QTextEdit::keyReleaseEvent(event);
}

////////////////////////////  行号

/// @brief 行号区域可见性
/// @param visible 是否可见
void MyTextEdit::setLineNumberVisible(bool visible)
{
    lineNumersVisible = visible;
    lineNumberArea->setVisible(visible);
    updateLineNumberArea();
}

void MyTextEdit::updateLineNumberArea()
{
    // 前导检查
    if (!lineNumersVisible)     return;

    // 编辑器边界：左侧给行号留空间，右侧给迷你地图留空间
    int rightMargin = (m_minimapVisible && m_minimapWidget) ? m_minimapWidget->width() : 0;
    setViewportMargins(lineNumberAreaWidth(), 0, rightMargin, 0);

    // 更新行号区域几何位置（通过接口）
    QRect rect = contentsRect();
    lineNumberArea->updateGeometry(rect, lineNumberAreaWidth());
    lineNumberArea->updateLineNumber();
}

int MyTextEdit::lineNumberAreaWidth() const
{
    // 根据位数 更新 位数到了有大的时候 就要更新
    int digits = 1;
    int maxDig = qMax(1, document()->blockCount());

    // 最大行号的位数
    while (maxDig >= 10) {
        maxDig /= 10;
        digits++;
    }

    // 宽度
    return  10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void MyTextEdit::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    // 获取底层QWidget进行绘制
    QWidget* areaWidget = lineNumberArea->asWidget();
    if (!areaWidget) return;

    // Qpainter 来画
    QPainter painter(areaWidget);
    // 使用主题色板中的侧边栏背景色
    const auto& palette = ThemeManager::instance().currentPalette();
    painter.fillRect(event->rect(), palette.bgSideBar);

    // 替换为 QTextEdit 兼容的方式获取第一个可见块
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::Start);
    
    // 计算可见区域
    QRect visibleRect = viewport()->rect();
    int verticalScrollValue = verticalScrollBar()->value();
    int contentHeight = document()->documentLayout()->documentSize().height();
    
    // 查找第一个可见的文本块
    QTextBlock block = document()->firstBlock();
    int blockNumber = block.blockNumber();
    
    // 获取文档布局
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    
    // 计算块的顶部位置
    QRectF blockRect = layout->blockBoundingRect(block);
    qreal top = blockRect.top();
    qreal bottom = top + blockRect.height();
    
    // 调整到可见区域
    top -= verticalScrollValue;
    bottom -= verticalScrollValue;
    
    painter.setPen(palette.fgLineNumber);  // 使用主题色板行号色
    QFont font = this->font();
    // 行号字体跟随编辑器字体大小（比编辑器字体小 1pt，视觉更协调）
    // 修复：QSS 用 font-size: Xpx 时 pointSize() 返回 -1，需兜底
    int editorSize = font.pointSize();
    if (editorSize <= 0) {
        editorSize = ConfigManager::instance().fontSize();
        if (editorSize <= 0) editorSize = 14;
    }
    font.setPointSize(editorSize > 2 ? editorSize - 1 : editorSize);
    painter.setFont(font);

    // 遍历所有文本块
    while (block.isValid()) {
        blockRect = layout->blockBoundingRect(block);
        top = blockRect.top() - verticalScrollValue;
        bottom = top + blockRect.height();
        
        // 检查块是否在可见区域内
        if (bottom >= visibleRect.top() && top <= visibleRect.bottom()) {
            QString number = QString::number(blockNumber + 1);
            painter.drawText(0, top, areaWidget->width(), fontMetrics().height(),
                            Qt::AlignCenter, number);
        }

        // 绘制折叠图标（在行号区右侧）
        if (isFoldable(block.blockNumber())) {
            int iconSize = m_foldIconSize;
            int iconX = areaWidget->width() - iconSize - 2;
            int iconY = static_cast<int>(top + (fontMetrics().height() - iconSize) / 2);

            // 绘制图标背景方块
            painter.fillRect(iconX, iconY, iconSize, iconSize, palette.borderDefault);

            // 绘制图标符号：折叠状态显示 ▶，展开状态显示 ▼
            painter.setPen(palette.fgPrimary);
            QFont iconFont = painter.font();
            iconFont.setPointSize(qMax(6, editorSize - 3));
            painter.setFont(iconFont);

            if (isFolded(block.blockNumber())) {
                painter.drawText(iconX, iconY, iconSize, iconSize,
                                Qt::AlignCenter, QStringLiteral("\u25B6"));
            } else {
                painter.drawText(iconX, iconY, iconSize, iconSize,
                                Qt::AlignCenter, QStringLiteral("\u25BC"));
            }
        }

        // 移动到下一个文本块
        block = block.next();
        blockNumber++;
    }
}

// ========== 代码折叠实现 ==========

void MyTextEdit::scanFoldRegions()
{
    m_foldRegions.clear();
    m_foldableBlocks.clear();

    QTextBlock block = document()->firstBlock();
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
            int endPos = findMatchingBracket(startPos);
            if (endPos >= 0) {
                QTextBlock endBlock = document()->findBlock(endPos);
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

MyTextEdit::FoldRegion* MyTextEdit::findFoldRegion(int blockNumber)
{
    for (auto& region : m_foldRegions) {
        if (region.startBlock == blockNumber) return &region;
    }
    return nullptr;
}

void MyTextEdit::applyFoldState()
{
    // 遍历所有折叠区域，隐藏/显示块
    QTextBlock block = document()->firstBlock();
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
    document()->markContentsDirty(0, document()->characterCount());
    updateLineNumberArea();
    viewport()->update();
}

void MyTextEdit::toggleFold(int blockNumber)
{
    FoldRegion* region = findFoldRegion(blockNumber);
    if (!region) return;

    region->folded = !region->folded;
    applyFoldState();
}

bool MyTextEdit::isFoldable(int blockNumber) const
{
    for (const auto& region : m_foldRegions) {
        if (region.startBlock == blockNumber) return true;
    }
    return false;
}

bool MyTextEdit::isFolded(int blockNumber) const
{
    for (const auto& region : m_foldRegions) {
        if (region.startBlock == blockNumber) return region.folded;
    }
    return false;
}

void MyTextEdit::lineNumberAreaClicked(const QPoint& pos, int areaWidth)
{
    // 折叠图标绘制在行号区右侧，尺寸 m_foldIconSize
    int iconX = areaWidth - m_foldIconSize - 2;

    // 计算点击位置对应的块号
    QTextCursor cursor = cursorForPosition(QPoint(0, pos.y()));
    if (cursor.isNull()) return;
    int blockNumber = cursor.blockNumber();

    // 检查是否点击在折叠图标区域
    if (pos.x() >= iconX && pos.x() <= iconX + m_foldIconSize) {
        if (isFoldable(blockNumber)) {
            toggleFold(blockNumber);
        }
    }
}

/// @brief 窗口大小改变事件
/// @param event 大小改变对象
void MyTextEdit::resizeEvent(QResizeEvent* event)
{
    QTextEdit::resizeEvent(event);
    updateLineNumberArea();

    // 更新迷你地图位置和大小 (M7)
    if (m_minimapWidget && m_minimapVisible) {
        // minimapWidget 是 this 的子控件，使用 this 的坐标系定位到右侧
        int mapW = m_minimapWidget->width();
        m_minimapWidget->setGeometry(
            width() - mapW,
            0,
            mapW,
            height());
        m_minimapWidget->show();
        // 触发一次更新
        m_minimapUpdateTimer.start(100);
    }
}

void MyTextEdit::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);   // 基类保证默认进行

    // 收集所有额外选择（当前行高亮 + 括号匹配高亮）
    QList<QTextEdit::ExtraSelection> extraSelections;

    // 当前行高亮 - 使用主题色板
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        const auto& themePalette = ThemeManager::instance().currentPalette();
        QColor lineColor = themePalette.currentLineBg;
        lineColor.setAlpha(180);
        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    // 括号匹配高亮
    extraSelections.append(m_bracketSelections);

    // T17: 次级光标高亮
    extraSelections.append(m_secondarySelections);

    setExtraSelections(extraSelections);

    // ========== M8: LSP 诊断波浪线绘制 ==========
    if (!m_diagnostics.isEmpty()) {
        QPainter painter(viewport());
        const int lineHeight = fontMetrics().height();

        for (const auto& diag : m_diagnostics) {
            // 根据严重程度选择颜色
            QColor waveColor;
            switch (diag.severity) {
            case LspDiagnosticOverlay::Error:   waveColor = QColor(255, 0, 0);     break;   // 红色
            case LspDiagnosticOverlay::Warning: waveColor = QColor(255, 165, 0);   break;   // 橙色/黄色
            case LspDiagnosticOverlay::Info:    waveColor = QColor(0, 120, 215);   break;   // 蓝色
            case LspDiagnosticOverlay::Hint:    waveColor = QColor(128, 128, 128); break;   // 灰色
            default: waveColor = QColor(255, 0, 0); break;
            }

            // 计算诊断区域的像素坐标
            // 修复 P2-2: 使用 left() 而非 right() 作为起点，避免光标在行首/空白位置时
            // cursorRect().right() 返回异常值导致波浪线从 x=0 开始覆盖整行
            QTextCursor cursor(document());
            cursor.movePosition(QTextCursor::Start);
            cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, diag.startLine);
            cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, diag.startCol);

            QRect startRect = cursorRect(cursor);
            int startX = startRect.left();
            int startY = startRect.bottom();

            // 绘制波浪线（从诊断起始位置延伸到行尾）
            if (startY > 0) {
                // 计算行尾 X 坐标：取行末光标的 left()，若失败则回退到视口右边
                QTextCursor endCursor(cursor);
                endCursor.movePosition(QTextCursor::EndOfLine);
                int endX = cursorRect(endCursor).right();
                if (endX <= startX) {
                    endX = viewport()->width() - 10;  // 回退：波浪线延伸到接近行尾
                }
                painter.setPen(QPen(waveColor, 1, Qt::SolidLine));
                int y = startY;
                for (int x = startX; x < endX; x += 6) {
                    painter.drawLine(x, y, x + 3, y + 2);
                    painter.drawLine(x + 3, y + 2, x + 6, y);
                }
            }
        }
    }
}

/// @brief 鼠标点击事件：处理 Alt+Click 添加次级光标，点击后更新括号匹配高亮
void MyTextEdit::mouseMoveEvent(QMouseEvent* event)
{
    // L16: 鼠标移动时重置悬停定时器（500ms 防抖）
    // 只有当鼠标位置变化时才重置，避免不必要的 LSP 请求
    if (m_lastHoverPos != event->pos()) {
        m_lastHoverPos = event->pos();
        m_hoverTimer.start();
    }
    QTextEdit::mouseMoveEvent(event);
}

void MyTextEdit::mousePressEvent(QMouseEvent* event)
{
    // T17: Alt+Click 添加次级光标
    if (event->modifiers() & Qt::AltModifier) {
        QTextCursor clickCursor = cursorForPosition(event->pos());
        if (clickCursor.isNull()) {
            QTextEdit::mousePressEvent(event);
            return;
        }
        // 添加次级光标（主光标保持原位，由 QTextEdit 原生管理）
        m_secondaryCursors.append(clickCursor);
        m_multiCursorMode = true;
        updateSecondaryCursorDisplay();
        event->accept();
        return;
    }

    // Ctrl+左键单击：跳转定义（与 F12 等效）
    if ((event->modifiers() & Qt::ControlModifier) && event->button() == Qt::LeftButton) {
        QTextCursor clickCursor = cursorForPosition(event->pos());
        if (!clickCursor.isNull()) {
            setTextCursor(clickCursor);  // 先定位光标到点击符号，供 onLspGotoDefinition 读取
            emit lspGotoDefinitionRequested();
            event->accept();
            return;
        }
    }

    // 非 Alt 点击：调用父类，清除次级光标
    QTextEdit::mousePressEvent(event);
    if (m_multiCursorMode) {
        clearSecondaryCursors();
    }
    highlightMatchingBracket();
}

// ========== 括号匹配高亮实现 ==========

bool MyTextEdit::isBracketChar(const QChar& ch)
{
    return ch == QLatin1Char('(') || ch == QLatin1Char(')') ||
           ch == QLatin1Char('[') || ch == QLatin1Char(']') ||
           ch == QLatin1Char('{') || ch == QLatin1Char('}');
}

QChar MyTextEdit::matchingBracket(const QChar& ch)
{
    switch (ch.unicode()) {
    case '(':  return QLatin1Char(')');
    case ')':  return QLatin1Char('(');
    case '[':  return QLatin1Char(']');
    case ']':  return QLatin1Char('[');
    case '{':  return QLatin1Char('}');
    case '}':  return QLatin1Char('{');
    default:   return QChar();
    }
}

int MyTextEdit::findMatchingBracket(int position) const
{
    QString text = toPlainText();
    if (position < 0 || position >= text.length()) return -1;

    QChar ch = text.at(position);
    if (!isBracketChar(ch)) return -1;

    QChar match = matchingBracket(ch);
    bool isForward = (ch == QLatin1Char('(') || ch == QLatin1Char('[') || ch == QLatin1Char('{'));

    int count = 1;  // 当前括号计数

    if (isForward) {
        // 开括号 → 向后查找闭括号
        for (int i = position + 1; i < text.length(); ++i) {
            if (text.at(i) == ch) {
                ++count;
            } else if (text.at(i) == match) {
                --count;
                if (count == 0) return i;  // 找到配对位置
            }
        }
    } else {
        // 闭括号 → 向前查找开括号
        for (int i = position - 1; i >= 0; --i) {
            if (text.at(i) == ch) {
                ++count;
            } else if (text.at(i) == match) {
                --count;
                if (count == 0) return i;  // 找到配对位置
            }
        }
    }

    return -1;  // 未找到配对
}

void MyTextEdit::highlightMatchingBracket()
{
    // 清除旧的高亮
    m_bracketSelections.clear();
    m_matchStartPos = -1;
    m_matchEndPos = -1;

    QTextCursor cursor = textCursor();
    int pos = cursor.position();

    QString text = toPlainText();

    // 光标可能在括号字符上，也可能在括号后面（刚输入完）
    // 检查光标位置的字符
    int bracketPos = -1;
    if (pos > 0 && pos <= text.length()) {
        // 检查光标前一个字符（处理刚输入完括号的场景）
        QChar prevCh = text.at(pos - 1);
        if (isBracketChar(prevCh)) {
            bracketPos = pos - 1;
        }
    }
    // 如果前面不是，检查当前位置的字符
    if (bracketPos == -1 && pos < text.length()) {
        QChar curCh = text.at(pos);
        if (isBracketChar(curCh)) {
            bracketPos = pos;
        }
    }

    if (bracketPos == -1) {
        // 没有括号，清除高亮后刷新显示
        setExtraSelections(extraSelections());  // 仅保留当前行高亮
        return;
    }

    // 查找配对位置
    int matchPos = findMatchingBracket(bracketPos);
    if (matchPos == -1) {
        // 没有配对，仅高亮当前括号
        setExtraSelections(extraSelections());
        return;
    }

    // 记录匹配位置
    m_matchStartPos = qMin(bracketPos, matchPos);
    m_matchEndPos = qMax(bracketPos, matchPos);

    // 创建两个 ExtraSelection（当前位置和匹配位置），使用柔和黄色半透明背景
    QColor highlightColor(255, 200, 0, 60);  // rgba(255,200,0,60)

    // 当前括号位置高亮
    QTextEdit::ExtraSelection selCurrent;
    selCurrent.format.setBackground(highlightColor);
    selCurrent.cursor = QTextCursor(document());
    selCurrent.cursor.setPosition(bracketPos);
    selCurrent.cursor.setPosition(bracketPos + 1, QTextCursor::KeepAnchor);
    m_bracketSelections.append(selCurrent);

    // 配对括号位置高亮
    QTextEdit::ExtraSelection selMatch;
    selMatch.format.setBackground(highlightColor);
    selMatch.cursor = QTextCursor(document());
    selMatch.cursor.setPosition(matchPos);
    selMatch.cursor.setPosition(matchPos + 1, QTextCursor::KeepAnchor);
    m_bracketSelections.append(selMatch);

    // 应用所有额外选择（触发重绘）
    QList<QTextEdit::ExtraSelection> allSelections;

    // 当前行高亮
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection lineSel;
        const auto& themePalette = ThemeManager::instance().currentPalette();
        QColor lineColor = themePalette.currentLineBg;
        lineColor.setAlpha(180);
        lineSel.format.setBackground(lineColor);
        lineSel.format.setProperty(QTextFormat::FullWidthSelection, true);
        lineSel.cursor = textCursor();
        lineSel.cursor.clearSelection();
        allSelections.append(lineSel);
    }

    // 括号高亮
    allSelections.append(m_bracketSelections);

    // T17: 次级光标高亮
    allSelections.append(m_secondarySelections);

    setExtraSelections(allSelections);
}

void MyTextEdit::showCompleter()
{
    if (!m_completer)   return;
    m_completer->updateCompletionList();
}

// ========== IEditorEdit 接口方法实现 ==========

void MyTextEdit::setFontSize(int size)
{
    // 修复：QSS 使用 font-size: Xpx 设置 pixelSize 时 pointSize() 返回 -1，
    // 此处对入参做合法性校验，兜底为 ConfigManager 配置值
    if (size <= 0) {
        size = ConfigManager::instance().fontSize();
        if (size <= 0) size = 14;  // 最终兜底
    }
    QFont font = this->font();
    // 同时清除 pixelSize，避免 pointSize 与 pixelSize 共存时 Qt 行为不确定
    font.setPixelSize(-1);
    font.setPointSize(size);
    this->setFont(font);
    // 通知外部（Widget 同步到 ConfigManager 和其他编辑器）
    emit fontSizeChanged(size);
}

int MyTextEdit::fontSize() const
{
    int ps = this->font().pointSize();
    if (ps > 0) return ps;
    // pointSize 为 -1 时（QSS 用 font-size: Xpx 设置了 pixelSize），
    // 从 pixelSize 反推 pointSize：pt = px * 72 / dpi
    int px = this->font().pixelSize();
    if (px > 0) {
        qreal dpi = QFontInfo(this->font()).pixelSize() > 0
                        ? QGuiApplication::primaryScreen()->logicalDotsPerInch()
                        : 96.0;
        return qMax(1, qRound(px * 72.0 / dpi));
    }
    // 最终兜底：ConfigManager 配置值
    return ConfigManager::instance().fontSize();
}

void MyTextEdit::fontZoomIn()
{
    QFont font = this->font();
    int size = font.pointSize();
    if (size > 0) {
        int newSize = size + 1;
        font.setPointSize(newSize);
        this->setFont(font);
        // 持久化到配置 + 通知外部同步其他编辑器
        ConfigManager::instance().setFontSize(newSize);
        emit fontSizeChanged(newSize);
    }
}

void MyTextEdit::fontZoomOut()
{
    QFont font = this->font();
    int size = font.pointSize();
    if (size > 1) {
        int newSize = size - 1;
        font.setPointSize(newSize);
        this->setFont(font);
        // 持久化到配置 + 通知外部同步其他编辑器
        ConfigManager::instance().setFontSize(newSize);
        emit fontSizeChanged(newSize);
    }
}

bool MyTextEdit::isLineNumberVisible() const
{
    return lineNumersVisible;
}

// ========== 迷你地图实现 (M7) ==========

void MyTextEdit::updateMinimap()
{
    if (!m_minimapWidget || !m_minimapVisible) return;

    int w = m_minimapWidget->width();
    int h = m_minimapWidget->height();

    if (w <= 0 || h <= 0) return;

    // 计算文档总高度和可见区域比例
    QAbstractTextDocumentLayout* layout = document()->documentLayout();
    qreal docHeight = layout->documentSize().height();
    qreal visibleHeight = viewport()->height();

    if (docHeight <= 0) {
        m_minimapWidget->update();
        return;
    }

    // 缩放比例
    double scaleY = static_cast<double>(h) / docHeight;

    // 创建缩略图（宽度固定80px，高度按比例）
    delete m_minimapImage;
    m_minimapImage = new QImage(w, h, QImage::Format_RGB32);

    // 使用主题编辑器背景色（适配亮/暗模式）
    const auto& themePalette = ThemeManager::instance().currentPalette();
    m_minimapImage->fill(themePalette.bgEditor);

    QPainter painter(m_minimapImage);

    // 使用缩小的字体渲染文本
    QFont miniFont = this->font();
    miniFont.setPointSize(1);  // 极小字体
    painter.setFont(miniFont);

    // 渲染每一行（简化：只画文字颜色，保留语法高亮色相）
    QTextBlock block = document()->firstBlock();
    while (block.isValid()) {
        QRectF blockRect = layout->blockBoundingRect(block);
        int y = static_cast<int>(blockRect.top() * scaleY);
        int lineH = qMax(1, static_cast<int>(blockRect.height() * scaleY));

        QString text = block.text();
        if (!text.isEmpty()) {
            QColor fgColor = themePalette.fgPrimary;
            painter.setPen(fgColor);
            QString displayText = text.left(w / 2);
            painter.drawText(1, y + lineH - 1, displayText);
        }

        block = block.next();
    }

    // 绘制可见区域指示器（半透明矩形）
    drawMinimapViewport(painter, w, h, scaleY, visibleHeight);

    painter.end();

    // 触发重绘
    m_minimapWidget->update();
}

void MyTextEdit::drawMinimapViewport(QPainter& painter, int w, int h, double scaleY, qreal visibleHeight)
{
    QScrollBar* vBar = verticalScrollBar();
    int scrollMax = vBar->maximum();
    int viewH = qMax(4, static_cast<int>(visibleHeight * scaleY));
    int viewY;
    if (scrollMax > 0) {
        viewY = static_cast<int>(static_cast<double>(vBar->value()) / scrollMax * (h - viewH));
    } else {
        viewY = 0;
    }

    // 根据主题明暗选择指示器颜色
    const auto& palette = ThemeManager::instance().currentPalette();
    bool isLight = palette.bgEditor.lightness() > 128;
    QColor fillColor = isLight ? QColor(0, 0, 0, 20) : QColor(255, 255, 255, 25);
    QColor borderColor = isLight ? QColor(0, 0, 0, 50) : QColor(255, 255, 255, 80);

    painter.fillRect(0, viewY, w, viewH, fillColor);
    painter.setPen(borderColor);
    painter.drawRect(0, viewY, w - 1, viewH - 1);
}

void MyTextEdit::paintMinimapEvent(QPaintEvent* event)
{
    if (!m_minimapImage || m_minimapImage->isNull()) return;

    QPainter painter(m_minimapWidget);
    painter.drawImage(event->rect(), *m_minimapImage, event->rect());
}

void MyTextEdit::onMinimapClicked(QMouseEvent* event)
{
    if (!m_minimapWidget) return;

    // 点击位置转换为文档滚动位置
    // [修复] 使用滚动条最大值正确计算比例，与 updateMinimap() 保持一致
    QScrollBar* vBar = verticalScrollBar();
    int scrollMax = vBar->maximum();
    int minimapHeight = m_minimapWidget->height();

    if (scrollMax <= 0 || minimapHeight <= 0) return;

    double ratio = static_cast<double>(event->pos().y()) / minimapHeight;
    int targetScroll = static_cast<int>(ratio * scrollMax);

    verticalScrollBar()->setValue(targetScroll);
}

void MyTextEdit::toggleMinimap(bool visible)
{
    m_minimapVisible = visible;
    if (m_minimapWidget) {
        m_minimapWidget->setVisible(visible);
        if (visible)
            updateMinimap();
    }
}

// ========== M8: LSP 诊断覆盖层实现 ==========

void MyTextEdit::setDiagnostics(const QList<LspDiagnosticOverlay>& diagnostics)
{
    m_diagnostics = diagnostics;
    // 触发重绘以显示波浪线
    viewport()->update();
}

void MyTextEdit::clearDiagnostics()
{
    m_diagnostics.clear();
    viewport()->update();
}

QList<LspDiagnosticOverlay> MyTextEdit::diagnosticsForLine(int line) const
{
    QList<LspDiagnosticOverlay> result;
    for (const auto& diag : m_diagnostics) {
        if (diag.startLine == line || (line >= diag.startLine && line <= diag.endLine)) {
            result.append(diag);
        }
    }
    return result;
}

void MyTextEdit::requestLspCompletion()
{
    int line = textCursor().blockNumber();       // 0-based 行号
    int col = textCursor().columnNumber();        // 0-based 列号
    emit lspCompletionRequested(line, col);
}

// ====================================================================
// 右键菜单 (VSCode 风格增强版)
// ====================================================================
//
// 设计说明：
//   - 标准动作（撤销/重做/复制/粘贴/剪切/全选）由 QTextEdit 内置处理
//   - 自定义动作的快捷键仅用于菜单显示（setShortcutVisibleInContextMenu），
//     实际触发由 ShortcutFilter 全局处理，避免双重响应
//   - 新增动作通过信号通知 Widget 层处理（保持视图/逻辑分离）
//
void MyTextEdit::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu* menu = createStandardContextMenu();

    // --- 分隔符 ---
    menu->addSeparator();

    // ===== 格式化文档 Ctrl+Shift+I =====
    QAction* fmtAct = menu->addAction(tr("格式化文档"));
    fmtAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    fmtAct->setShortcutVisibleInContextMenu(true);
    connect(fmtAct, &QAction::triggered, this, &MyTextEdit::formatDocumentRequested);

    // ===== 生成 Doxygen 注释 Ctrl+Shift+D =====
    QAction* doxAct = menu->addAction(tr("生成注释"));
    doxAct->setToolTip(tr("生成 Doxygen / docstring 注释 (Ctrl+Shift+D)"));
    doxAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    doxAct->setShortcutVisibleInContextMenu(true);
    connect(doxAct, &QAction::triggered, this, &MyTextEdit::insertDoxygenComment);

    // ===== 切换行注释 Ctrl+/ =====
    QAction* commentAct = menu->addAction(tr("切换行注释"));
    commentAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Slash));
    commentAct->setShortcutVisibleInContextMenu(true);
    connect(commentAct, &QAction::triggered, this, &MyTextEdit::toggleLineCommentRequested);

    menu->addSeparator();

    // ===== 查找 Ctrl+F =====
    QAction* findAct = menu->addAction(tr("查找"));
    findAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    findAct->setShortcutVisibleInContextMenu(true);
    connect(findAct, &QAction::triggered, this, &MyTextEdit::findRequested);

    // ===== 替换 Ctrl+H =====
    QAction* replaceAct = menu->addAction(tr("替换"));
    replaceAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    replaceAct->setShortcutVisibleInContextMenu(true);
    connect(replaceAct, &QAction::triggered, this, &MyTextEdit::replaceRequested);

    menu->addSeparator();

    // ===== 转换为大写 =====
    QAction* upperAct = menu->addAction(tr("转换为大写"));
    connect(upperAct, &QAction::triggered, this, &MyTextEdit::toUpperCaseRequested);

    // ===== 转换为小写 =====
    QAction* lowerAct = menu->addAction(tr("转换为小写"));
    connect(lowerAct, &QAction::triggered, this, &MyTextEdit::toLowerCaseRequested);

    menu->addSeparator();

    // ===== 复制文件路径 Ctrl+Shift+C =====
    QAction* copyPathAct = menu->addAction(tr("复制文件路径"));
    copyPathAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copyPathAct->setShortcutVisibleInContextMenu(true);
    connect(copyPathAct, &QAction::triggered, this, &MyTextEdit::copyFilePathRequested);

    // ===== 在文件管理器中打开 =====
    QAction* openFolderAct = menu->addAction(tr("在文件管理器中打开"));
    connect(openFolderAct, &QAction::triggered, this, &MyTextEdit::openInFolderRequested);

    menu->exec(e->globalPos());
    delete menu;
}

// ====================================================================
// Doxygen 注释生成
// ====================================================================

QString MyTextEdit::detectFunctionSignature() const
{
    QTextBlock block = textCursor().block();
    // 向上搜索最多 60 行，找最近的函数定义
    for (int i = 0; i < 60 && block.isValid(); ++i) {
        const QString line = block.text().trimmed();
        if (line.isEmpty()) { block = block.previous(); continue; }

        // Python: def funcName(params):
        static const QRegularExpression pyRe(
            QStringLiteral(R"(^\s*def\s+(\w+)\s*\(\s*([^)]*)\s*\))"));
        QRegularExpressionMatch m = pyRe.match(line);
        if (m.hasMatch()) {
            return QStringLiteral("py|") + m.captured(1) + QStringLiteral("|") + m.captured(2);
        }

        // C/C++ 函数: type name(params)
        static const QRegularExpression cppRe(
            QStringLiteral(R"(^\s*(?:[\w:*&<>,\s]+?)\s+(\w+)\s*\(\s*([^)]*)\s*\))"));
        m = cppRe.match(line);
        if (m.hasMatch()) {
            const QString name = m.captured(1);
            // 排除常见控制流关键字
            static const QStringList kw = { QStringLiteral("if"),QStringLiteral("else"),
                QStringLiteral("for"),QStringLiteral("while"),QStringLiteral("switch"),
                QStringLiteral("return"),QStringLiteral("catch"),QStringLiteral("do") };
            if (!kw.contains(name))
                return QStringLiteral("cpp|") + name + QStringLiteral("|") + m.captured(2);
        }

        block = block.previous();
    }
    return QString();
}

void MyTextEdit::insertDoxygenComment()
{
    const QString sig = detectFunctionSignature();
    if (sig.isEmpty()) return;

    const QStringList parts = sig.split('|');
    if (parts.size() < 3) return;
    const QString& lang = parts[0];
    const QString& funcName = parts[1];
    const QString  rawParams = parts[2];

    // 提取当前函数定义行的缩进
    QString indent;
    QTextBlock block = textCursor().block();
    for (int i = 0; i < 60 && block.isValid(); ++i) {
        const QString line = block.text();
        if (!line.trimmed().isEmpty()) {
            static const QRegularExpression indentRe(QStringLiteral(R"(^(\s*))"));
            auto m = indentRe.match(line);
            if (m.hasMatch()) indent = m.captured(1);
            break;
        }
        block = block.previous();
    }

    // 解析参数列表 (简单逗号分割，不处理嵌套模板)
    QStringList paramList;
    if (!rawParams.isEmpty()) {
        const QStringList rawList = rawParams.split(',');
        for (const auto& p : rawList) {
            QString trimmed = p.trimmed();
            if (trimmed.isEmpty()) continue;
            // 提取参数名 (C++ 去类型前缀, Python 去默认值)
            static const QRegularExpression nameRe(QStringLiteral(R"(\b(\w+)\s*$)"));
            auto nameM = nameRe.match(trimmed);
            if (nameM.hasMatch()) {
                QString n = nameM.captured(1);
                if (n != QStringLiteral("self") && n != QStringLiteral("cls"))
                    paramList.append(n);
            }
        }
    }

    QString comment;
    if (lang == QStringLiteral("py")) {
        // Python docstring (Google Style)
        comment += indent + QStringLiteral("\"\"\"\n");
        comment += indent + QStringLiteral("\n");
        if (!paramList.isEmpty()) {
            comment += indent + QStringLiteral("Args:\n");
            for (const auto& p : paramList)
                comment += indent + QStringLiteral("    ") + p + QStringLiteral(": \n");
        }
        comment += indent + QStringLiteral("Returns:\n");
        comment += indent + QStringLiteral("    \n");
        comment += indent + QStringLiteral("\"\"\"");
    } else {
        // C/C++ Doxygen
        comment += indent + QStringLiteral("/**\n");
        comment += indent + QStringLiteral(" * @brief \n");
        for (const auto& p : paramList)
            comment += indent + QStringLiteral(" * @param ") + p + QStringLiteral(" \n");
        // 非 void 函数且非构造/析构 → 加 @return
        if (!funcName.startsWith('~'))
            comment += indent + QStringLiteral(" * @return \n");
        comment += indent + QStringLiteral(" */");
    }

    // 定位到函数定义行开头，上方插入注释
    QTextBlock funcBlock = textCursor().block();
    for (int i = 0; i < 60 && funcBlock.isValid(); ++i) {
        if (funcBlock.text().contains(funcName))
            break;
        funcBlock = funcBlock.previous();
    }

    QTextCursor insertCur(funcBlock);
    insertCur.movePosition(QTextCursor::StartOfBlock);
    insertCur.insertText(comment + QStringLiteral("\n"));
}

// ====================================================================
// T17: 多光标编辑实现
// ====================================================================

void MyTextEdit::updateSecondaryCursorDisplay()
{
    m_secondarySelections.clear();

    // 次级光标高亮颜色（蓝色半透明背景，模拟竖线光标效果）
    QColor bgColor(100, 150, 255, 120);

    for (const auto& cursor : m_secondaryCursors) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(bgColor);
        int pos = cursor.position();
        // 高亮光标位置的字符（竖线效果）
        if (pos < document()->characterCount() - 1) {
            sel.cursor = QTextCursor(document());
            sel.cursor.setPosition(pos);
            sel.cursor.setPosition(pos + 1, QTextCursor::KeepAnchor);
        } else if (pos > 0) {
            // 光标在文档末尾，高亮前一个字符
            sel.cursor = QTextCursor(document());
            sel.cursor.setPosition(pos - 1);
            sel.cursor.setPosition(pos, QTextCursor::KeepAnchor);
        }
        m_secondarySelections.append(sel);
    }

    // 触发重绘（paintEvent 会合并所有 ExtraSelections）
    viewport()->update();
}

void MyTextEdit::applyToAllCursors(QChar ch)
{
    QTextCursor mainCursor = textCursor();

    // 合并到一个编辑操作中（便于撤销）
    mainCursor.beginEditBlock();

    // 主光标插入
    mainCursor.insertText(QString(ch));

    // 次级光标插入（按位置从后往前，避免位置偏移）
    for (int i = m_secondaryCursors.size() - 1; i >= 0; --i) {
        m_secondaryCursors[i].insertText(QString(ch));
    }

    mainCursor.endEditBlock();

    // 更新主光标
    setTextCursor(mainCursor);

    // 更新次级光标显示
    updateSecondaryCursorDisplay();

    // 触发补全和括号匹配更新
    m_completionTimer.start();
    highlightMatchingBracket();
}

void MyTextEdit::backspaceAllCursors()
{
    QTextCursor mainCursor = textCursor();

    // 收集所有光标，按位置从后往前排序（避免位置偏移）
    QList<QTextCursor> allCursors;
    allCursors.append(mainCursor);
    allCursors.append(m_secondaryCursors);

    std::sort(allCursors.begin(), allCursors.end(),
        [](const QTextCursor& a, const QTextCursor& b) {
            return a.position() > b.position();
        });

    mainCursor.beginEditBlock();

    for (auto& c : allCursors) {
        if (c.position() > 0) {
            c.deletePreviousChar();
        }
    }

    mainCursor.endEditBlock();

    setTextCursor(mainCursor);

    updateSecondaryCursorDisplay();
    m_completionTimer.start();
    highlightMatchingBracket();
}

void MyTextEdit::deleteAllCursors()
{
    QTextCursor mainCursor = textCursor();

    // 收集所有光标，按位置从后往前排序（避免位置偏移）
    QList<QTextCursor> allCursors;
    allCursors.append(mainCursor);
    allCursors.append(m_secondaryCursors);

    std::sort(allCursors.begin(), allCursors.end(),
        [](const QTextCursor& a, const QTextCursor& b) {
            return a.position() > b.position();
        });

    mainCursor.beginEditBlock();

    for (auto& c : allCursors) {
        c.deleteChar();
    }

    mainCursor.endEditBlock();

    setTextCursor(mainCursor);

    updateSecondaryCursorDisplay();
    m_completionTimer.start();
    highlightMatchingBracket();
}

void MyTextEdit::moveAllCursors(QTextCursor::MoveOperation op, QTextCursor::MoveMode mode)
{
    // 移动主光标
    QTextCursor mainCursor = textCursor();
    mainCursor.movePosition(op, mode);
    setTextCursor(mainCursor);

    // 移动次级光标
    for (auto& c : m_secondaryCursors) {
        c.movePosition(op, mode);
    }

    updateSecondaryCursorDisplay();
    highlightMatchingBracket();

    // 发出光标位置变化信号
    emit cursorPositionChangedSignal();
}

void MyTextEdit::clearSecondaryCursors()
{
    m_secondaryCursors.clear();
    m_secondarySelections.clear();
    m_multiCursorMode = false;
    viewport()->update();
}
