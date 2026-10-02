#ifndef MERMAIDRENDERER_H
#define MERMAIDRENDERER_H

#include <QObject>
#include <QHash>
#include <QByteArray>
#include <QString>
#include <QRecursiveMutex>

/// @brief Mermaid 图表渲染器（P3-M02 子项5）
///
/// 设计目标：
/// - 不集成 QWebEngineView（避免依赖体积庞大的 Chromium 内核）
/// - 改用 mermaid-cli（mmdc）外部命令渲染 mermaid 代码块为 SVG
/// - 带内存缓存（QHash<源码哈希, SVG 字节>），避免重复调用 mmdc
///
/// 使用流程：
///   1. MaddyParser 解析 Markdown 后，识别 `language-mermaid` 代码块
///   2. 调用 MermaidRenderer::isAvailable() 检查 mmdc 是否可用
///   3. 调用 renderToSvg(mermaidCode) 获取 SVG 字节流
///   4. 成功则嵌入 <div class="mermaid">SVG</div>，失败则保留原代码 + 错误提示
///
/// mmdc 调用约定：
///   mmdc -i <input.mmd> -o <output.svg> -t <theme> -b transparent
///   - 输入/输出通过临时文件传递（避免 stdin 管道的跨平台编码问题）
///   - 主题随 ThemeManager 切换（dark/default）
///   - 超时 15 秒（防止 hang）
///
/// 缓存策略：
/// - 内存缓存键：mermaid 源码的 SHA-1 哈希（hex）
/// - 缓存值：SVG 字节流
/// - 缓存上限：100 条（避免内存膨胀；超出按 FIFO 淘汰，由 QHash 自然顺序）
/// - 缓存进程内有效，重启清空（mermaid 源码通常较少重复，无需持久化）
class MermaidRenderer
{
public:
    /// 检测 mmdc 命令是否可用（PATH 中可执行 + --version 成功）
    /// 结果会缓存，避免每次渲染都 fork 进程
    static bool isAvailable();

    /// @brief 将 mermaid 源码渲染为 SVG
    /// @param mermaidCode mermaid 图表源码（如 "graph TD\n  A-->B"）
    /// @return SVG 字节流（UTF-8）；失败返回空 QByteArray
    /// @note 命中缓存时直接返回；未命中则调用 mmdc 渲染并写入缓存
    static QByteArray renderToSvg(const QString& mermaidCode);

    /// 清空内存缓存（用于主题切换后强制重渲染）
    static void clearCache();

private:
    MermaidRenderer() = delete;  // 纯静态工具类，禁止实例化

    /// 调用 mmdc 渲染（无缓存检查，直接执行外部进程）
    static QByteArray renderViaMmdc(const QString& mermaidCode);

    /// 计算源码的缓存键（SHA-1 哈希 hex）
    static QString cacheKey(const QString& mermaidCode);

    /// 根据当前主题返回 mmdc 主题参数（"dark" / "default"）
    static QString currentMmdcTheme();

    /// 创建临时输入文件（.mmd），返回文件路径；失败返回空字符串
    static QString writeTempInput(const QString& content);

    /// 创建临时输出文件路径（.svg），返回路径（文件不预先创建）
    static QString tempOutputPath(const QString& cacheKey);

    /// 检测 mmdc 可执行文件路径（缓存结果）
    /// 优先级：环境变量 MERMAID_CLI_PATH > PATH 中的 mmdc > npm 全局安装路径
    static QString findMmdcExecutable();

    // === 静态状态 ===
    static bool          s_availableChecked;   ///< 是否已检测过可用性
    static bool          s_available;          ///< 上次检测结果
    static QHash<QString, QByteArray> s_cache; ///< 内存缓存（key=SHA-1 hex, value=SVG）
    static QRecursiveMutex s_mutex;            ///< 递归互斥锁（renderToSvg 内部调用 isAvailable/renderViaMmdc 需可重入）
    static QString       s_mmdcPath;           ///< 缓存的 mmdc 可执行路径
    static int           s_cacheHits;          ///< 缓存命中计数（调试用）
};

#endif // MERMAIDRENDERER_H
