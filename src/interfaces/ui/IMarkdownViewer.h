#ifndef IMARKDOWNVIEWER_H
#define IMARKDOWNVIEWER_H

#include <QString>

/// @brief Markdown 预览器抽象接口
/// 定义 Markdown 编辑+预览的核心能力
/// 上层代码只依赖此接口，不依赖 MarkdownMode 具体实现
class IMarkdownViewer
{
public:
    virtual ~IMarkdownViewer() = default;

    /// 设置编辑器内容
    virtual void setContent(const QString& text) = 0;

    /// 获取编辑器内容
    virtual QString content() const = 0;

    /// 刷新预览
    virtual void refreshPreview() = 0;
};

#endif // IMARKDOWNVIEWER_H
