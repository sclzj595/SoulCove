#ifndef ISIDEFILEBAR_H
#define ISIDEFILEBAR_H

#include <QWidget>

/// @brief 侧边栏文件树抽象接口
/// 定义侧边栏文件列表的核心能力
/// 上层代码只依赖此接口，不依赖 SideBar 具体实现
class ISideFileBar
{
public:
    virtual ~ISideFileBar() = default;

    /// 刷新文件列表
    virtual void refreshFileList() = 0;

    /// 设置面板宽度
    virtual void setPanelWidth(int width) = 0;
};

#endif // ISIDEFILEBAR_H
