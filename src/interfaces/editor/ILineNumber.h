#ifndef ILINENUMBER_H
#define ILINENUMBER_H

#include <QPaintEvent>

class QWidget;

/// @brief 行号模块抽象接口
/// 定义行号绘制、滚动同步、样式刷新纯虚函数
class ILineNumber
{
public:
    virtual ~ILineNumber() = default;

    /// 刷新行号显示
    virtual void updateLineNumber() = 0;

    /// 设置可见性
    virtual void setVisible(bool visible) = 0;

    /// 是否可见
    virtual bool isVisible() const = 0;

    /// 计算行号区域宽度
    virtual int areaWidth() const = 0;

    /// 更新几何位置（跟随编辑区）
    virtual void updateGeometry(const QRect& editorRect, int width) = 0;

    /// 获取底层QWidget指针（用于绘制等底层操作）
    virtual QWidget* asWidget() = 0;
};

#endif // ILINENUMBER_H
