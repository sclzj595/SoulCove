#ifndef COMPLETIONPREVIEWWIDGET_H
#define COMPLETIONPREVIEWWIDGET_H

#include "interfaces/lsp/ILspClient.h"

#include <QFrame>
#include <QString>

class QTextEdit;

/// @file CompletionPreviewWidget.h
/// @brief C04-11: 补全项预览面板
///
/// 作为 TextCompleter 的子组件，定位在补全弹窗右侧，
/// 显示当前选中补全项的完整签名 / 文档 / snippet 展开预览。
///
/// 显示内容：
/// - LSP 项：完整签名（detail）+ documentation（markdown 渲染）+ insertText 预览
/// - 本地项：仅显示「本地单词」
/// - snippet 项：展开后的代码（高亮显示 $1/$2 占位符）
///
/// 尺寸：固定宽度 300px，高度自适应内容（最大 200px）
class CompletionPreviewWidget : public QFrame
{
    Q_OBJECT

public:
    explicit CompletionPreviewWidget(QWidget* parent = nullptr);

    /// @brief 显示 LSP 项预览（detail + documentation + insertText）
    void setLspItem(const LspCompletionItem& item);

    /// @brief 显示本地词典项预览
    void setLocalWord(const QString& word);

    /// @brief 显示 snippet 项预览（高亮 $1/$2 占位符）
    void setSnippet(const QString& label, const QString& body);

    /// @brief 清空内容
    void clearContent();

    /// @brief 应用主题样式（主题切换时由 TextCompleter 调用）
    void applyTheme();

protected:
    /// 内容变化后按文档高度自适应（最大 200px）
    void adjustHeightToFit();

private:
    /// 转义 HTML 特殊字符
    QString escapeHtml(const QString& text) const;

    /// 简单 markdown → HTML（标题/代码块/行内代码/段落）
    QString markdownToHtml(const QString& markdown) const;

    /// snippet 占位符 $1/$2/${1:default} 高亮
    QString highlightSnippetPlaceholders(const QString& body) const;

    QTextEdit* m_textEdit = nullptr;
};

#endif // COMPLETIONPREVIEWWIDGET_H
