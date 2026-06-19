#ifndef ITABWIDGET_H
#define ITABWIDGET_H

#include <QString>

class IEditorEdit;

/// @brief 标签页组件抽象接口
/// 定义多文件标签页管理的核心能力
/// 上层代码只依赖此接口，不依赖 EditorTabBar 具体实现
class ITabWidget
{
public:
    virtual ~ITabWidget() = default;

    /// 新建空白标签页
    virtual void addNewTab() = 0;

    /// 打开文件标签页
    virtual void openFileTab(const QString& filePath, const QString& content) = 0;

    /// 关闭当前标签页
    virtual bool closeCurrentTab() = 0;

    /// 获取当前活跃编辑器（通过 IEditorEdit 接口解耦）
    virtual IEditorEdit* currentEditor() const = 0;

    /// 获取标签页数量
    virtual int tabCount() const = 0;

    /// 当前是否有修改
    virtual bool isCurrentModified() const = 0;

    /// 设置当前标签页修改状态
    virtual void setCurrentModified(bool modified) = 0;

    /// 获取当前文件路径
    virtual QString currentFilePath() const = 0;

    /// 设置当前标签页文件路径
    virtual void setCurrentFilePath(const QString& path) = 0;
};

#endif // ITABWIDGET_H
