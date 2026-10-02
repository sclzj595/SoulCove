#ifndef EDITORTABBAR_H
#define EDITORTABBAR_H

#include <QWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <QTimer>
#include <QSet>

#include "interfaces/ui/ITabWidget.h"

class MyTextEdit;
class ICompleter;
class IEditorEdit;

/// @brief 编辑器标签页数据结构
struct TabData {
    QString filePath;       // 文件完整路径
    QString displayName;    // 显示名称（文件名或"未命名"）
    bool isModified = false; // 是否有未保存修改
    bool isSpecial = false;  // 特殊标签页（设置/MD预览等，不可编辑）
    MyTextEdit* editor = nullptr;  // 该标签页对应的编辑器实例
    QWidget*    customWidget = nullptr; // 自定义Widget（设置页/MD分屏等）
    // R4: 闲置检测已提取到 IdleTabTracker，TabData 不再包含 LSP 相关字段
};

/// @brief VSCode风格编辑器标签页栏组件
/// 管理多个文件标签页，每个标签页持有独立的MyTextEdit编辑器实例
/// 支持：新建/打开/关闭标签页、修改标记(*)、切换标签、关闭前保存检查
class EditorTabBar : public QWidget, public ITabWidget
{
    Q_OBJECT

public:
    explicit EditorTabBar(QWidget* parent = nullptr);

    /// @brief 新建空白标签页
    void addNewTab() override;

    /// @brief 打开文件并创建标签页（若已存在则切换到该标签）
    /// @param filePath 文件路径
    /// @param content 文件内容（由FileOperator提供）
    void openFileTab(const QString& filePath, const QString& content) override;

    /// @brief 添加自定义Widget标签页（设置页/MD预览等）
    /// @param widget 自定义Widget
    /// @param title 标签页标题
    /// @param closable 是否可关闭
    /// @return 标签页索引
    int addCustomTab(QWidget* widget, const QString& title, bool closable = true);

    /// @brief 查找自定义标签页索引（按标题）
    /// @return 找到返回索引，未找到返回 -1
    int findCustomTabIndex(const QString& title) const;

    /// @brief 获取所有已打开的编辑器实例（用于全局应用配置，如字体大小）
    /// @return 编辑器指针列表（仅包含 MyTextEdit，不含自定义 Widget 标签）
    /// R3: 重命名避免与 ITabWidget::allEditors() 接口冲突
    QList<MyTextEdit*> allMyTextEditors() const;

    /// @brief 添加Markdown分屏标签页
    /// @param filePath 文件路径
    /// @param content 文件内容
    void openMarkdownTab(const QString& filePath, const QString& content);

    /// @brief 添加 HTML 预览分屏标签页（V1.9 新增）
    /// @param filePath 文件路径
    /// @param content 文件内容
    void openHtmlPreviewTab(const QString& filePath, const QString& content);

    /// @brief 关闭当前标签页（带保存检查）
    /// @return 是否成功关闭（用户取消则返回false）
    bool closeCurrentTab() override;

    /// @brief 关闭指定索引的标签页
    bool closeTab(int index);

    /// @brief 切换到指定标签页
    void switchToTab(int index);

    /// @brief 获取当前活跃的编辑器（通过接口返回，解耦具体实现）
    IEditorEdit* currentEditor() const override;

    /// @brief 获取当前标签页索引
    int currentIndex() const { return m_tabBar->currentIndex(); }

    /// @brief 获取标签页数量
    int tabCount() const override { return m_tabBar->count(); }

    /// @brief 当前是否有修改
    bool isCurrentModified() const override;

    /// @brief 设置当前标签页的修改状态
    void setCurrentModified(bool modified) override;

    /// @brief 获取当前文件路径
    QString currentFilePath() const override;

    /// @brief 设置当前标签页的文件路径（另存为后更新）
    void setCurrentFilePath(const QString& path) override;

    /// R3: 获取所有已打开标签页的 (filePath, editor) 列表
    /// 实现 ITabWidget 接口，供 LspCoordinator 遍历编辑器进行状态路由
    QList<QPair<QString, IEditorEdit*>> allEditors() const override;

    /// @brief 获取当前标签页数据
    const TabData* currentTabData() const;

    /// @brief 获取指定索引的标签页数据（用于遍历所有标签页，如 LSP 诊断分发）
    /// @param index 标签页索引
    /// @return 标签页数据指针（索引无效返回 nullptr）
    const TabData* tabDataAt(int index) const;

    /// @brief V1.9: 按文件路径查找标签页索引（用于文件移动后更新路径）
    /// @return 找到返回索引，未找到返回 -1
    int findTabByFilePath(const QString& filePath) const;

    /// @brief V1.9: 更新指定标签页的文件路径（文件移动/重命名后调用）
    void updateTabFilePath(int index, const QString& newPath);

    /// @brief 刷新所有编辑器（主题切换后行号区重绘等）
    void refreshAllEditors();

signals:
    /// @brief 当前编辑器变更信号（用于重新连接补全器等）
    void currentEditorChanged(MyTextEdit* editor);

    /// @brief 标签页数量变化信号
    void tabCountChanged(int count);

    /// @brief 所有标签页关闭完毕
    void allTabsClosed();

    /// @brief 自定义Widget标签页被销毁（通知外部置空悬空指针）
    void customTabDestroyed(const QString& title);

    /// @brief 请求保存指定编辑器（关闭标签时用户选择保存）
    void saveRequested(MyTextEdit* editor);

    /// @brief 文件标签页关闭（通知 LSP 发送 didClose 释放文档）
    void fileClosed(const QString& filePath);

    // R4: 闲置检测信号已移到 IdleTabTracker（fileIdleTimeout/fileReactivated）

private slots:
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onEditorModified(bool modified);

private:
    /// @brief 创建新的MyTextEdit编辑器实例
    MyTextEdit* createEditor();

    /// @brief 重新映射标签数据索引（tab移除后索引变化）
    void remapTabData();

    /// @brief 更新标签页显示文本（添加*号等）
    void updateTabText(int index, const TabData& data);

    // === 标签页拖出独立窗口（V1.9）===
    bool eventFilter(QObject* obj, QEvent* event) override;
    void detachTabToWindow(int index);

    QTabBar* m_tabBar;
    QStackedWidget* m_editorStack;   // 编辑器堆栈，每个tab对应一个editor
    QMap<int, TabData> m_tabDataMap;  // 索引→标签数据
    int m_untitledCounter = 0;        // 未命名文件计数器

    // R4: 闲置检测已提取到 IdleTabTracker，EditorTabBar 不再持有定时器

    // 拖拽状态
    bool m_dragging = false;           // 是否正在拖拽标签
    int m_dragTabIndex = -1;           // 被拖拽的标签索引
    QPoint m_dragStartPos;             // 拖拽起始位置
};

#endif // EDITORTABBAR_H
