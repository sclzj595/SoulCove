#include "controller/EditorActions.h"
#include "interfaces/editor/IEditorEdit.h"
#include "core/format/CodeFormatter.h"
#include "ui/editor/MyTextEdit.h"

#include <QTextCursor>
#include <QTextBlock>
#include <QTextDocument>
#include <QFileInfo>
#include <QScrollBar>
#include <algorithm>

// ============================================================
// 行注释切换
// ============================================================

QString EditorActions::commentTokenForFile(const QString& filePath)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == "py" || suffix == "sh" || suffix == "yaml" || suffix == "yml"
        || suffix == "rb" || suffix == "pl" || suffix == "r" || suffix == "conf"
        || suffix == "toml" || suffix == "ini" || suffix == "properties"
        || suffix == "dockerfile" || suffix == "makefile" || suffix == "cmake")
        return QStringLiteral("#");
    if (suffix == "sql")
        return QStringLiteral("--");
    return QStringLiteral("//");   // C/C++/Java/JS/Go/Rust/PHP 等
}

void EditorActions::toggleLineComment(IEditorEdit* editor, const QString& filePath)
{
    if (!editor) return;

    // 通过 asWidget 获取具体编辑器（需要 document() 访问）
    auto* ed = qobject_cast<MyTextEdit*>(editor->asWidget());
    if (!ed) return;

    QString commentToken = commentTokenForFile(filePath);

    QTextCursor cursor = ed->textCursor();
    QTextDocument* doc = ed->document();

    int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    if (start > end) std::swap(start, end);

    // 无选择：切换当前行
    if (start == end) {
        QTextBlock block = doc->findBlock(start);
        if (!block.isValid()) return;
        cursor.beginEditBlock();
        QString text = block.text();
        cursor.setPosition(block.position());
        if (text.startsWith(commentToken + " ")) {
            for (int i = 0; i < commentToken.length() + 1; ++i)
                cursor.deleteChar();
        } else if (text.startsWith(commentToken)) {
            for (int i = 0; i < commentToken.length(); ++i)
                cursor.deleteChar();
        } else {
            cursor.insertText(commentToken + " ");
        }
        cursor.endEditBlock();
        return;
    }

    // 多行：先检查所有非空行是否都已注释
    QTextBlock startBlock = doc->findBlock(start);
    QTextBlock endBlock = doc->findBlock(end);
    if (!startBlock.isValid() || !endBlock.isValid()) return;

    bool allCommented = true;
    for (QTextBlock b = startBlock; b.isValid() && b.position() <= endBlock.position(); b = b.next()) {
        QString text = b.text();
        if (!text.isEmpty() && !text.startsWith(commentToken)) {
            allCommented = false;
            break;
        }
    }

    cursor.beginEditBlock();
    for (QTextBlock b = startBlock; b.isValid() && b.position() <= endBlock.position(); b = b.next()) {
        if (b.text().isEmpty()) continue;   // 空行跳过
        cursor.setPosition(b.position());
        if (allCommented) {
            if (b.text().startsWith(commentToken + " ")) {
                for (int i = 0; i < commentToken.length() + 1; ++i)
                    cursor.deleteChar();
            } else if (b.text().startsWith(commentToken)) {
                for (int i = 0; i < commentToken.length(); ++i)
                    cursor.deleteChar();
            }
        } else {
            cursor.insertText(commentToken + " ");
        }
    }
    cursor.endEditBlock();
}

// ============================================================
// 大小写转换
// ============================================================

void EditorActions::toUpperCase(IEditorEdit* editor)
{
    if (!editor) return;
    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) return;
    cursor.beginEditBlock();
    cursor.insertText(cursor.selectedText().toUpper());
    cursor.endEditBlock();
}

void EditorActions::toLowerCase(IEditorEdit* editor)
{
    if (!editor) return;
    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) return;
    cursor.beginEditBlock();
    cursor.insertText(cursor.selectedText().toLower());
    cursor.endEditBlock();
}

// ============================================================
// 代码格式化
// ============================================================

void EditorActions::formatDocument(IEditorEdit* editor, const QString& filePath)
{
    if (!editor) return;

    QString code = editor->toPlainText();
    QString formatted = CodeFormatter::instance().format(code, filePath);

    // 保存光标位置和滚动位置
    int pos = editor->textCursor().position();
    int scrollValue = 0;
    auto* editWidget = qobject_cast<MyTextEdit*>(editor->asWidget());
    if (editWidget) scrollValue = editWidget->verticalScrollBar()->value();

    editor->setPlainText(formatted);

    // 恢复光标位置（不超过文档长度）
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(qMin(pos, formatted.length()));
    editor->setTextCursor(cursor);
    if (editWidget)
        editWidget->verticalScrollBar()->setValue(scrollValue);
}
