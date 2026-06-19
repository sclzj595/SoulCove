#ifndef EDITORACTIONS_H
#define EDITORACTIONS_H

#include <QString>

class IEditorEdit;

/// @brief 编辑器操作控制器（Command Pattern）
///
/// 从 Widget 下沉的编辑器文本操作逻辑，包括：
/// - 行注释切换（根据文件后缀自动选择注释符号）
/// - 大小写转换
/// - 代码格式化（调用 ICodeFormatter）
///
/// 设计要点：
/// - 方法接收 IEditorEdit* 接口指针，不依赖具体 MyTextEdit
/// - 需要访问具体类特性时（如滚动条）通过 asWidget() + qobject_cast
/// - 无状态类，可被多个调用方共享
class EditorActions
{
public:
    /// 切换行注释（Ctrl+/）
    /// 根据文件后缀自动选择注释符号（# // -- 等）
    /// 支持单行和多行（选区）操作
    static void toggleLineComment(IEditorEdit* editor, const QString& filePath);

    /// 选中文本转大写
    static void toUpperCase(IEditorEdit* editor);

    /// 选中文本转小写
    static void toLowerCase(IEditorEdit* editor);

    /// 格式化文档（Ctrl+Shift+I）
    /// 调用 CodeFormatter，保存/恢复光标和滚动位置
    static void formatDocument(IEditorEdit* editor, const QString& filePath);

private:
    /// 根据文件后缀获取注释符号
    static QString commentTokenForFile(const QString& filePath);
};

#endif // EDITORACTIONS_H
