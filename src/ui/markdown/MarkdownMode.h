#ifndef MARKDOWNMODE_H
#define MARKDOWNMODE_H

#include <QWidget>
#include <QSplitter>
#include <QMap>

#include "interfaces/ui/IMarkdownViewer.h"

class MyTextEdit;
class QTextBrowser;
class QTreeWidget;
class QToolBar;
class IMarkdownParser;

/// @brief Markdown 编辑+预览分屏模式（V1.7 增强版）
/// 左侧源码编辑，右侧实时HTML渲染预览
/// 通过 IMarkdownParser 接口使用解析器（默认 maddy 三方库）
///
/// V1.7 新增功能：
/// - TOC目录面板（可视化导航+点击跳转）
/// - 图片灯箱预览（模态窗口+缩放20%-500%）
/// - 导出PDF/HTML功能（一键导出+格式选择）
/// - 编辑器与预览区滚动同步
/// - 自适应防抖策略（150-500ms根据文档大小动态调整）
class MarkdownMode : public QWidget, public IMarkdownViewer
{
    Q_OBJECT

public:
    explicit MarkdownMode(QWidget* parent = nullptr);

    /// 获取左侧编辑器
    MyTextEdit* editor() const { return m_editor; }

    /// 设置编辑器内容
    void setContent(const QString& text) override;

    /// 获取编辑器内容
    QString content() const override;

    /// 设置 Markdown 解析器（默认使用 maddy）
    void setParser(IMarkdownParser* parser);

    /// 刷新预览
    void refreshPreview() override;

    /// @brief 跳转到指定标题（TOC导航）
    void scrollToHeading(const QString& anchorId);

    /// @brief 显示导出对话框
    void showExportDialog();

    /// @brief 获取TOC面板（供外部使用）
    class MdTocPanel* tocPanel() const { return m_tocPanel; }

    /// @brief 设置当前文件路径（用于 TOC 折叠状态按文件记忆）
    void setFilePath(const QString& filePath);

    /// @brief 获取当前文件路径
    QString filePath() const { return m_filePath; }

    /// @brief 复制当前 Markdown 为富文本到剪贴板（P3-M02 子项4）
    void copyAsRichText();

signals:
    void contentChanged();

private slots:
    /// @brief 编辑器滚动时同步预览区滚动位置
    void onEditorScrolled(int verticalValue);

    /// @brief 预览区滚动时反向同步编辑器滚动位置 (双向滚动同步)
    void onPreviewScrolled(int verticalValue);

    /// @brief TOC项点击跳转
    void onTocItemClicked(const QString& anchorId, int lineNumber);

    /// @brief 主题切换时完整刷新（预览+工具栏+TOC面板样式）
    void onThemeChanged();

private:
    // 主布局组件
    QSplitter*        m_mainSplitter;     ///< 主分割器（左侧：编辑器+TOC | 右侧：预览）
    QSplitter*        m_leftSplitter;     ///< 左侧分割器（编辑器 | TOC面板）
    MyTextEdit*       m_editor;           ///< 源码编辑器
    QTextBrowser*     m_preview;          ///< HTML预览区
    MdTocPanel*       m_tocPanel = nullptr; ///< 目录树面板
    QToolBar*         m_toolbar = nullptr;  ///< 工具栏（导出按钮等）

    // 解析器
    IMarkdownParser*  m_parser = nullptr;

    // 滚动同步防循环标志 (避免编辑器<->预览互相触发死循环)
    bool m_syncingScroll = false;

    // P3-M02 子项1: 当前文件路径（用于 TOC 折叠状态按文件记忆）
    QString m_filePath;

    // D5: TOC数据结构
    struct TocItem {
        int level;
        QString title;
        QString anchorId;
        int lineNum;  // 在编辑器中的行号
    };
    QList<TocItem> m_tocItems;  ///< 目录项列表

    /// @brief 解析Markdown提取标题列表（用于TOC）
    void parseToc();

    /// @brief 计算滚动比例（用于同步）
    double getScrollRatio() const;

    /// @brief 创建工具栏（导出按钮等）
    void setupToolbar();

    /// @brief 仅刷新工具栏样式（不重建控件，避免布局断裂）
    void applyToolbarStyle();

    /// @brief 处理预览区图片点击事件（调用ImageLightBox）
    bool eventFilter(QObject* obj, QEvent* event) override;

    // === P3-M02 子项2+3: CSS 预设与用户自定义 CSS ===
    /// @brief 暗色主题 CSS 预设（深色背景 + 浅色文字 + 链接 + 代码块）
    static QString darkCssPreset();
    /// @brief 浅色主题 CSS 预设（浅色背景 + 深色文字）
    static QString lightCssPreset();
    /// @brief 构建预览区 CSS：主题预设 + 用户自定义 CSS（叠加在预设之上）
    QString buildPreviewCss() const;
    /// @brief 应用 CSS 到预览区 QTextDocument
    void applyPreviewCss();
};

#endif // MARKDOWNMODE_H