#ifndef ICOMPLETER_H
#define ICOMPLETER_H

#include <QString>
#include <QStringList>
#include <QPair>

class QWidget;

/// @brief 补全模块抽象接口
/// 定义词库加载、联想匹配、弹窗展示纯虚函数
class ICompleter
{
public:
    virtual ~ICompleter() = default;

    /// 绑定目标编辑器
    virtual void bindEditor(void* editor) = 0;

    /// 设置候选词列表
    virtual void setWordList(const QString& fullText, int cursorPos) = 0;

    /// 根据当前输入筛选并更新补全列表
    virtual void updateCompletionList() = 0;

    /// 隐藏补全提示框
    virtual void hideCompletion() = 0;

    /// 是否可见
    virtual bool isCompletionVisible() const = 0;

    /// 设置最小触发前缀长度
    virtual void setMinPrefixLen(int length) = 0;
    virtual int getMinPrefixLen() const = 0;

    /// 获取光标所在位置的上下文（前缀 + 起始位置）
    virtual QPair<QString, int> getCurrentContext() const = 0;

    /// 处理光标移动事件
    virtual void handleCursorMovement() = 0;

    /// 获取补全项数量
    virtual int itemCount() const = 0;

    /// 是否有当前选中项
    virtual bool hasCurrentItem() const = 0;

    /// 获取底层QWidget指针（用于事件转发等底层操作）
    virtual QWidget* asWidget() = 0;

    /// 补全弹窗是否获得焦点（用于focusOut判断）
    virtual bool isCompleterFocused() const = 0;

    // ========== H3: 成员补全自动触发（. / -> / ::）==========
    /// 进入成员补全模式 — 跳过最小前缀检查，允许空前缀显示 LSP 候选
    virtual void triggerMemberCompletion() {}
    /// 退出成员补全模式
    virtual void clearMemberCompletion() {}
    /// 当前是否处于成员补全模式
    virtual bool isMemberCompletionMode() const { return false; }
};

#endif // ICOMPLETER_H
