#ifndef PRODUCTCONFIG_H
#define PRODUCTCONFIG_H

#include <QString>

/// @brief 产品线配置（Factory Pattern — 配置驱动的组件创建）
///
/// 定义不同产品（notebook / editor / IDE）的功能开关，
/// Widget 根据此配置条件化创建/显示组件。
///
/// 设计要点：
/// - 纯数据结构，无行为，无依赖
/// - 通过静态工厂方法创建三种预设配置
/// - Widget 构造时注入，决定哪些子系统被初始化
///
/// 产品线迭代：
/// - Notebook: 纯文本编辑 + Markdown 预览（最小化）
/// - Editor:   Notebook + 文件树 + 搜索 + 格式化（中等）
/// - IDE:      Editor + LSP + 终端 + Git + 任务 + 大纲（完整）
struct ProductConfig
{
    // === 核心编辑 ===
    bool editor          = true;   // 文本编辑器（始终启用）
    bool markdownPreview = true;   // Markdown 预览
    bool codeFolding     = true;   // 代码折叠
    bool minimap         = true;   // 小地图
    bool doxygen         = true;   // Doxygen 注释生成

    // === 文件操作 ===
    bool fileOps         = true;   // 文件打开/保存/编码
    bool fileTree        = true;   // 文件资源管理器
    bool multiWorkspace  = true;   // 多文件夹工作区

    // === 搜索/导航 ===
    bool search          = true;   // 全局搜索
    bool outline         = true;   // 符号大纲
    bool commandPalette  = true;   // 命令面板

    // === LSP ===
    bool lsp             = true;   // 语言服务器（定义/悬停/引用/补全）
    bool completion      = true;   // 代码补全

    // === 集成工具 ===
    bool terminal        = true;   // 内嵌终端
    bool git             = true;   // Git 源代码管理
    bool tasks           = true;   // 任务系统

    // === 产品标识 ===
    QString productName  = QStringLiteral("IDE");

    /// @brief Notebook Lite 配置 — 纯文本编辑 + Markdown 预览
    static ProductConfig notebook()
    {
        ProductConfig c;
        c.editor          = true;
        c.markdownPreview = true;
        c.codeFolding     = false;
        c.minimap         = false;
        c.doxygen         = false;
        c.fileOps         = true;
        c.fileTree        = false;
        c.multiWorkspace  = false;
        c.search          = false;
        c.outline         = false;
        c.commandPalette  = false;
        c.lsp             = false;
        c.completion      = false;
        c.terminal        = false;
        c.git             = false;
        c.tasks           = false;
        c.productName     = QStringLiteral("SoulCove Notebook Lite");
        return c;
    }

    /// @brief Notebook 配置 — Notebook Lite + 文件树 + 搜索 + 格式化
    static ProductConfig codeEditor()
    {
        ProductConfig c;
        c.editor          = true;
        c.markdownPreview = true;
        c.codeFolding     = true;
        c.minimap         = true;
        c.doxygen         = true;
        c.fileOps         = true;
        c.fileTree        = true;
        c.multiWorkspace  = true;
        c.search          = true;
        c.outline         = false;
        c.commandPalette  = true;
        c.lsp             = false;
        c.completion      = false;
        c.terminal        = false;
        c.git             = false;
        c.tasks           = false;
        c.productName     = QStringLiteral("SoulCove Notebook");
        return c;
    }

    /// @brief IDE 配置 — 全功能（默认）
    static ProductConfig ide()
    {
        ProductConfig c;
        c.productName     = QStringLiteral("SoulCove IDE");
        return c;  // 所有功能默认启用
    }
};

#endif // PRODUCTCONFIG_H
