#include "ui/editor/MyTextEdit.h"
#include "ui/editor/TextCompleter.h"       // 具体类仅在cpp中使用（用于isChineseChar等静态方法）
#include "ui/editor/LineNumberArea.h"
#include "core/config/ThemeManager.h"
#include "core/config/ConfigManager.h"
#include "core/editor/CodeSyntaxHighlighter.h"
#include "core/editor/DoxygenGenerator.h"
#include "core/editor/CodeFoldingManager.h"
#include "core/editor/MinimapRenderer.h"
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
#include <climits>  // C02-5: INT_MAX（增量渲染可视行范围上限）

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
        if (m_minimapRenderer && m_minimapRenderer->isVisible()) m_minimapRenderer->scheduleUpdate();
    });

    // 配置补全延迟定时器：单次触发，间隔100ms（避免输入时频繁更新补全列表）
    m_completionTimer.setSingleShot(true);
    m_completionTimer.setInterval(100);

    connect(&m_completionTimer, &QTimer::timeout, this, &MyTextEdit::updateCompletion);
    // 新增逻辑 连接文本变化信号到自定义处理器 (中间层 用于控制更新逻辑)
    connect(this, &QTextEdit::textChanged, this, &MyTextEdit::handleTextChanged);
    connect(this, &QTextEdit::cursorPositionChanged, this, &MyTextEdit::cursorPositionChangedInternal);

    // ========== 迷你地图渲染器初始化 (M7) ==========
    // MinimapRenderer 构造时创建子控件并 installEventFilter，
    // 鼠标点击/绘制事件由 MinimapRenderer::eventFilter 自身拦截处理
    m_minimapRenderer = new MinimapRenderer(this, this);

    // 加载缩进配置（tabSize / indentStyle）
    loadIndentConfig();

    // ========== 代码折叠管理器初始化 ==========
    m_foldingManager = new CodeFoldingManager(this, this);
    // 注入回调解耦：findMatchingBracket 保留在 MyTextEdit（括号匹配也使用）
    m_foldingManager->setFindMatchingBracketCallback(
        [this](int pos) { return findMatchingBracket(pos); });
    // 注入刷新回调：折叠状态变更后需刷新行号区
    m_foldingManager->setRequestUpdateCallback(
        [this]() { updateLineNumberArea(); });

    // 监听配置变更（设置页修改 tabSize/indentStyle 时实时生效）
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, [this](const QString& key) {
        if (key == QStringLiteral("Editor/tabSize") ||
            key == QStringLiteral("Editor/indentStyle")) {
            loadIndentConfig();
        }
        // P3-M03 子项5: 拼写检查开关变更时同步 SpellChecker 并重新检查
        if (key == QStringLiteral("Editor/spellCheck")) {
            SpellChecker::instance().setEnabled(
                ConfigManager::instance().spellCheckEnabled());
            m_spellErrors.clear();
            if (SpellChecker::instance().enabled()) {
                m_spellCheckTimer.start();
            } else {
                viewport()->update();
            }
        }
    });

    // ========== P3-M03 子项5: 拼写检查防抖定时器 ==========
    m_spellCheckTimer.setSingleShot(true);
    m_spellCheckTimer.setInterval(500);
    connect(&m_spellCheckTimer, &QTimer::timeout, this, &MyTextEdit::performSpellCheck);
    // 文本变更时启动防抖定时器（文件加载/编辑均会触发，500ms 后批量检查）
    connect(this, &QTextEdit::textChanged, this, [this]() {
        if (SpellChecker::instance().enabled()) {
            m_spellCheckTimer.start();
        }
    });

    // ========== L16/H1: 鼠标悬停 LSP hover 初始化 ==========
    // H1: 防抖延时 300ms→200ms（用户要求 150~200ms 停留才触发）
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(200);
    connect(&m_hoverTimer, &QTimer::timeout, this, [this]() {
        // 定时器触发时，获取鼠标位置对应的文本光标位置
        QTextCursor cursor = cursorForPosition(m_lastHoverPos);
        if (cursor.isNull()) return;
        int line = cursor.blockNumber();
        int col = cursor.columnNumber();
        // 记录全局坐标（用于弹窗定位）和请求序号（用于 stale 检测）
        m_lastHoverGlobalPos = mapToGlobal(m_lastHoverPos + QPoint(15, 20));
        m_hoverSeq++;
        emit lspHoverRequested(line, col);
    });
    setMouseTracking(true);  // 启用鼠标追踪，即使不按键也能收到 mouseMoveEvent
}

// J1: 静默设置文本 — 抑制 textChanged 引发的补全弹窗触发
// 用于打开文件、跳转定义等程序化文本加载场景，避免编辑器卡顿
void MyTextEdit::setPlainTextSilently(const QString& text)
{
    m_suppressCompletion = true;
    setPlainText(text);
    // 停止可能由 textChanged 启动的补全定时器
    m_completionTimer.stop();
    // 隐藏可能已弹出的补全框
    if (m_completer) {
        m_completer->hideCompletion();
    }
    m_suppressCompletion = false;
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

    // J1: 程序化文本加载（打开文件/跳转定义）时抑制补全弹窗，避免编辑器卡顿
    if (m_suppressCompletion) {
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

    // 触发迷你地图延迟更新 (M7) — 委托给 MinimapRenderer
    if (m_minimapRenderer) m_minimapRenderer->scheduleUpdate();

    // 代码折叠：文本变更时重新扫描折叠区域（防抖，避免频繁扫描）
    // 仅在补全器初始化后才扫描（避免 setPlainText 时触发）
    static QTimer* foldScanTimer = nullptr;
    if (!foldScanTimer) {
        foldScanTimer = new QTimer(this);
        foldScanTimer->setSingleShot(true);
        foldScanTimer->setInterval(500);
        connect(foldScanTimer, &QTimer::timeout, this, [this]() {
            if (m_foldingManager) {
                m_foldingManager->scanFoldRegions();
                updateLineNumberArea();
            }
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
        // H3: 成员补全模式下跳过最小前缀检查，允许 1 字符前缀过滤 LSP 候选
        if (m_completer->isMemberCompletionMode() || context.first.length() >= 2) {
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

void MyTextEdit::setLspHighlightState(LspHighlightState state)
{
    if (m_syntaxHighlighter) {
        m_syntaxHighlighter->setLspState(state);
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
    // P0 C02: 正则复用 — 编译一次多次执行，避免每次 updateWordList 重新编译正则
    static const QRegularExpression wordRegex(
        QStringLiteral(R"(([\w\p{Han}]+))")  // 匹配单词字符和汉字
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
    // 失焦时中止悬停（切窗口/切标签页等）
    m_hoverTimer.stop();
    emit hoverAborted();
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

    // 注：minimap widget 的事件由 MinimapRenderer::eventFilter 自身拦截处理
    // （构造时已 m_minimapWidget->installEventFilter(m_minimapRenderer)）
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
    // F1: 滚动时中止悬停预览（内容位置变化，旧悬停位置失效）
    m_hoverTimer.stop();
    emit hoverAborted();
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

// P3-M03 子项4: .editorconfig 按文件覆盖缩进配置（不写入全局 ConfigManager）
void MyTextEdit::setIndentConfig(int tabSize, bool useSpaces)
{
    if (tabSize > 0) m_tabSize = tabSize;
    m_useSpaces = useSpaces;
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

    // 按键时中止悬停预览（非修饰键才触发，避免 Ctrl/Shift 等误触）
    if (key != Qt::Key_Control && key != Qt::Key_Shift &&
        key != Qt::Key_Alt && key != Qt::Key_Meta) {
        m_hoverTimer.stop();
        emit hoverAborted();
    }

    // ====== T17: 多光标编辑模式 ======
    if (m_multiCursorMode && !m_secondaryCursors.isEmpty()) {
        // Esc: 清除所有次级光标
        if (key == Qt::Key_Escape) {
            // P3-M03 子项3: 列选模式下 Esc 同时退出列选模式
            if (m_columnSelectionMode) {
                m_columnSelectionMode = false;
                m_columnDragging = false;
            }
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

    // H3: 检测成员访问符 . / -> / :: 并自动触发 LSP 成员补全
    // 输入 . 或 -> 或 :: 后，立即请求 LSP 补全并进入成员补全模式
    // 成员补全模式下跳过最小前缀检查，允许空前缀显示所有成员
    if (m_completer && event->text().size() == 1) {
        QChar typedChar = event->text().at(0);
        QTextCursor cursor = textCursor();
        int pos = cursor.position();
        QString docText = toPlainText();

        bool triggerMember = false;
        if (typedChar == '.') {
            // . 成员访问（排除数字小数点：前一个字符是数字时不触发）
            if (pos >= 2) {
                QChar prevChar = docText[pos - 2];
                if (!prevChar.isDigit()) {
                    triggerMember = true;
                }
            } else {
                triggerMember = true;
            }
        } else if (typedChar == '>' && pos >= 2 && docText[pos - 2] == '-') {
            // -> 指针成员访问
            triggerMember = true;
        } else if (typedChar == ':' && pos >= 2 && docText[pos - 2] == ':') {
            // :: 作用域解析运算符
            triggerMember = true;
        }

        if (triggerMember) {
            m_completer->triggerMemberCompletion();
            requestLspCompletion();
            // 不启动普通补全定时器，避免与成员补全冲突
            highlightMatchingBracket();
            return;
        }

        // 输入非单词字符（空格/分号/括号等）时退出成员补全模式
        if (m_completer->isMemberCompletionMode() &&
            !typedChar.isLetterOrNumber() && typedChar != '_') {
            m_completer->clearMemberCompletion();
        }
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

    // 编辑器边界：左侧给行号留空间，右侧给迷你地图留空间（委托给 MinimapRenderer）
    int rightMargin = m_minimapRenderer ? m_minimapRenderer->width() : 0;
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

    // P0 C02-2: 行号栏渲染缓存 — 滚动位置/宽度/文档版本/折叠状态未变时复用 QImage
    // 避免水平滚动、光标移动（不引起垂直滚动）等场景下重复渲染行号
    const int scrollVal = verticalScrollBar()->value();
    const int areaWidth = areaWidget->width();
    const int docRev = document()->revision();
    // 折叠状态签名：用折叠区域数量作为简单签名（折叠/展开操作会改变数量）
    const int foldSig = m_foldingManager ? m_foldingManager->foldedBlockCount() : 0;
    // P3-M04 子项3: 断点签名（用断点数量 + 总和作为简单签名）
    const int bpSig = m_breakpointLines.size();

    if (m_lnCacheScroll == scrollVal && m_lnCacheWidth == areaWidth &&
        m_lnCacheDocRev == docRev && m_lnCacheFoldSig == foldSig &&
        m_lnCacheBpSig == bpSig &&
        !m_lineNumberCache.isNull()) {
        // 缓存命中：直接将缓存 QImage 绘制到 widget
        QPainter cachePainter(areaWidget);
        cachePainter.drawImage(event->rect().topLeft(), m_lineNumberCache, event->rect());
        return;
    }

    // 缓存未命中 → 重新渲染到 QImage，再输出到 widget
    // 使用 Format_ARGB32_Premultiplied（Qt 推荐的快速渲染格式）
    QImage cache(areaWidth, areaWidget->height(), QImage::Format_ARGB32_Premultiplied);
    cache.fill(Qt::transparent);
    QPainter painter(&cache);

    // 使用主题色板中的侧边栏背景色
    const auto& palette = ThemeManager::instance().currentPalette();
    painter.fillRect(cache.rect(), palette.bgSideBar);

    // P0 C02: 性能优化 — 只遍历可见行，避免大文件（10k+行）时遍历全文
    // 使用 cursorForPosition 定位第一个可见块，O(可见行数) 而非 O(总行数)
    QTextCursor cursor = cursorForPosition(QPoint(0, 0));
    QTextBlock block = cursor.block();
    int blockNumber = block.blockNumber();

    // 获取文档布局
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    int verticalScrollValue = verticalScrollBar()->value();
    QRect visibleRect = viewport()->rect();

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

    // P2-H03 子项3: Git blame 颜色调色板（6 色）
    // 同一提交 hash 哈希到固定颜色，连续同提交行同色，不同提交切换颜色
    static const QColor kBlamePalette[] = {
        QColor( 78, 201, 176),  // 绿
        QColor( 86, 156, 214),  // 蓝
        QColor(244,  71,  71),  // 红
        QColor(220, 220, 170),  // 黄
        QColor(197, 134, 192),  // 紫
        QColor(156, 220, 254)   // 青
    };
    const int kBlameBarWidth = 4;  // 颜色条宽度（像素）

    // 遍历可见文本块（从第一个可见块开始，到可见区域底部结束）
    while (block.isValid()) {
        QRectF blockRect = layout->blockBoundingRect(block);
        qreal top = blockRect.top() - verticalScrollValue;
        qreal bottom = top + blockRect.height();

        // 超出可见区域底部 → 停止遍历
        if (top > visibleRect.bottom()) break;

        // 检查块是否在可见区域内
        if (bottom >= visibleRect.top() && top <= visibleRect.bottom()) {
            QString number = QString::number(blockNumber + 1);
            painter.drawText(0, static_cast<int>(top), areaWidget->width(), fontMetrics().height(),
                            Qt::AlignCenter, number);

            // P3-M04 子项3: 绘制断点红圆点（行号栏左边缘 8px 区域）
            if (m_breakpointLines.contains(blockNumber + 1)) {
                // 圆点直径取行高的 60%，与行垂直居中
                int dotSize = qMax(6, static_cast<int>(fontMetrics().height() * 0.6));
                int dotX = 4;  // 左侧留 4px 边距
                int dotY = static_cast<int>(top + (blockRect.height() - dotSize) / 2.0);
                painter.setBrush(QColor(220, 60, 60));   // 红色填充
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(dotX, dotY, dotSize, dotSize);
                painter.setBrush(Qt::NoBrush);            // 恢复
                painter.setPen(palette.fgLineNumber);     // 恢复行号色
            }

            // P2-H03 子项3: 绘制 Git blame 颜色条（行号右侧 4px）
            if (m_gitBlameVisible && !m_gitBlameInfo.isEmpty()) {
                auto it = m_gitBlameInfo.constFind(blockNumber + 1);
                if (it != m_gitBlameInfo.constEnd() && !it->commitHash.isEmpty()) {
                    // 按提交 hash 哈希到 6 色调色板
                    size_t h = qHash(it->commitHash);
                    QColor barColor = kBlamePalette[h % 6];
                    int barX = areaWidget->width() - kBlameBarWidth;
                    QRectF barRect(barX, top, kBlameBarWidth, blockRect.height());
                    painter.fillRect(barRect, barColor);
                }
            }
        }

        // 绘制折叠图标（委托给 CodeFoldingManager）
        if (m_foldingManager) {
            int iconX = areaWidget->width() - m_foldingManager->foldIconSize() - 2;
            m_foldingManager->paintFoldIcon(painter, block.blockNumber(),
                                            iconX, static_cast<int>(top),
                                            fontMetrics().height(), editorSize);
        }

        // 移动到下一个文本块
        block = block.next();
        blockNumber++;
    }

    // 保存缓存并更新缓存 key
    m_lineNumberCache = cache;
    m_lnCacheScroll = scrollVal;
    m_lnCacheWidth = areaWidth;
    m_lnCacheDocRev = docRev;
    m_lnCacheFoldSig = foldSig;
    m_lnCacheBpSig = bpSig;

    // 将缓存 QImage 输出到 widget
    QPainter widgetPainter(areaWidget);
    widgetPainter.drawImage(event->rect().topLeft(), cache, event->rect());
}

// ========== 代码折叠实现 ==========

// ========== 代码折叠（委托给 CodeFoldingManager） ==========

void MyTextEdit::toggleFold(int blockNumber)
{
    if (m_foldingManager) m_foldingManager->toggleFold(blockNumber);
}

bool MyTextEdit::isFoldable(int blockNumber) const
{
    return m_foldingManager ? m_foldingManager->isFoldable(blockNumber) : false;
}

bool MyTextEdit::isFolded(int blockNumber) const
{
    return m_foldingManager ? m_foldingManager->isFolded(blockNumber) : false;
}

void MyTextEdit::lineNumberAreaClicked(const QPoint& pos, int areaWidth)
{
    // P3-M04 子项3: 优先处理断点切换（点击行号栏左半空白区域）
    // 折叠图标绘制在行号区右侧（iconX = areaWidth - foldIconSize - 2），
    // 断点红圆点绘制在行号区左侧（约 8~14 像素区域），互不冲突。
    // 这里：点击行号栏前半区域（x < areaWidth/2）触发断点切换；
    //       点击后半区域（含折叠图标）保持原折叠逻辑
    if (pos.x() < areaWidth / 2) {
        QTextCursor cursor = cursorForPosition(QPoint(0, pos.y()));
        if (!cursor.isNull()) {
            int line = cursor.blockNumber() + 1;  // 1-based
            toggleBreakpoint(line);
            return;  // 不再触发折叠
        }
    }

    if (!m_foldingManager) return;
    QTextCursor cursor = cursorForPosition(QPoint(0, pos.y()));
    m_foldingManager->onLineNumberAreaClicked(pos, areaWidth, cursor);
}

// ============================================================
// P3-M04 子项3: 断点切换实现
// ============================================================

void MyTextEdit::toggleBreakpoint(int line)
{
    if (line < 1) return;

    bool enabled;
    if (m_breakpointLines.contains(line)) {
        m_breakpointLines.remove(line);
        enabled = false;
    } else {
        m_breakpointLines.insert(line);
        enabled = true;
    }

    // 断点变化时失效行号栏缓存
    m_lnCacheBpSig = -1;
    updateLineNumberArea();

    emit breakpointToggled(line, enabled);
}

void MyTextEdit::setBreakpoints(const QSet<int>& lines)
{
    m_breakpointLines = lines;
    m_lnCacheBpSig = -1;
    updateLineNumberArea();
}

void MyTextEdit::clearBreakpoints()
{
    if (m_breakpointLines.isEmpty()) return;
    m_breakpointLines.clear();
    m_lnCacheBpSig = -1;
    updateLineNumberArea();
}

/// @brief 窗口大小改变事件
/// @param event 大小改变对象
void MyTextEdit::resizeEvent(QResizeEvent* event)
{
    QTextEdit::resizeEvent(event);
    updateLineNumberArea();

    // P0 C02-2: resize 时行号栏尺寸变化，失效渲染缓存
    m_lnCacheWidth = -1;

    // 更新迷你地图位置和大小 (M7) — 委托给 MinimapRenderer
    if (m_minimapRenderer) {
        m_minimapRenderer->handleResize(width(), height());
    }
}

void MyTextEdit::paintEvent(QPaintEvent *event)
{
    // C02-5: 编辑器增量渲染 — 在调用基类 paintEvent 前记录脏区域和滚动位置
    const QRect paintRect = event->rect();
    const int  scrollY    = verticalScrollBar() ? verticalScrollBar()->value() : 0;

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

        // C02-5: 增量渲染 — 仅遍历行号在 event->rect() 覆盖范围内的诊断
        // 用 cursorForPosition 获取可视区域起止行号（0-based blockNumber）
        // 避免大文件全量遍历所有诊断（性能优化）
        int visibleStartLine = 0;
        int visibleEndLine   = INT_MAX;
        // cursorForPosition 在 rect 边界外可能返回空光标，需做有效性校验
        QTextCursor topCursor = cursorForPosition(QPoint(0, paintRect.top()));
        if (!topCursor.isNull()) {
            visibleStartLine = topCursor.blockNumber();
        }
        // bottom() 可能位于最后一行之下，cursorForPosition 仍会返回最后一行的光标
        QTextCursor bottomCursor = cursorForPosition(QPoint(0, paintRect.bottom()));
        if (!bottomCursor.isNull()) {
            visibleEndLine = bottomCursor.blockNumber();
        }

        for (const auto& diag : m_diagnostics) {
            // 增量渲染裁剪：诊断起始行不在可视范围内则跳过
            // diag.startLine 为 0-based，与 blockNumber() 一致
            if (diag.startLine < visibleStartLine || diag.startLine > visibleEndLine) {
                continue;
            }

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

    // ========== P3-M03 子项5: 拼写错误波浪线绘制 ==========
    // 红色波浪下划线，复用诊断波浪线算法，按可视行范围裁剪
    if (!m_spellErrors.isEmpty()) {
        QPainter painter(viewport());
        // 复用上方诊断块计算的可见行范围（visibleStartLine/visibleEndLine）
        // 重新计算一次以避免变量作用域问题
        int visStartLine = 0;
        int visEndLine = INT_MAX;
        QTextCursor topC = cursorForPosition(QPoint(0, paintRect.top()));
        if (!topC.isNull()) visStartLine = topC.blockNumber();
        QTextCursor botC = cursorForPosition(QPoint(0, paintRect.bottom()));
        if (!botC.isNull()) visEndLine = botC.blockNumber();

        QColor spellColor(255, 0, 0);  // 红色波浪线
        painter.setPen(QPen(spellColor, 1, Qt::SolidLine));

        for (const auto& err : m_spellErrors) {
            // 边界保护：文档可能已变更导致区间失效（防抖窗口内位置过期）
            if (err.start < 0 || err.start + err.length > document()->characterCount()) continue;

            // 通过起始位置定位行号，判断是否在可视范围内
            QTextCursor c(document());
            c.setPosition(err.start);
            int errLine = c.blockNumber();
            if (errLine < visStartLine || errLine > visEndLine) continue;

            // 计算单词起止像素坐标
            QTextCursor endC(document());
            endC.setPosition(err.start + err.length);

            QRect startRect = cursorRect(c);
            QRect endRect = cursorRect(endC);
            int startX = startRect.left();
            int endX = endRect.left();
            int startY = startRect.bottom();

            if (startY <= 0 || endX <= startX) continue;
            // 绘制波浪线（与诊断相同算法）
            int yy = startY;
            for (int x = startX; x < endX; x += 6) {
                painter.drawLine(x, yy, x + 3, yy + 2);
                painter.drawLine(x + 3, yy + 2, x + 6, yy);
            }
        }
    }

    // C02-5: 记录本次绘制的脏区域和滚动位置，供后续增量渲染参考
    m_lastPaintRect    = paintRect;
    m_lastPaintScrollY = scrollY;
}

/// @brief 鼠标移动事件：H1 重写 hover 触发策略
/// H1 核心修复：
///   1. 鼠标移动时立即取消 pending 的 LSP hover 请求（发射 hoverAborted）
///   2. 鼠标移动时立即隐藏已显示的弹窗（hoverAborted → hideImmediately）
///   3. 重启 200ms 防抖定时器，仅当鼠标静止停留 200ms 才触发新请求
/// 效果：快速划过/滑动不触发预览，杜绝弹窗闪现、刷新、卡顿
void MyTextEdit::mouseMoveEvent(QMouseEvent* event)
{
    // P3-M03 子项3: 列选拖拽中 → 实时重建列选光标
    if (m_columnDragging && m_columnSelectionMode) {
        QTextCursor startCursor = cursorForPosition(m_columnSelectStart);
        QTextCursor endCursor = cursorForPosition(event->pos());
        if (!startCursor.isNull() && !endCursor.isNull()) {
            rebuildColumnCursors(startCursor.blockNumber(), m_columnSelectStartCol,
                                 endCursor.blockNumber(), endCursor.columnNumber());
        }
        event->accept();
        return;
    }

    if (m_lastHoverPos != event->pos()) {
        m_lastHoverPos = event->pos();
        // H1: 鼠标移动 → 立即取消 pending hover 请求 + 隐藏弹窗
        // 避免鼠标滑动时弹窗频繁闪现、刷新（旧请求未取消导致视觉割裂）
        m_hoverTimer.stop();
        emit hoverAborted();
        // 重启防抖定时器：仅当鼠标静止停留 200ms 才重新触发
        m_hoverTimer.start();
    }

    // Bug1/Bug2: Ctrl 按住时显示手型光标（VSCode 风格，提示可 Ctrl+Click 跳转）
    if (event->modifiers() & Qt::ControlModifier) {
        viewport()->setCursor(Qt::PointingHandCursor);
    } else {
        viewport()->unsetCursor();
    }

    QTextEdit::mouseMoveEvent(event);
}

void MyTextEdit::leaveEvent(QEvent* event)
{
    // 鼠标离开编辑器 → 停止悬停定时器，中止悬停请求
    m_hoverTimer.stop();
    emit hoverAborted();
    QTextEdit::leaveEvent(event);
}

void MyTextEdit::mouseReleaseEvent(QMouseEvent* event)
{
    // P3-M03 子项3: 列选拖拽结束 — 仅停止拖拽标志，保留列选光标供后续编辑
    if (m_columnDragging) {
        m_columnDragging = false;
        event->accept();
        return;
    }
    QTextEdit::mouseReleaseEvent(event);
}

void MyTextEdit::mousePressEvent(QMouseEvent* event)
{
    // P3-M03 子项3: Shift+Alt+左键拖拽 → 进入列选择模式
    // 必须在 C03-6 (Ctrl+Alt) 和 T17 (Alt+Click) 分支之前匹配
    if ((event->modifiers() & Qt::ShiftModifier) &&
        (event->modifiers() & Qt::AltModifier) &&
        event->button() == Qt::LeftButton) {
        QTextCursor clickCursor = cursorForPosition(event->pos());
        if (!clickCursor.isNull()) {
            m_columnSelectionMode = true;
            m_columnDragging = true;
            m_columnSelectStart = event->pos();
            m_columnSelectStartCol = clickCursor.columnNumber();
            // 初始：起点即终点（单点列选，先建立主光标）
            rebuildColumnCursors(clickCursor.blockNumber(),
                                 clickCursor.columnNumber(),
                                 clickCursor.blockNumber(),
                                 clickCursor.columnNumber());
        }
        event->accept();
        return;
    }

    // C03-6: Ctrl+Alt+左键单击 → 请求定义预览（不跳转，悬浮显示）
    // 必须在 T17 Alt+Click 分支之前匹配：Ctrl+Alt 同时按下时 Qt::AltModifier 也为真
    // 不修改 T17 的纯 Alt+Click 行为，仅在新增 Ctrl 修饰时改走预览路径
    if ((event->modifiers() & Qt::AltModifier) &&
        (event->modifiers() & Qt::ControlModifier) &&
        event->button() == Qt::LeftButton) {
        QTextCursor clickCursor = cursorForPosition(event->pos());
        if (!clickCursor.isNull()) {
            setTextCursor(clickCursor);  // 先定位光标，供 Widget 读取行/列
            emit definitionPreviewRequested(clickCursor.blockNumber() + 1,
                                            clickCursor.columnNumber() + 1);
            event->accept();
            return;
        }
    }

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
            // Bug1: 优先检测 #include 头文件路径点击 → 打开头文件（而非 LSP 跳转定义）
            QString includeText;
            bool isSystem = false;
            if (extractIncludeAtCursor(clickCursor, includeText, isSystem)) {
                emit includeOpenRequested(includeText, isSystem);
                event->accept();
                return;
            }
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
    // P3-M03 子项3: 任意非列选拖拽的普通点击退出列选模式
    if (m_columnSelectionMode) {
        m_columnSelectionMode = false;
        clearColumnCursors();
    }
    highlightMatchingBracket();
}

// ========== Bug1: #include 头文件路径检测 ==========

bool MyTextEdit::extractIncludeAtCursor(const QTextCursor& cursor, QString& includeText, bool& isSystem)
{
    if (cursor.isNull()) return false;

    QTextBlock block = cursor.block();
    QString lineText = block.text();

    // 匹配 #include 指令行，提取路径部分
    // 支持: #include <header>  /  #include "header"  /  #  include <header>
    static const QRegularExpression includeRe(
        QStringLiteral("^\\s*#\\s*include\\s*([<\"])([^>\"\n]+)[>\"]"));
    QRegularExpressionMatch m = includeRe.match(lineText);
    if (!m.hasMatch()) return false;

    // 检查光标位置是否在路径范围内（含尖括号/引号）
    int pathStart = m.capturedStart(0) + m.capturedStart(1);  // < 或 " 的位置
    int pathEnd = pathStart + 1 + m.capturedLength(2) + 1;    // 含 > 或 "
    int cursorPos = cursor.position() - block.position();
    if (cursorPos < pathStart || cursorPos > pathEnd) return false;

    // 输出原始 include 文本（含定界符）
    QChar delim = m.captured(1).at(0);
    isSystem = (delim == QLatin1Char('<'));
    includeText = delim + m.captured(2) + (isSystem ? QLatin1Char('>') : QLatin1Char('"'));
    return true;
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
    // 修复：避免调用 setPixelSize(-1)（会触发 Qt 警告 "Pixel size <= 0"）。
    // 改为基于 family 构造新字体，仅设置 pointSize，从根源消除 pixelSize 残留。
    QFont oldFont = this->font();
    QFont font(oldFont.family());
    font.setPointSize(size);
    font.setBold(oldFont.bold());
    font.setItalic(oldFont.italic());
    font.setUnderline(oldFont.underline());
    font.setStrikeOut(oldFont.strikeOut());
    font.setFamily(oldFont.family());  // 保留等宽字体族
    if (oldFont.fixedPitch()) font.setFixedPitch(true);
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

// ========== 迷你地图实现 (M7) — 委托给 MinimapRenderer ==========

void MyTextEdit::toggleMinimap(bool visible)
{
    if (m_minimapRenderer) m_minimapRenderer->setVisible(visible);
}

bool MyTextEdit::isMinimapVisible() const
{
    return m_minimapRenderer ? m_minimapRenderer->isVisible() : false;
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

// ========== P2-H03 子项3: Git blame 行级标注 ==========

void MyTextEdit::setGitBlameInfo(const QList<GitBlameLine>& info)
{
    m_gitBlameInfo.clear();
    m_gitBlameInfo.reserve(info.size());
    for (const GitBlameLine& bl : info) {
        m_gitBlameInfo.insert(bl.lineNumber, bl);
    }
    // 失效行号栏缓存，触发重绘
    m_lnCacheDocRev = -1;
    if (lineNumberArea && lineNumberArea->asWidget())
        lineNumberArea->asWidget()->update();
}

void MyTextEdit::clearGitBlameInfo()
{
    m_gitBlameInfo.clear();
    m_lnCacheDocRev = -1;
    if (lineNumberArea && lineNumberArea->asWidget())
        lineNumberArea->asWidget()->update();
}

void MyTextEdit::setGitBlameVisible(bool visible)
{
    if (m_gitBlameVisible == visible) return;
    m_gitBlameVisible = visible;
    // 失效缓存并重绘行号栏
    m_lnCacheDocRev = -1;
    if (lineNumberArea && lineNumberArea->asWidget())
        lineNumberArea->asWidget()->update();
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

    // ===== P3-M03 子项5: 拼写建议（光标位于拼写错误单词上时显示）=====
    if (SpellChecker::instance().enabled()) {
        QTextCursor clickCursor = cursorForPosition(e->pos());
        if (!clickCursor.isNull()) {
            const SpellMisspelledRange* err = spellErrorAt(clickCursor.position());
            if (err) {
                QStringList sugg = SpellChecker::instance().suggestions(err->word, 5);
                if (!sugg.isEmpty()) {
                    for (const QString& s : sugg) {
                        QAction* suggAct = menu->addAction(tr("更正为: %1").arg(s));
                        connect(suggAct, &QAction::triggered, this, [this, err, s]() {
                            QTextCursor c(document());
                            c.setPosition(err->start);
                            c.setPosition(err->start + err->length, QTextCursor::KeepAnchor);
                            c.insertText(s);
                            // 替换后重新检查（防抖定时器会自动触发）
                        });
                    }
                    menu->addSeparator();
                }
                // 添加到用户词典
                QAction* addAct = menu->addAction(tr("将 \"%1\" 添加到词典").arg(err->word));
                connect(addAct, &QAction::triggered, this, [this, err]() {
                    SpellChecker::instance().addToDictionary(err->word);
                    // 立即重新检查（新词已入词典，该错误会消失）
                    performSpellCheck();
                });
                menu->addSeparator();
            }
        }
    }

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

    menu->addSeparator();

    // ===== P2-H01: 在终端运行（选中代码）=====
    QAction* runInTermAct = menu->addAction(tr("在终端运行"));
    runInTermAct->setEnabled(textCursor().hasSelection());
    runInTermAct->setToolTip(tr("将选中的代码发送到终端执行"));
    connect(runInTermAct, &QAction::triggered, this, [this]() {
        QString code = textCursor().selectedText();
        // QTextCursor::selectedText() 返回 U+2029 作为段落分隔符，替换为换行符
        code.replace(QChar(0x2029), QChar('\n'));
        if (!code.isEmpty()) {
            emit runInTerminalRequested(code);
        }
    });

    menu->exec(e->globalPos());
    delete menu;
}

// ====================================================================
// Doxygen 注释生成
// ====================================================================

void MyTextEdit::insertDoxygenComment()
{
    // 委托给 DoxygenGenerator（纯静态工具类，无状态）
    DoxygenGenerator::insertComment(textCursor());
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

// ====================================================================
// P3-M03 子项1: EOL（行尾）配置实现
// ====================================================================

void MyTextEdit::setEolMode(const QString& eol)
{
    QString normalized = eol.toUpper();
    if (normalized != QStringLiteral("LF") &&
        normalized != QStringLiteral("CRLF") &&
        normalized != QStringLiteral("CR")) {
        return;  // 非法值忽略
    }
    if (m_eolMode == normalized) {
        emit eolModeChanged(m_eolMode);
        return;
    }
    m_eolMode = normalized;
    // Qt 文档内部统一使用 '\n' 作为段落分隔符，无需在此处转换文档内容
    // 实际行尾转换在保存时由 FileOperator::convertEol() 完成
    emit eolModeChanged(m_eolMode);
    viewport()->update();
}

// ====================================================================
// P3-M03 子项3: 列选择模式实现
// ====================================================================
//
// 设计说明：
//   - Shift+Alt+左键拖拽进入列选模式
//   - 拖拽范围内每行创建一个 QTextCursor，所有光标选中相同列范围
//   - 列选模式下输入字符同步到所有光标（复用 T17 多光标架构）
//   - Esc / 鼠标单击 / 失焦退出列选模式
//
// 与 T17 多光标的关系：
//   - T17：Alt+Click 添加次级光标（多光标点）
//   - P3-M03：Shift+Alt+拖拽生成列选光标（多光标列块）
//   - 两者复用 m_secondaryCursors/m_secondarySelections 视觉显示
//   - 列选模式下 m_columnCursors 仅记录列选状态，实际编辑通过 m_secondaryCursors 完成

void MyTextEdit::setColumnSelectionMode(bool enabled)
{
    if (m_columnSelectionMode == enabled) return;
    m_columnSelectionMode = enabled;
    if (!enabled) {
        m_columnCursors.clear();
        m_columnDragging = false;
        // 退出列选时同时清除次级光标（避免残留选择高亮）
        clearSecondaryCursors();
    }
    viewport()->update();
}

void MyTextEdit::clearColumnCursors()
{
    m_columnCursors.clear();
    m_columnDragging = false;
    clearSecondaryCursors();
}

int MyTextEdit::columnAtPosition(const QPoint& pos) const
{
    // 通过 cursorForPosition 获取该像素位置的列号
    QTextCursor c = cursorForPosition(pos);
    return c.isNull() ? 0 : c.columnNumber();
}

void MyTextEdit::rebuildColumnCursors(int startLine, int startCol, int endLine, int endCol)
{
    // 标准化：确保 startLine <= endLine，startCol <= endCol
    if (startLine > endLine) std::swap(startLine, endLine);
    if (startCol > endCol) std::swap(startCol, endCol);

    m_columnCursors.clear();
    m_secondaryCursors.clear();  // 复用 T17 视觉显示

    QTextCursor cursor(document());
    for (int line = startLine; line <= endLine; ++line) {
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line);
        // 移动到 startCol（注意不能超出该行长度）
        QTextBlock block = cursor.block();
        int lineLength = block.text().length();
        int selStart = qMin(startCol, lineLength);
        int selEnd = qMin(endCol, lineLength);
        if (selEnd <= selStart) continue;  // 该行太短，跳过

        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, selStart);
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, selEnd - selStart);

        m_columnCursors.append(cursor);
        // 同时加入次级光标列表（复用 T17 高亮显示）
        // 主光标用第一行的 cursor，其余作为次级
        if (m_columnCursors.size() == 1) {
            setTextCursor(cursor);
        } else {
            m_secondaryCursors.append(cursor);
        }
    }

    // P3-M03 子项3: 多行列选时设置多光标模式标志（复用 T17 的 keyPressEvent 输入分发）
    m_multiCursorMode = (m_secondaryCursors.size() > 0);

    // 若只有一行（非列选场景），主光标已设置；多行则次级光标列表记录其余行
    updateSecondaryCursorDisplay();
}

// ====================================================================
// P3-M03 子项5: 拼写检查实现
// ====================================================================

void MyTextEdit::performSpellCheck()
{
    if (!SpellChecker::instance().enabled()) {
        m_spellErrors.clear();
        viewport()->update();
        return;
    }
    // 全文档扫描（防抖已限制频率，大文件可接受）
    m_spellErrors = SpellChecker::instance().checkText(toPlainText());
    viewport()->update();
}

const SpellMisspelledRange* MyTextEdit::spellErrorAt(int docPosition) const
{
    for (const auto& err : m_spellErrors) {
        if (docPosition >= err.start && docPosition < err.start + err.length) {
            return &err;
        }
    }
    return nullptr;
}
