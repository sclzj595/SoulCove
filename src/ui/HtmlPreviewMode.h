#ifndef HTMLPREVIEWMODE_H
#define HTMLPREVIEWMODE_H

#include <QWidget>
#include <QSplitter>
#include <QTimer>

class MyTextEdit;
class QTextBrowser;
class QToolBar;
class QAction;
class QActionGroup;

/// @brief HTML 编辑+预览模式（V1.9 新增）
///
/// 三种视图模式可切换：
/// - 源码模式：仅显示 HTML 源码编辑器
/// - 预览模式：仅显示 HTML 渲染预览
/// - 分屏模式：左侧源码 + 右侧预览（默认）
///
/// 预览特性：
/// - 解析 <link rel="stylesheet"> 引用的本地 CSS 文件并内联注入
/// - CSS 查找策略：同目录优先 → 工作目录递归查找
/// - 实时刷新（防抖 300ms）
/// - 主题感知预览背景
class HtmlPreviewMode : public QWidget
{
    Q_OBJECT

public:
    /// 视图模式枚举
    enum class ViewMode {
        SourceOnly,   // 仅源码
        PreviewOnly,  // 仅预览
        SplitView     // 分屏（默认）
    };

    explicit HtmlPreviewMode(QWidget* parent = nullptr);

    /// 获取内部编辑器
    MyTextEdit* editor() const { return m_editor; }

    /// 设置编辑器内容
    void setContent(const QString& text);

    /// 获取编辑器内容
    QString content() const;

    /// 设置 HTML 文件路径（用于解析相对路径的 CSS 引用）
    void setFilePath(const QString& filePath) { m_filePath = filePath; }

    /// 设置工作目录（用于递归查找 CSS 文件）
    void setWorkDirectory(const QString& workDir) { m_workDir = workDir; }

    /// 刷新预览
    void refreshPreview();

    /// 切换视图模式
    void setViewMode(ViewMode mode);

signals:
    void contentChanged();

private slots:
    void onThemeChanged();

private:
    // 布局组件
    QSplitter*      m_mainSplitter = nullptr;
    MyTextEdit*     m_editor = nullptr;
    QTextBrowser*   m_preview = nullptr;
    QToolBar*       m_toolbar = nullptr;

    // 工具栏动作
    QAction*        m_actSource = nullptr;
    QAction*        m_actPreview = nullptr;
    QAction*        m_actSplit = nullptr;
    QAction*        m_actRefresh = nullptr;

    // 状态
    ViewMode        m_viewMode = ViewMode::SplitView;
    QString         m_filePath;        // 当前 HTML 文件路径
    QString         m_workDir;         // 工作目录
    QTimer          m_debounceTimer;   // 防抖定时器

    /// 初始化 UI
    void setupUi();

    /// 初始化工具栏
    void setupToolbar();

    /// 应用主题样式
    void applyThemeStyle();

    /// 根据视图模式更新控件可见性
    void updateViewVisibility();
};

#endif // HTMLPREVIEWMODE_H
