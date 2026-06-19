#include "textCompleter.h"
#include "core/ThemeManager.h"
#include "core/ConfigManager.h"
#include "Logger.hpp"

#include <QTextCursor>
#include <QRect>
#include <QPoint>
#include <QTextDocument>
#include <QDebug>
#include <QApplication>
#include <QScrollBar>
#include <QScreen>
#include <QRegExp>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QGraphicsDropShadowEffect>
#include <QCryptographicHash>
#include <algorithm>

bool TextCompleter::isChineseChar(QChar ch)
{
	// 扩展中文标点支持
    static QSet<QChar> chinesePunctuation = {
        QChar(0x3000), QChar(0x3001), QChar(0x3002), QChar(0xFF01), 
        QChar(0xFF0C), QChar(0xFF1A), QChar(0xFF1B), QChar(0xFF1F)
    };
    
    // 检查汉字或中文标点
    return (ch.script() == QChar::Script_Han) || chinesePunctuation.contains(ch);
}

bool TextCompleter::isWordChar(QChar ch)
{
	return ch.isLetterOrNumber() || ch == '_' || isChineseChar(ch);
}

TextCompleter::TextCompleter(QWidget *parent) : QListWidget(parent)
{
	// 现代化补全弹窗 - Popup + 无边框 + 半透明阴影
	setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
	setWindowOpacity(0.96);
	setFocusPolicy(Qt::NoFocus);
	setSelectionMode(QAbstractItemView::SingleSelection);

	// 尺寸限制
	setMinimumWidth(320);
	setMaximumWidth(580);
	setMinimumHeight(36);
	setMaximumHeight(380);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

	// 阴影效果（模拟VSCode/JetBrains的浮层感）
	auto* shadow = new QGraphicsDropShadowEffect(this);
	shadow->setBlurRadius(20);
	shadow->setColor(QColor(0, 0, 0, 100));
	shadow->setOffset(0, 4);
	setGraphicsEffect(shadow);

	// 样式由applyTheme()根据ThemeManager动态生成
	applyTheme();

	// 监听主题切换
	connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
	        this, &TextCompleter::applyTheme);

	// 字体：等宽+中文混排优化，大小从配置中心读取（与编辑器/设置页联动）
	applyFontSize();
	// 监听配置变更，字体大小变化时自动刷新
	connect(&ConfigManager::instance(), &ConfigManager::configChanged,
	        this, [this](const QString& key, const QVariant& value) {
		if (key == QStringLiteral("Display/fontSize")) {
			applyFontSize();
		}
	});

	// 计时器
	m_showTimer.setSingleShot(true);
	connect(&m_showTimer, &QTimer::timeout, this, &TextCompleter::delayedShow);
	connect(this, &QListWidget::clicked, this, &TextCompleter::onItemSelected);
	connect(this, &TextCompleter::cursorPositionChanged, this, &TextCompleter::handleCursorMovement);
}

// setter
void TextCompleter::setTextEdit(QTextEdit *textEdit)
{
	m_textEdit = textEdit;
}

/// @brief 核心函数：更新候选词列表（合并历史片段和文档单词）
/// @param fullText 完整文档内容
/// @param cursorPos 当前光标位置
void TextCompleter::setWordList(const QString &fullText, int cursorPos)
{
	// 首先更新当前光标位置 计算片段距离
	m_cursorPosition = cursorPos;
	// 清空原有列表
	m_wordList.clear();
	m_recentFragments.clear();

	// 提取历史子字符串  位置优先级排序
	extractRecentFragments(fullText);
	// 提取其中独立单词 去重 
	extractDocumentWords(fullText);

	// 合并候选词 使用Qset去重
	QSet<QString> uniqueSet;
	QStringList combinedList;

	// 最近优先排序
	QList<int> pos = m_recentFragments.keys();
	std::sort(pos.begin(), pos.end(), [cursorPos](int a, int b) {
		// 距离绝对值来排序
		return std::abs(a - cursorPos) < std::abs(b - cursorPos);
	});

	// 添加排序后的历史记录 
	for (int idx : pos) {
		const QString& fragment = m_recentFragments[idx];
		if (!uniqueSet.contains(fragment)) {
			uniqueSet.insert(fragment);
			combinedList.append(fragment);
		}
	}

	// 添加文档单词
	for (const QString& word : m_wordList) {
		if (!uniqueSet.contains(word)) {
			uniqueSet.insert(word);
			combinedList.append(word);
		}
	}

	// 综合排序 先长度 同长度按照字典序 不区分大小写
	std::sort(combinedList.begin(), combinedList.end(), [](const QString& a, const QString& b) {
		// 一级排序 长度
		if (a.length() != b.length()) {
			return a.length() < b.length();
		}
		// 剩下的就是长度相同的情况  直接字典序
		// return a.toLower() < b.toLower();
		return a.compare(b, Qt::CaseInsensitive) < 0;
	});

	// 限制最大候选词数量 
	if (combinedList.size() > 200)		combinedList = combinedList.mid(0, 200);

	m_wordList = combinedList;
}

/// @brief 提取文档中包含当前前缀的历史子字符串
/// @param fullText 完整文档内容
void TextCompleter::extractRecentFragments(const QString& fullText) {
	// // 当前前缀的边界 前后扫描 遇见隔断就停止
	// int start = m_cursorPosition;
	// int end = m_cursorPosition;
	// while (end < fullText.length() && isWordChar(fullText[end]))	end++;
	// while (start > 0 && isWordChar(fullText[start - 1]))	start--;

	// // 提取当前输入的前缀 
	// QString curInput = fullText.mid(start, end - start);
	
	// // scan整个doc 提取包含前缀的所有连续的单词片段
	// int scanPos = 0; // 扫描起始位置
    // while (scanPos < fullText.length()) {
    //     // 找到单词片段的起始位置（跳过非单词字符）
    //     if (isWordChar(fullText[scanPos])) {
    //         int fragStart = scanPos; // 片段起始索引
    //         int fragEnd = scanPos;   // 片段结束索引
    //         // 向后扩展到单词片段结束（遇到非单词字符停止）
    //         while (fragEnd < fullText.length() && isWordChar(fullText[fragEnd])) 		fragEnd++;
            
    //         // 提取完整单词片段
    //         QString fragment = fullText.mid(fragStart, fragEnd - fragStart);
            
    //         // 只保留包含当前前缀的片段（不区分大小写）
    //         if (fragment.contains(curInput, Qt::CaseInsensitive))     m_recentFragments[fragStart] = fragment;
            
    //         // 跳到下一个片段继续扫描
    //         scanPos = fragEnd;
    //     } else {
    //         // 非单词字符：直接跳过
    //         scanPos++;
    //     }
    // }

	auto context = getCurrentContext();
    QString curPrefix = context.first;
    
    // 前导处理
    if (curPrefix.isEmpty() || curPrefix.length() < m_minPrefixLen) {
        return;
    }
    
    // 扫描整个文档提取包含前缀的片段
    int scanPos = 0;
    while (scanPos < fullText.length()) {
        // 跳过非单词字符
        while (scanPos < fullText.length() && !isWordChar(fullText[scanPos])) {
            scanPos++;
        }
        
        if (scanPos >= fullText.length()) break;
        
        // 提取连续单词片段
        int fragStart = scanPos;
        int fragEnd = scanPos;
        while (fragEnd < fullText.length() && isWordChar(fullText[fragEnd])) {
            fragEnd++;
        }
        
        QString fragment = fullText.mid(fragStart, fragEnd - fragStart);
        
        // 包含当前前缀（支持中文）
        if (fragment.contains(curPrefix, Qt::CaseInsensitive)) {
            m_recentFragments[fragStart] = fragment;
        }
        
        scanPos = fragEnd;
    }
}

/// @brief 提取文档中所有符合规则的独立单词
/// @param fullText 完整文档内容
void TextCompleter::extractDocumentWords(const QString& fullText) {
	// // 使用Unicode属性正则表达式
	// QRegExp wordRegex("\\b[\\w\\p{Han}]+\\b"); // 匹配单词字符和汉字
    // int pos = 0; // 匹配起始位置
    
    // // 循环查找所有匹配的单词
    // while ((pos = wordRegex.indexIn(fullText, pos)) != -1) {
    //     QString word = wordRegex.cap(); // 获取匹配到的单词
    //     // 避免重复添加
    //     if (!m_wordList.contains(word)) 	m_wordList.append(word);
    //     // 移动到下一个匹配位置
    //     pos += wordRegex.matchedLength();
    // }

	QRegularExpression wordRegex(
        R"([\w\p{Han}]+)"  // 匹配单词字符和汉字
    );
    
    QRegularExpressionMatchIterator it = wordRegex.globalMatch(fullText);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString word = match.captured();
        if (!m_wordList.contains(word)) {
            m_wordList.append(word);
        }
    }
    
    // 添加单字中文（增强中文补全）
    for (int i = 0; i < fullText.length(); ++i) {
        QChar ch = fullText[i];
        if (isChineseChar(ch)) {
            QString singleChar = QString(ch);
            if (!m_wordList.contains(singleChar)) {
                m_wordList.append(singleChar);
            }
        }
    }
}

void TextCompleter::ensureMultiLineDisplay()
{
	// [性能优化] 只在有项目时调整布局
    if (count() > 0) {
        // 动态调整高度以显示多行
        int visibleItems = qMin(count(), 8); // 最多显示8行
        int itemHeight = sizeHintForRow(0);
        setFixedHeight(visibleItems * itemHeight + 2 * frameWidth());

        // [性能优化] 确保宽度足够（遍历计算最大宽度）
        int maxWidth = 0;
        for (int i = 0; i < count(); ++i) {
            int itemWidth = fontMetrics().horizontalAdvance(item(i)->text());
            maxWidth = qMax(maxWidth, itemWidth);
        }
        setMinimumWidth(qMax(300, maxWidth + 20));
    }
}

void TextCompleter::setMinPrefixLen(int length)
{
	// 有效是 > 0
	if (length > 2)	m_minPrefixLen = length;
}

int TextCompleter::getMinPrefixLen() const
{
	return m_minPrefixLen;
}

void TextCompleter::setMatchingMode(MatchingMode mode)
{
	m_matchingMode = mode;
}

void TextCompleter::delayedShow()
{
	// 添加最小前缀长度检查
    auto context = getCurrentContext();
    if (context.first.length() < m_minPrefixLen) {
        hideCompletion();
        return;
    }

#ifdef QT_DEBUG
	LOG_DEBUG("延迟显示补全框");
#endif
    if (count() > 0 && !isVisible()) {
#ifdef QT_DEBUG
        LOG_DEBUG("显示补全框，项目数:" << count());
#endif
        setCurrentRow(0);
        adjustPosition();

        // 关键修改：设置属性确保正确获取焦点
        setAttribute(Qt::WA_ShowWithoutActivating, false);
        show();
        activateWindow(); // 激活窗口
        raise();
        setFocus(); // 强制获取焦点

#ifdef QT_DEBUG
        LOG_DEBUG("补全框已显示并激活");
#endif
    } else {
#ifdef QT_DEBUG
        LOG_DEBUG("不显示补全框 - 项目数:" << count()
                 << "已可见:" << isVisible());
#endif
    }
}

// 给提示框位置
void TextCompleter::adjustPosition()
{
#ifdef QT_DEBUG
	LOG_DEBUG("====== 开始计算补全框位置 ======");
#endif
    if (!m_textEdit) {
#ifdef QT_DEBUG
        LOG_DEBUG("错误：未绑定文本编辑器");
#endif
        return;
    }

	// 根据光标位置来 动态设置提示框
	QRect cursorRect = m_textEdit->cursorRect();
	QPoint idealPos = m_textEdit->mapToGlobal(cursorRect.bottomLeft());
    idealPos.setY(idealPos.y() + 5); // 向下偏移避免遮挡光标
#ifdef QT_DEBUG
    LOG_DEBUG("光标矩形位置:" << cursorRect);
#endif

	///////////////////////////////////////////////////////////////////////////

	// 考虑编辑器滚动
	// 坐标位置来定位  其实也可以用一个数对来存储
	// QPoint pos = m_textEdit->mapToGlobal(cursorRect.bottomLeft());

	// // 滚动条偏移
	// int verticalScroll = m_textEdit->verticalScrollBar()->value();
	// int horizontalScroll = m_textEdit->horizontalScrollBar()->value();

	// // 窗口位置和滚动
	// pos.setX(pos.x() - horizontalScroll);
	// pos.setY(pos.y() - verticalScroll);

	///////////////////////////////////////////////////////////////////////////

	// 始终在屏幕里面
	QScreen* screen = QGuiApplication::screenAt(idealPos);
    // if (!screen) return;
	// QScreen* screen = QGuiApplication::screenAt(pos);
	if (!screen) {
#ifdef QT_DEBUG
        LOG_DEBUG("错误：无法获取屏幕信息");
#endif
        return;
    }

	QRect screenGeometry = screen->availableGeometry();
#ifdef QT_DEBUG
	LOG_DEBUG("屏幕可用区域:" << screenGeometry);
#endif
	int x = idealPos.x();
	int y = idealPos.y();
#ifdef QT_DEBUG
	LOG_DEBUG("初始位置 - X:" << x << "Y:" << y);
#endif

	// 空间不足的话  显示换位置
	if (y + height() > screenGeometry.bottom()) {
#ifdef QT_DEBUG
        LOG_DEBUG("下方空间不足，调整到上方");
#endif
        y = idealPos.y() - height() - cursorRect.height();
        if (y < screenGeometry.top()) {
#ifdef QT_DEBUG
            LOG_DEBUG("上方空间不足，贴顶显示");
#endif
            y = screenGeometry.top();
        }
    }

    if (x + width() > screenGeometry.right()) {
#ifdef QT_DEBUG
        LOG_DEBUG("右侧空间不足，调整到左侧");
#endif
        x = screenGeometry.right() - width();
    }

    if (x < screenGeometry.left()) {
#ifdef QT_DEBUG
        LOG_DEBUG("左侧空间不足，贴左显示");
#endif
        x = screenGeometry.left();
    }

#ifdef QT_DEBUG
    LOG_DEBUG("最终位置 - X:" << x << "Y:" << y);
#endif
    setGeometry(x, y, width(), height());
#ifdef QT_DEBUG
    LOG_DEBUG("补全框位置设置完成:" << geometry());
#endif

	ensureMultiLineDisplay();
}

// 获取上下文
QPair<QString, int> TextCompleter::getCurrentContext() const
{	
	// 前导处理
	if (!m_textEdit)	return qMakePair(QString(), 0);

	QTextCursor cursor = m_textEdit->textCursor();
	int pos = cursor.position();		// 光标位置
	QString fullText = m_textEdit->toPlainText();

	// 往前查找单词起始位置
	int start = pos;
	while (start > 0) {
		QChar ch = fullText[start - 1];		// 数组的index 和 实际 pos 差 1
		// 中英文都要算 - 添加中文支持
		// 添加更多中文标点支持
        if (!isWordChar(ch))
			break;
		start--;		// 往前迭代
	}

	// 往后找结束位置
	int end = pos;
	while (end < fullText.length()) {
		QChar ch = fullText[end];		// 数组的index 和 实际 pos 差 1
		// 中英文都要算 - 添加中文支持
		// 添加更多中文标点支持
        if (!isWordChar(ch)) 	
			break;
		end++;		// 往前迭代
	}
	// cur的单词和开始位置
	QString curWord = fullText.mid(start, end - start);

	return qMakePair(curWord, start);
}

// 模糊匹配评分算法
int TextCompleter::fuzzyMatchScore(const QString& pattern, const QString& text) const
{
    // 空模式匹配一切，但得分最低
    if (pattern.isEmpty()) return 0;

    int pi = 0;  // pattern索引
    int ti = 0;  // text索引
    int score = 0;
    bool prevMatched = false;

    while (pi < pattern.length() && ti < text.length()) {
        if (pattern[pi].toLower() == text[ti].toLower()) {
            score += 10;  // 基础匹配分

            // 连续匹配加分
            if (pi > 0 && ti > 0 && pattern[pi - 1].toLower() == text[ti - 1].toLower()) {
                score += 15;  // 连续匹配奖励
            }

            // 词首匹配加分（下划线后或大写字母开头）
            if (ti == 0 || text[ti - 1] == '_' || text[ti - 1].isSpace() ||
                (text[ti].isUpper() && text[ti - 1].isLower())) {
                score += 20;  // 词边界匹配奖励
            }

            // 前缀匹配额外加分
            if (pi == 0 && ti == 0) {
                score += 30;  // 首字符匹配奖励
            }

            prevMatched = true;
            pi++;
        } else {
            prevMatched = false;
        }
        ti++;
    }

    // pattern未完全匹配 → 不匹配
    if (pi < pattern.length()) return -1;

    // 匹配越靠前加分越多
    score += qMax(0, 10 - (ti - pattern.length())) * 2;

    return score;
}

// 筛选匹配的候选词
void TextCompleter::filterMatches(const QString &context)
{
	// 先做前导处理
	m_filteredList.clear();
    if (context.isEmpty()) return;

    if (m_matchingMode == FuzzyMatch) {
        // 模糊匹配模式：使用评分算法
        QList<QPair<int, QString>> scored;  // (score, word)
        for (const auto& word : m_wordList) {
            int s = fuzzyMatchScore(context, word);
            if (s > 0) {
                scored.append(qMakePair(s, word));
            }
        }
        // 按评分降序排序
        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        for (int i = 0; i < qMin(scored.size(), 20); ++i) {
            m_filteredList.append(scored[i].second);
        }
    } else if (m_matchingMode == SubstrMatch) {
        // 子串匹配模式
        for (const auto& word : m_wordList) {
            if (word.contains(context, Qt::CaseInsensitive)) {
                m_filteredList.append(word);
                if (m_filteredList.size() >= 20) break;
            }
        }
    } else {
        // 前缀匹配模式
        for (const auto& word : m_wordList) {
            if (word.startsWith(context, Qt::CaseInsensitive)) {
                m_filteredList.append(word);
                if (m_filteredList.size() >= 20) break;
            }
        }
    }

    // 中文优先排序：将包含前缀的汉字排在前面
    // 使用 static QRegularExpression 避免每次比较都构造新对象
    static const QRegularExpression chineseRegex(R"(\p{Han})");
    std::sort(m_filteredList.begin(), m_filteredList.end(), [&context](const QString& a, const QString& b) {
        bool aContainsChinese = a.contains(chineseRegex);
        bool bContainsChinese = b.contains(chineseRegex);

        if (aContainsChinese && !bContainsChinese) return true;
        if (!aContainsChinese && bContainsChinese) return false;

        // 都包含中文时，按匹配质量排序
        int aScore = a.startsWith(context) ? 2 : (a.contains(context) ? 1 : 0);
        int bScore = b.startsWith(context) ? 2 : (b.contains(context) ? 1 : 0);

        if (aScore != bScore) return aScore > bScore;
        return a.length() < b.length();
    });
}

/// @brief 动态更新补全列表 核心触发逻辑
void TextCompleter::updateCompletionList()
{
	// 前导处理  没有绑定编辑器就直接返回
#ifdef QT_DEBUG
	LOG_DEBUG("====== 更新补全列表 ======");
#endif
    if (!m_textEdit) {
#ifdef QT_DEBUG
		LOG_DEBUG("错误：未绑定文本编辑器");
#endif
        return;
    }

    // 获取当前上下文（支持中文）
    auto context = getCurrentContext();
    QString curWord = context.first;
    int startPos = context.second;
#ifdef QT_DEBUG
    LOG_DEBUG("当前上下文 - 前缀:" << curWord << "长度:" << curWord.length()
             << "起始位置:" << startPos);
#endif

    // 条件检查
    if (curWord.length() < m_minPrefixLen) {
#ifdef QT_DEBUG
		LOG_DEBUG("前缀长度不足 (" << curWord.length() << "<" << m_minPrefixLen << ")，隐藏补全框");
#endif
        hideCompletion();
        return;
    }

    // 获取最新文档内容（支持中文）
    QString fullText = m_textEdit->toPlainText();

    // [性能优化] 快速路径：通过文档哈希检测是否真正变化，避免每次击键全量扫描
    QString docHash = QCryptographicHash::hash(fullText.toUtf8(), QCryptographicHash::Md5).toHex().left(8);
    bool docChanged = (docHash != m_cachedDocHash);

    if (!docChanged && !m_cachedWordList.isEmpty()) {
        // 文档未变，直接用缓存的词列表做筛选，跳过 extractRecentFragments + extractDocumentWords
        m_wordList = m_cachedWordList;
    } else {
        // 文档已变化或首次加载，执行全量扫描
        m_cachedDocHash = docHash;

        // 清空并重新构建候选列表
        m_wordList.clear();
        m_recentFragments.clear();

        // 提取历史片段（支持中文）
        extractRecentFragments(fullText);

        // 提取文档单词（支持中文）
        extractDocumentWords(fullText);

        // 合并候选词并去重
        QSet<QString> uniqueSet;
        QStringList combinedList;

        // 添加历史片段（按距离排序）
        QList<int> positions = m_recentFragments.keys();
        std::sort(positions.begin(), positions.end(), [startPos](int a, int b) {
            return std::abs(a - startPos) < std::abs(b - startPos);
        });

        for (int pos : positions) {
            const QString& fragment = m_recentFragments[pos];
            if (!uniqueSet.contains(fragment)) {
                uniqueSet.insert(fragment);
                combinedList.append(fragment);
            }
        }

        // 添加文档单词
        for (const QString& word : m_wordList) {
            if (!uniqueSet.contains(word)) {
                uniqueSet.insert(word);
                combinedList.append(word);
            }
        }

        // 按长度和字典序排序（支持中文）
        std::sort(combinedList.begin(), combinedList.end(), [](const QString& a, const QString& b) {
            if (a.length() != b.length()) {
                return a.length() < b.length();
            }
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });

        // 限制最大候选词数量
        if (combinedList.size() > 200) combinedList = combinedList.mid(0, 200);

        m_wordList = combinedList;
        // 缓存结果供后续使用
        m_cachedWordList = m_wordList;
    }
    
    // 筛选匹配的候选词（支持中文子串匹配）
    filterMatches(curWord);

    // 更新UI显示
    clear();

    // [LSP 补全] 优先显示 LSP 候选项（带类型详情，排在本地词典前面）
    int lspShown = 0;
    if (!m_lspItems.isEmpty()) {
        // 按 sortText 排序（LSP 服务器提供的排序优先级）
        QList<LspCompletionItem> sortedLsp = m_lspItems;
        std::sort(sortedLsp.begin(), sortedLsp.end(), [](const LspCompletionItem& a, const LspCompletionItem& b) {
            // sortText 优先，其次按 label
            if (!a.sortText.isEmpty() && !b.sortText.isEmpty() && a.sortText != b.sortText)
                return a.sortText < b.sortText;
            return a.label < b.label;
        });

        for (const LspCompletionItem& item : sortedLsp) {
            // 前缀匹配筛选（LSP 项按 label 匹配当前输入前缀）
            if (!curWord.isEmpty() && !item.label.startsWith(curWord, Qt::CaseInsensitive))
                continue;

            // 显示文本：label + detail（如 "printf  (int, const char*)"）
            QString displayText = item.label;
            if (!item.detail.isEmpty())
                displayText += QStringLiteral("  ") + item.detail;

            // tooltip 显示完整文档
            QString tooltip = item.kind;
            if (!item.documentation.isEmpty())
                tooltip += QStringLiteral("\n") + item.documentation;

            QListWidgetItem* listItem = new QListWidgetItem(displayText, this);
            listItem->setToolTip(tooltip);
            listItem->setData(RoleIsLsp, true);
            listItem->setData(RoleLspItem, item.label);  // 用 label 作为查找键
            // 存储 insertText（可能与 label 不同，如代码片段补全）
            listItem->setData(Qt::UserRole, item.insertText.isEmpty() ? item.label : item.insertText);

            addItem(listItem);
            ++lspShown;
            if (lspShown >= 50) break;  // LSP 项上限
        }
    }

    // 本地词典候选项（追加在 LSP 项之后）
    if (!m_filteredList.isEmpty()) {
#ifdef QT_DEBUG
        LOG_DEBUG("找到" << m_filteredList.size() << "个本地候选词, " << lspShown << "个LSP候选");
#endif
        for (const QString& word : m_filteredList) {
            // 避免与已显示的 LSP 项重复
            bool dup = false;
            for (int i = 0; i < count(); ++i) {
                if (item(i)->data(RoleIsLsp).toBool() &&
                    item(i)->data(RoleLspItem).toString() == word) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                addItem(new QListWidgetItem(word));
            }
        }
        ensureMultiLineDisplay(); // 确保多行显示
        delayedShow();
    } else if (lspShown > 0) {
        // 只有 LSP 项，没有本地词典项
        ensureMultiLineDisplay();
        delayedShow();
    } else {
#ifdef QT_DEBUG
        LOG_DEBUG("没有匹配的候选词，隐藏补全框");
#endif
        hideCompletion();
    }
}

void TextCompleter::hideCompletion()
{
	if (isVisible())	hide();
}

////////////////////  槽函数具体实现  /////////////////////////////
// 处理用户选择的补全项
void TextCompleter::onItemSelected(const QModelIndex &index) {
	// 文本框和索引有效
	if (!m_textEdit || !index.isValid())	return;

	// 检查是否为 LSP 补全项（使用 insertText 而非 displayText）
	bool isLsp = index.data(RoleIsLsp).toBool();
	QString completion;
	if (isLsp) {
		// LSP 项：使用存储的 insertText（可能是代码片段）
		completion = index.data(Qt::UserRole).toString();
	} else {
		// 本地词典项：使用显示文本
		completion = index.data(Qt::DisplayRole).toString();
	}

	// 候选框为空直接return
	if (completion.isEmpty())	return;

	// 获取当前的上下文
	auto context = getCurrentContext();
	QString curWord = context.first;
	int startPos = context.second;

	// 计算替换范围
	QTextCursor cursor = m_textEdit->textCursor();
	cursor.setPosition(startPos);
	cursor.setPosition(startPos + curWord.length(), QTextCursor::KeepAnchor);
	// 替换
	cursor.insertText(completion);

	// 然后就给焦点光标位置
	cursor.setPosition(startPos + completion.length());
	m_textEdit->setTextCursor(cursor);

	// 替换完之后隐藏提示框
	hideCompletion();
	m_textEdit->setFocus();		// 重新回编辑框

	// 状态量更新一下
	m_ignoreNextCursorChange = true;

	// LSP 项插入后清除 LSP 候选（避免过期数据残留）
	if (isLsp) {
		clearLspCompletionItems();
	}
}

// public slots
void TextCompleter::handleCursorMovement() {
	// [性能优化] 节流：50ms内不重复触发，避免高频光标移动导致性能问题
	if (m_lastUpdateTime.isValid() && m_lastUpdateTime.elapsed() < 50) return;
	m_lastUpdateTime.start();

	// 根据光标切换的状态来做补全框的隐藏与否
	if (m_ignoreNextCursorChange) {
		m_ignoreNextCursorChange = false;
		return;
	}
	updateCompletionList();
	adjustPosition();
}

/// @brief override Tab
/// @param event 
// 优化 keyPressEvent 函数
void TextCompleter::keyPressEvent(QKeyEvent *event)
{
    // switch-case
	switch (event->key())
	{
	case Qt::Key_Tab:
	case Qt::Key_Enter:
	case Qt::Key_Return:
		if (currentItem()) {
			onItemSelected(currentIndex());
			event->accept();
		}
		break;

	case Qt::Key_Up:
		if (currentRow() <= 0)	setCurrentRow(count() - 1);	// 循环到底部
		else setCurrentRow(currentRow() - 1);
		event->accept();
		break;

	case Qt::Key_Down:
		if (currentRow() >= count() - 1)	setCurrentRow(0);
		else setCurrentRow(currentRow() + 1);
		event->accept();
		break;
	
	case Qt::Key_Escape:
		hideCompletion();
		if (m_textEdit) 	m_textEdit->setFocus();
		event->accept();
		break;

	case Qt::Key_Left:
	case Qt::Key_Right:
		hideCompletion();
		if (m_textEdit) {
			QCoreApplication::sendEvent(m_textEdit, event);
			QTimer::singleShot(10, this, &TextCompleter::updateCompletionList);
		}
		event->accept();
		break;

	default:
		if (m_textEdit) {
			QCoreApplication::sendEvent(m_textEdit, event);
			if (!event->text().isEmpty())	updateCompletionList();
		}
		break;
	}
}

bool TextCompleter::event(QEvent *event)
{
	if (event->type() == QEvent::WindowDeactivate) {
        hideCompletion();
    }
    return QListWidget::event(event);
}

TextCompleter::~TextCompleter()
{
}

bool TextCompleter::isCompleterFocused() const
{
	return hasFocus() || isAncestorOf(QApplication::focusWidget());
}

void TextCompleter::applyTheme()
{
    const auto& p = ThemeManager::instance().currentPalette();
    auto c = [](const QColor& color) -> QString {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };

    // 背景色：使用tooltip/弹出层背景（比侧边栏略亮，更有浮层感）
    QString bg = c(p.bgTooltip.isValid() ? p.bgTooltip : p.bgSideBar.lighter(105));
    QString border = c(p.borderDefault);
    QString fg = c(p.fgPrimary);
    QString fgDim = c(p.fgSecondary);
    QString selBg = c(p.selectionBg);
    QString accent = c(p.accentPrimary);
    QString hoverBg = c(p.bgHover.isValid() ? p.bgHover : QColor(255, 255, 255, 12));
    QString scrollBg = c(p.scrollbarBg.isValid() ? p.scrollbarBg : QColor(0, 0, 0, 30));
    QString scrollHandle = c(p.scrollbarHandle.isValid() ? p.scrollbarHandle : QColor(255, 255, 255, 25));

    setStyleSheet(QStringLiteral(
        /* ===== 主容器 ===== */
        "QListWidget {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 8px;"
        "   padding: 4px;"
        "   color: %3;"
        "   outline: none;"
        "   font-family: 'Cascadia Code', '''Consolas', 'Microsoft YaHei', monospace;"
        "}"
        /* ===== 列表项：宽松内边距 + 微妙分隔 ===== */
        "QListWidget::item {"
        "   padding: 6px 16px;"
        "   border-radius: 4px;"
        "   margin: 1px 3px;"
        "   min-height: 20px;"
        "}"
        /* ===== 选中项：强调色高亮 ===== */
        "QListWidget::item:selected {"
        "   background-color: %4;"
        "   color: #ffffff;"
        "   border-left: 3px solid %5;"
        "   padding-left: 13px;"  /* 补偿左边框宽度 */
        "}"
        /* ===== 悬浮态：微妙提亮 ===== */
        "QListWidget::item:hover:!selected {"
        "   background-color: %6;"
        "}"
        /* ===== 滚动条：极细风格（VSCode风格）===== */
        "QListWidget::verticalScrollBar {"
        "   background: transparent;"
        "   width: 8px;"
        "   margin: 2px;"
        "   border: none;"
        "   border-radius: 4px;"
        "}"
        "QListWidget::verticalScrollBar::handle:vertical {"
        "   background: %7;"
        "   min-height: 28px;"
        "   border-radius: 4px;"
        "}"
        "QListWidget::verticalScrollBar::handle:vertical:hover {"
        "   background: rgba(255,255,255,0.35);"
        "}"
        "QListWidget::verticalScrollBar::add-line:vertical,"
        "QListWidget::verticalScrollBar::sub-line:vertical {"
        "   height: 0;"
        "   border: none;"
        "}"
        "QListWidget::verticalScrollBar::add-page:vertical,"
        "QListWidget::verticalScrollBar::sub-page:vertical {"
        "   background: transparent;"
        "}"
    ).arg(bg, border, fg, selBg, accent, hoverBg, scrollHandle));
}

void TextCompleter::applyFontSize()
{
    // 从配置中心读取字体大小（与编辑器/设置页联动）
    int size = ConfigManager::instance().fontSize();
    if (size < 8) size = 8;   // 下限保护
    QFont f = this->font();
    f.setPointSize(size);
    f.setFamily(QStringLiteral("Cascadia Code, Consolas, 'Microsoft YaHei', monospace"));
    setFont(f);
}

// ========== LSP 补全项注入（阶段三 L9-L11）==========

void TextCompleter::setLspCompletionItems(const QList<LspCompletionItem>& items)
{
    m_lspItems = items;
    // 收到新 LSP 候选后立即刷新补全列表（如果当前可见）
    if (isVisible() && m_textEdit) {
        updateCompletionList();
        adjustPosition();
    }
}

void TextCompleter::clearLspCompletionItems()
{
    m_lspItems.clear();
}
