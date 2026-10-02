#pragma once

#include <QString>

/// @file LspTypes.h
/// @brief LSP 公共类型定义（跨层共享，避免循环依赖）
///
/// 本头文件不依赖任何 Qt 重型模块（仅 QtCore），可被 core/lsp、core/editor、
/// ui/shell、controller 等各层安全包含。
/// 设计原则：将跨层共享的枚举/结构体提取到公共头文件，消除反向依赖。

/// @brief LSP 服务器高亮状态（双轨高亮降级架构核心枚举）
///
/// 高亮器根据此状态决定是否启用启发式兜底规则：
/// - Ready: LSP 语义高亮就绪，禁用启发式避免冲突
/// - 非 Ready: 启用启发式兜底（PascalCase→类型, m_→成员, ALL_CAPS→常量）
///
/// 信号链路使用此枚举而非 int，保证类型安全，消除 int→enum 映射代码。
enum class LspHighlightState {
    NotStarted = 0,     ///< 未启动 LSP（纯静态高亮）
    Initializing = 1,   ///< LSP 启动中（静态高亮 + 启发式兜底）
    Ready = 2,          ///< LSP 就绪（静态高亮 + 语义高亮，禁用启发式）
    Disconnected = 3    ///< LSP 断开/崩溃（静态高亮 + 启发式兜底）
};
