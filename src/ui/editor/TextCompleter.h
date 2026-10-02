// textCompleter.h
#ifndef TEXTCOMPLETER_H
#define TEXTCOMPLETER_H

#include "interfaces/editor/ICompleter.h"
#include "interfaces/lsp/ILspClient.h"

#include <QListWidget>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QKeyEvent>
#include <QMap>
#include <QSet>
#include <QHash>
#include <QElapsedTimer>

class CompletionPreviewWidget;

/// @brief 自动补全提示框组件
/// 实现ICompleter接口，提供词库加载、联想匹配、弹窗展示能力
/// 智能获取历史记录 自动补全逻辑 包括候选词筛选 显示 选择插入
class TextCompleter : public QListWidget, public ICompleter
{
	Q_OBJECT

private:
	/////////////////  私有辅助  //////////////
	
	// 筛选匹配的候选词
	void filterMatches(const QString& context);
	// 模糊匹配评分（返回-1表示不匹配，>0表示匹配质量，越大越好）
	int fuzzyMatchScore(const QString& pattern, const QString& text) const;
	// 历史子字符串
	void extractRecentFragments(const QString& fullText);
	// 文档候选单词
	void extractDocumentWords(const QString& fullText);
	// 添加多行显示控制
    void ensureMultiLineDisplay();
    // 应用当前主题样式
    void applyTheme();
    // 应用配置中的字体大小（与编辑器/设置页联动）
    void applyFontSize();
	// P0 C04-1: LSP kind 字符串 → 短类型标签（用于补全项视觉区分）
	QString lspKindToTag(const QString& kind) const;
	// C04-11: 同步预览面板位置（补全弹窗右侧）
	void syncPreviewPosition();
	// C04-11: 根据当前选中项刷新预览面板内容
	void updatePreviewForCurrentItem();
	// C04-11: 同步预览面板的显示/隐藏状态
	void syncPreviewVisibility();

protected:
	void keyPressEvent(QKeyEvent* event) override;
	bool event(QEvent* event) override;
	/// H3: 弹窗隐藏时清除成员补全模式标志（覆盖所有隐藏路径，包括 Qt::Popup 自动隐藏）
	void hideEvent(QHideEvent* event) override;
	/// C04-11: 弹窗位置变化时同步移动预览面板
	void moveEvent(QMoveEvent* event) override;

public:
	TextCompleter(QWidget* parent = nullptr);

	// ========== ICompleter 接口实现 ==========
	void bindEditor(void* editor) override { setTextEdit(static_cast<QTextEdit*>(editor)); }
	void setWordList(const QString& fullText, int cursorPos) override;
	void updateCompletionList() override;
	void hideCompletion() override;
	bool isCompletionVisible() const override { return isVisible(); }
	void setMinPrefixLen(int length) override;
	int getMinPrefixLen() const override;
	QPair<QString, int> getCurrentContext() const override;
	void handleCursorMovement() override;
	int itemCount() const override { return count(); }
	bool hasCurrentItem() const override { return currentItem() != nullptr; }
	QWidget* asWidget() override { return this; }
	bool isCompleterFocused() const override;

	// ========== 原有公开方法（保留兼容）==========
	void setTextEdit(QTextEdit *textEdit);  	// 绑定目标文本编辑器
	~TextCompleter();

	// ========== LSP 补全项注入（阶段三 L9-L11）==========
	/// @brief 注入 LSP 补全候选项（优先于本地词典显示）
	void setLspCompletionItems(const QList<LspCompletionItem>& items);
	/// @brief 清除 LSP 补全候选项
	void clearLspCompletionItems();

	// ========== H3: 成员补全自动触发（. / -> / ::）==========
	/// @brief 进入成员补全模式 — 跳过最小前缀检查，允许空前缀显示 LSP 候选
	/// 在用户输入 . / -> / :: 后由 MyTextEdit 调用，配合 requestLspCompletion 使用
	void triggerMemberCompletion() override;
	/// @brief 退出成员补全模式
	void clearMemberCompletion() override;
	/// @brief 当前是否处于成员补全模式
	bool isMemberCompletionMode() const override { return m_memberCompletionMode; }

	enum MatchingMode {
		// 三种匹配模式
		PrefixMatch,     // 前缀匹配
		SubstrMatch,     // 子串匹配
		FuzzyMatch       // 模糊匹配（字符序列匹配，如"ptn"匹配"println"）
	};

	// 设置匹配模式
	void setMatchingMode(MatchingMode mode);
	/// @brief 基于光标位置计算调整补全位置
	void adjustPosition();

	static bool isChineseChar(QChar ch);
	// 统一单词字符检查器
	static bool isWordChar(QChar ch);

signals:
	void cursorPositionChanged();

// 槽函数
public slots:
	/**
	 * @brief 处理用户选择的补全项
	 * @param index  选择List中的index		插入
	 */
	void onItemSelected(const QModelIndex &index);

private slots:

	// 延迟显示补全框
	void delayedShow();
	// P2: 节流防抖到期后执行实际的补全列表更新
	void performCompletionUpdate();
	// C04-11: 选中项变化时更新预览面板内容
	void onCurrentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);

private:
	// 私有字段 属性 
	QTextEdit* m_textEdit = nullptr;		// 绑定的文本编辑器 和 myTextEdit联动
	QStringList m_wordList;					// 候选词列表
	QStringList m_filteredList;				// 筛选后的候选词列表
	int m_minPrefixLen = 2;					// 触发智能补充的最小前缀长度

	MatchingMode m_matchingMode = FuzzyMatch;	// 匹配模式（默认模糊匹配）

	// 新增标志
	bool m_ignoreNextCursorChange = false;		// 忽略下一次光标变化的标志量 补全插入后避免误触

	int m_cursorPosition = 0;  		// 当前光标在文档中的位置
	QTimer m_showTimer;				// 延迟显示计时器(避免输入时频繁刷新)
	// 最近出现的文本片段 键值对 键为起始位置 值为片段内容  最近优先排序
	QMap<int, QString> m_recentFragments;

	// [性能优化] 文档内容缓存：避免每次击键全量扫描
	QString m_cachedDocHash;       // 文档内容MD5哈希（用于检测文档是否真正变化）
	QStringList m_cachedWordList;   // 缓存的候选词列表
	QElapsedTimer m_lastUpdateTime; // 上次更新时间戳（用于节流）
	int m_lastDocLength = -1;       // P2: 文档长度缓存（O(1) 快速判断是否需要 MD5）

	// P2: 200ms 节流防抖定时器 — 快速连续输入时丢弃中间查询，仅保留最后一次
	QTimer m_debounceTimer;

	// [LSP 补全] 语言服务器返回的候选项（优先于本地词典）
	QList<LspCompletionItem> m_lspItems;

	// P0 C04-2: LSP 补全结果缓存 — 按前缀缓存，LSP 响应延迟时立即显示缓存结果
	// 避免快速连续输入时弹窗空白闪烁
	QHash<QString, QList<LspCompletionItem>> m_lspCompletionCache;
	static constexpr int kMaxLspCacheEntries = 16;  // 缓存上限（超过时清空重建）

	// H3: 成员补全模式标志 — 输入 . / -> / :: 后置为 true，跳过最小前缀检查
	bool m_memberCompletionMode = false;
	// H3: 待处理成员补全标志 — 等待 LSP 响应期间为 true，收到响应后显示弹窗
	bool m_pendingMemberCompletion = false;

	// 自定义 data role：区分 LSP 项 vs 本地词典项
	static constexpr int RoleLspItem = Qt::UserRole + 100;  // 存储 LspCompletionItem 指针索引
	static constexpr int RoleIsLsp = Qt::UserRole + 101;    // 是否为 LSP 项

	// C04-11: 预览面板（位于补全弹窗右侧）
	CompletionPreviewWidget* m_previewWidget = nullptr;

};

#endif 	// TEXTCOMPLETER_H