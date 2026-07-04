# SoulCove 现代化 Qt 编辑器 — 开发进度文档（V1.8）

> 最后更新：2026-06-19 | 窗口8方向缩放修复 + Ctrl+滚轮字体缩放联动 + 右键菜单增强 + 字体全局同步架构 | 编译通过 ✅

---

## 一、需求完成度总览

### 已完成 ✅（61+项）

#### V1.0-V1.7 已完成功能（55项）

| # | 需求项 | 阶段 | 备注 |
|---|--------|------|------|
| 1-55 | V1.7 及之前所有功能 | 1-10 | 见 V1.7 文档 |

#### V1.8 新增已完成功能（6项）

| # | 需求项 | 阶段 | 备注 |
|---|--------|------|------|
| **56** | **窗口 8 方向缩放修复** | **11** | **WS_THICKFRAME + WM_NCCALCSIZE + WM_NCHITTEST，原生缩放循环** |
| **57** | **Ctrl+滚轮字体缩放** | **11** | **标准 modifiers 检测，持久化到 ConfigManager** |
| **58** | **字体全局同步架构** | **11** | **ConfigManager 单一数据源 + configChanged 广播 + QSignalBlocker 防递归** |
| **59** | **右键菜单增强（5项）** | **11** | **切换行注释/转大写/转小写/复制文件路径/在文件管理器中打开** |
| **60** | **TextCompleter 字体联动** | **11** | **监听 configChanged 刷新字体，移除硬编码 13px** |
| **61** | **MarkdownMode 字体联动** | **11** | **构造函数应用配置字体 + 监听 configChanged 同步** |

### 待完成 🔄

| # | 需求项 | 优先级 | 说明 |
|---|--------|--------|------|
| T19 | SFTP 远程文件编辑集成 | 中 | RemoteFileTree → SshConfigPanel 联动 → 标签页远程文件编辑 |
| T20 | 断线重连机制 | 中 | SSH 断连检测 + 指数退避重连 |
| T21 | 终端分屏 (T6) | 低 | 水平分割多终端 |
| T22 | 扩展/插件系统 (T15) | 低 | 第三方功能扩展接口 |
| T23 | 文件同步 | 低 | 本地↔远程双向同步 |

---

## 二、V1.8 本次迭代变更详情

### 2.1 窗口 8 方向缩放修复（核心修复）

**修改文件**: `src/ui/FramelessWindow.h/cpp`

#### 问题根因

`Qt::FramelessWindowHint` 创建的是 `WS_POPUP` 窗口，没有 `WS_THICKFRAME` 样式。没有 `WS_THICKFRAME`，即使 `WM_NCHITTEST` 返回 `HTLEFT/HTRIGHT` 等，Windows 也不会启动缩放循环（`DefWindowProc` 直接忽略 hit-test 结果）。

#### 解决方案

**三步走**：手动添加 `WS_THICKFRAME` → `WM_NCCALCSIZE` 裁剪边框 → `WM_NCHITTEST` 边缘检测

```cpp
// 1. initWindowAttributes() 中添加 WS_THICKFRAME
HWND hwnd = reinterpret_cast<HWND>(winId());
LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_THICKFRAME);

// 2. nativeEvent 中处理 WM_NCCALCSIZE — 裁剪 WS_THICKFRAME 添加的边框
if (msg->message == WM_NCCALCSIZE) {
    if (msg->wParam == TRUE) {
        *result = 0;  // 返回 0 表示使用整个客户区
        return true;
    }
}

// 3. nativeEvent 中处理 WM_NCHITTEST — 8 方向边缘检测
if (msg->message == WM_NCHITTEST) {
    // 计算 left/right/top/bottom 边缘
    // 返回 HTTOPLEFT/HTTOPRIGHT/HTBOTTOMLEFT/HTBOTTOMRIGHT/HTLEFT/HTRIGHT/HTTOP/HTBOTTOM
}
```

#### 清理

- 移除死代码：`testEdge()`, `updateCursorShape()`, `m_isResizing`, `m_activeEdge`, `Edge` 枚举
- 简化为仅保留拖拽状态（`m_isDragging`, `m_dragPos`）

### 2.2 Ctrl+滚轮字体缩放 + 设置页双向联动

**修改文件**: `src/mytextedit.h/cpp`, `src/widget.cpp`, `src/ui/SettingsPage.cpp`

#### wheelEvent 修复

**问题**：原实现使用成员变量 `ctrlKeyPressed` 跟踪 Ctrl 状态，焦点丢失时标志位无法复位，导致缩放不可靠。

**修复**：改用标准 `event->modifiers() & Qt::ControlModifier`，移除 `ctrlKeyPressed` 成员变量。

```cpp
void MyTextEdit::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0) fontZoomIn();
        else if (event->angleDelta().y() < 0) fontZoomOut();
        event->accept();
    } else {
        QTextEdit::wheelEvent(event);
    }
}
```

#### 持久化 + 信号发射

`fontZoomIn/fontZoomOut` 中调用 `ConfigManager::instance().setFontSize()` 持久化，并发射 `fontSizeChanged` 信号。

#### 设置页双向联动

SettingsPage 构造函数中监听 `ConfigManager::configChanged`，当 `Display/fontSize` 变化时用 `QSignalBlocker` 更新 SpinBox（防止回环）。

### 2.3 字体全局同步架构

**修改文件**: `src/widget.cpp`, `src/ui/EditorTabBar.h/cpp`

#### 设计原则

**ConfigManager 作为单一数据源**，所有字体变更走配置中心，通过 `configChanged` 信号广播。

#### 数据流

```
Ctrl+滚轮 → MyTextEdit::fontZoomIn
         → ConfigManager::setFontSize (持久化)
         → ConfigManager::configChanged("Display/fontSize")
         → Widget::lambda 遍历 allEditors() 同步 (QSignalBlocker 防递归)
         → SettingsPage 更新 SpinBox (QSignalBlocker 防回环)
         → TextCompleter::applyFontSize
         → MarkdownMode 更新编辑器字体
```

#### EditorTabBar::allEditors()

新增方法，遍历 `m_tabDataMap` 收集所有编辑器指针，用于全局字体同步。

```cpp
QList<MyTextEdit*> EditorTabBar::allEditors() const
{
    QList<MyTextEdit*> editors;
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().editor) editors.append(it.value().editor);
    }
    return editors;
}
```

### 2.4 右键菜单增强（5 个新动作）

**修改文件**: `src/mytextedit.h/cpp`, `src/widget.h/cpp`, `src/core/ShortcutFilter.cpp`(注册)

#### 新增信号（mytextedit.h）

```cpp
void fontSizeChanged(int size);
void copyFilePathRequested();
void openInFolderRequested();
void toggleLineCommentRequested();
void toUpperCaseRequested();
void toLowerCaseRequested();
```

#### 新增槽函数（widget.h/cpp）

| 槽函数 | 功能 | 快捷键 |
|--------|------|--------|
| `onCopyFilePath` | 复制当前文件路径到剪贴板（原生路径分隔符） | Ctrl+Shift+C |
| `onOpenInFolder` | 用 `QDesktopServices::openUrl` 打开所在目录 | — |
| `onToggleLineComment` | 切换行注释（根据后缀选 `#`/`--`/`//`，支持多行） | Ctrl+/ |
| `onToUpperCase` | 选中文本转大写 | — |
| `onToLowerCase` | 选中文本转小写 | — |

#### 注释符号智能选择

| 后缀 | 注释符号 |
|------|---------|
| py/sh/yaml/yml/rb/pl/r/conf/toml/ini/properties/dockerfile/makefile/cmake | `#` |
| sql | `--` |
| 其他（C/C++/Java/JS/Go/Rust/PHP 等） | `//` |

#### 多行注释逻辑

1. 无选择：切换当前行
2. 多行：先检查所有非空行是否都已注释
   - 全部已注释 → 取消注释
   - 否则 → 添加注释
3. 空行跳过，`beginEditBlock/endEditBlock` 保证撤销一致性

### 2.5 TextCompleter 字体联动

**修改文件**: `src/textCompleter.h/cpp`

- 移除构造函数中硬编码的 `f.setPointSize(13)`
- 移除 `applyTheme()` 样式表中的 `font-size: 13px`
- 新增 `applyFontSize()` 方法，从 `ConfigManager::fontSize()` 读取
- 监听 `configChanged` 信号，`Display/fontSize` 变化时自动刷新

### 2.6 MarkdownMode 字体联动

**修改文件**: `src/ui/MarkdownMode.cpp`

- 构造函数中调用 `m_editor->setFontSize(ConfigManager::instance().fontSize())`
- 监听 `configChanged` 信号，`QSignalBlocker` 防递归同步

### 2.7 ShortcutFilter + 命令面板新命令

**修改文件**: `src/widget.cpp`

#### ShortcutFilter 注册（2 个新命令）

| 命令 ID | 名称 | 快捷键 | 上下文 |
|---------|------|--------|--------|
| `edit.comment` | 切换行注释 | Ctrl+/ | editor |
| `edit.copyPath` | 复制文件路径 | Ctrl+Shift+C | editor |

#### 命令面板注册（6 个新命令）

| 命令 ID | 名称 | 快捷键 |
|---------|------|--------|
| `edit.doxygen` | 生成注释 | Ctrl+Shift+D |
| `edit.comment` | 切换行注释 | Ctrl+/ |
| `edit.copyPath` | 复制文件路径 | Ctrl+Shift+C |
| `edit.upper` | 转换为大写 | — |
| `edit.lower` | 转换为小写 | — |
| `edit.openFolder` | 在文件管理器中打开 | — |

### 2.8 行号区字体跟随编辑器

**修改文件**: `src/mytextedit.cpp`

原硬编码 `font.setPointSize(12)`，改为跟随编辑器字体（比编辑器小 1pt）：

```cpp
QFont font = this->font();
int editorSize = font.pointSize();
font.setPointSize(editorSize > 2 ? editorSize - 1 : editorSize);
painter.setFont(font);
```

---

## 三、架构约束遵守情况

本次迭代所有改动严格遵循原有接口设计和设计模式：

### 接口设计

| 接口 | 本次改动 | 合规性 |
|------|---------|--------|
| IEditorEdit | `setFontSize/fontSize/fontZoomIn/fontZoomOut` 已有纯虚方法 | ✅ 未新增接口方法 |
| IFramelessWindow | 未修改接口 | ✅ |
| IConfigManager | 使用已有 `fontSize()/setFontSize()/configChanged` | ✅ |
| ICompleter | 未修改接口，仅内部实现 `applyFontSize()` | ✅ |
| ITabWidget | 新增 `allEditors()` 非接口方法（具体类方法） | ✅ 不影响接口 |

### 设计模式

| 模式 | 本次应用 | 合规性 |
|------|---------|--------|
| 单例模式 | ConfigManager 作为单一数据源 | ✅ |
| 观察者模式 | configChanged 信号广播字体变更 | ✅ |
| 命令模式 | ShortcutFilter 注册 edit.comment/edit.copyPath | ✅ |
| 信号槽解耦 | MyTextEdit 发射信号 → Widget 槽处理 | ✅ 视图/逻辑分离 |
| QSignalBlocker | 防止字体同步递归/回环 | ✅ |

### 模块化

- TextCompleter 自行监听 configChanged，不依赖 Widget 转发
- MarkdownMode 自行监听 configChanged，不依赖 Widget 转发
- SettingsPage 自行监听 configChanged，不依赖 Widget 转发
- Widget 仅负责编辑器实例的同步（通过 allEditors()）

---

## 四、接口清单（src/interfaces/）

| 接口 | 实现类 | 状态 |
|------|--------|------|
| IConfigManager | ConfigManager | ✅ 字体单一数据源 |
| IFileOperator | FileOperator | ✅ |
| IMarkdownParser | MaddyParser / MarkdownParser | ✅ |
| IEditorEdit | MyTextEdit | ✅ +fontSizeChanged 信号 |
| ICompleter | TextCompleter | ✅ +applyFontSize 联动 |
| ILineNumber | LineNumberArea | ✅ |
| IObserver / ISubject | Widget / Subject | ✅ |
| IThemeManager | ThemeManager | ✅ |
| IFramelessWindow | FramelessWindow | ✅ +8方向缩放 |
| ITabWidget | EditorTabBar | ✅ +allEditors() |
| ISideFileBar | SideBar | ✅ |
| IMarkdownViewer | MarkdownMode | ✅ +字体联动 |
| ISyntaxHighlighter | CodeSyntaxHighlighter | ✅ |
| ITerminalWidget | EmbeddedTerminal | ✅ |
| ISshClient | SshClient | ✅ +SftpClient 扩展 |
| IUiLibrary | DefaultUiLibrary | ✅ |

---

## 五、版本变更记录

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| V1.0→V1.3 | 06月初 | 基础功能迭代 |
| V1.4 | 06-15 | 全量审计 |
| V1.5 | 06-16 | T1-T3 完成 |
| V1.6 | 06-16 | 终端模块化重构 + 全链路审查 |
| V1.7 | 06-18 | ModernDialog + PDF优化 + T8编码检测 + T18文件监听 + T17多光标 + SFTP + RemoteFileTree + P0-P3修复 |
| **V1.8** | **06-19** | **窗口8方向缩放修复 + Ctrl+滚轮字体缩放联动 + 右键菜单增强(5项) + 字体全局同步架构 + TextCompleter/MarkdownMode字体联动 + ShortcutFilter/命令面板新命令** |
