#ifndef ITABWIDGET_H
#define ITABWIDGET_H

#include <QString>
#include <QList>
#include <QPair>

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

    /// R3: 获取所有已打开标签页的 (filePath, editor) 列表
    /// 用于 LspCoordinator 遍历编辑器进行状态路由，消除 dynamic_cast 向下转型
    /// @return (文件路径, 编辑器接口) 列表，特殊标签页（无文件）的路径为空
    virtual QList<QPair<QString, IEditorEdit*>> allEditors() const = 0;

    /// O19: 按文件路径查找编辑器（路径比较做 NativeSeparators 归一化）
    /// 默认实现返回 nullptr；实现类可用内部索引/映射提供 O(1)~O(n) 无分配查找，
    /// 避免调用方每次 allEditors() 构造 QList 拷贝
    /// @return 匹配的编辑器接口，未找到返回 nullptr
    virtual IEditorEdit* editorForPath(const QString& filePath) const
    {
        Q_UNUSED(filePath)
        return nullptr;
    }
};

#endif // ITABWIDGET_H
