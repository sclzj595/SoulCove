#ifndef IEDITOREDIT_H
#define IEDITOREDIT_H

#include <QString>
#include <QTextCursor>
#include <QStringList>
#include "core/lsp/LspTypes.h"  // R3: LspHighlightState（双轨高亮降级）

class ICompleter;

/// @brief 编辑模块抽象接口
/// 定义文本增删改查、行高亮、选中文本等纯虚函数
/// 所有编辑器实现类必须继承此接口，保证多态替换能力
class IEditorEdit
{
public:
    virtual ~IEditorEdit() = default;

    // === 文本操作 ===
    virtual QString toPlainText() const = 0;
    virtual void setPlainText(const QString& text) = 0;
    virtual void append(const QString& text) = 0;
    virtual void clear() = 0;
    virtual QTextCursor textCursor() const = 0;
    virtual void setTextCursor(const QTextCursor& cursor) = 0;

    // === 光标与选择 ===
    virtual int cursorPosition() const = 0;
    virtual int currentLine() const = 0;
    virtual int currentColumn() const = 0;

    // === 字体操作 ===
    virtual void setFontSize(int size) = 0;
    virtual int fontSize() const = 0;
    virtual void fontZoomIn() = 0;
    virtual void fontZoomOut() = 0;

    // === 行号相关（通过接口统一）===
    virtual void setLineNumberVisible(bool visible) = 0;
    virtual bool isLineNumberVisible() const = 0;
    virtual void updateLineNumberArea() = 0;

    // === 修改状态 ===
    virtual bool isModified() const = 0;
    virtual void setModified(bool modified) = 0;

    // === 补全器（通过接口解耦）===
    virtual void setCompleter(ICompleter* completer) = 0;
    virtual ICompleter* completer() const = 0;
    virtual void updateWordList() = 0;

    // === R3: LSP 高亮状态（双轨高亮降级，通过接口统一）===
    /// 设置 LSP 高亮状态，控制启发式兜底的启用/禁用
    virtual void setLspHighlightState(LspHighlightState state) = 0;

    // === 获取底层QWidget指针（用于布局管理和信号连接）===
    virtual QWidget* asWidget() = 0;
};

#endif // IEDITOREDIT_H
