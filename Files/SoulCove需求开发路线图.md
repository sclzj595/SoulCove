# SoulCove 需求开发路线图

> 基于代码实际审查，梳理各需求实现状态与开发优先级，指导后续迭代。

---

## 一、开发阶段总览

| 阶段 | 目标 | 需求范围 | 前置条件 |
|------|------|----------|----------|
| P0 | 消除技术债，补全 LSP/补全/导航核心链路 | C01、C02、C03、C04 | 无 |
| P1 | 解决构建硬编码，打通多环境构建 | C05 | P0 完成 |
| P2 | 提升核心交互体验，贴近 VSCode | H01、H02、H03、H04、H05 | P1 完成 |
| P3 | 扩展功能边界，差异化体验 | M01-M05 | P2 完成 |
| P4 | 生态扩展与长期竞争力 | L01-L05 | P3 部分完成 |

---

## 二、P0 阶段：核心链路修复（必须做）

### C01 完善 LSP 链路稳定性

**现状**：部分实现。请求 ID 与路由机制已有基础，但 completion/definition/references 仍依赖 `LspManager::m_currentRequestFile`，存在并发覆盖风险。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | completion/definition/references 按 requestId 丢弃旧响应 | P0 | 在 `LspClient` 中为每个请求记录最新 requestId，响应回调中校验 id 是否匹配，不匹配则丢弃 | `LspClient::createRequest()` 已生成自增 ID；`m_pendingRequests` 已按 ID 路由（LspClient.cpp L128-L141、L490） |
| 2 | 通用请求超时机制（默认 5s） | P0 | 在 `createRequest()` 中启动 `QTimer::singleShot(5000)`，超时后从 `m_pendingRequests` 移除并发出超时信号 | 无，需新增 |
| 3 | Hover stale 丢弃模式推广到所有请求类型 | P0 | 将 `m_lastHoverRequestId` 模式（LspClient.cpp L320-L324）推广到 completion/definition/references | Hover 已实现（L588-L595） |

**涉及文件**：`src/core/lsp/LspClient.cpp/.h`、`src/core/lsp/LspManager.cpp/.h`

---

### C02 主线程性能优化落地

**现状**：未实现。无性能监控面板，编辑器仍为全量重绘。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | 正则复用：编译一次多次执行 | P0 | 将 `QRegularExpression` 提取为类成员或静态缓存，避免每次高亮重新编译 | 无，需重构高亮模块 |
| 2 | QImage 缓存：行号栏/图标避免重复创建 | P0 | 在 `MyTextEdit` 中缓存行号背景 QImage，仅在窗口 resize 时重建 | 无 |
| 3 | 增量高亮：仅重绘变化行而非全文 | P0 | 记录上次高亮行范围，编辑时只对变化行 ±N 行重新高亮 | 无 |
| 4 | 性能监控面板（调试模式） | P1 | 新增 `PerformanceMonitor` 类，用 `QElapsedTimer` 记录各操作耗时，通过 `QDockWidget` 展示 | 无，需新增 |
| 5 | 编辑器增量渲染（仅重绘变化区域） | P1 | 在 `MyTextEdit::paintEvent` 中计算脏区域，仅重绘 `event->rect()` 范围 | 当前全量重绘 |

**涉及文件**：`src/ui/editor/MyTextEdit.cpp/.h`、新增 `src/core/debug/PerformanceMonitor.cpp/.h`

---

### C03 完善导航/跳转体验

**现状**：部分实现。仅支持回退（Ctrl+←），无前进、持久化、定义预览、Go to Implementation。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | 导航前进（Ctrl+→） | P0 | 新增 `m_navForwardStack`，回退时 push 到前进栈，跳转新位置时清空前进栈 | `m_navStack` 已实现（Widget.h L268-L274） |
| 2 | 导航栈清空 | P0 | 新增 `clearNavigationStack()` 方法 | 数据结构已有 |
| 3 | 导航栈持久化（重启恢复） | P1 | 在 `ConfigManager` 中序列化 `m_navStack`，启动时恢复 | `ConfigManager` 已有 JSON 持久化 |
| 4 | Go to Implementation（Ctrl+F12） | P0 | `LspClient` 新增 `requestImplementation()`，发送 `textDocument/implementation`，响应处理同 definition | `requestDefinition()` 可作模板 |
| 5 | 定义预览（Alt+Click 悬浮显示定义） | P1 | 新增 `DefinitionPreviewPopup`，Alt+Click 时发送 `textDocument/definition`，在悬浮窗口显示定义代码片段 | 无，需新增 |

**涉及文件**：`src/ui/shell/Widget.cpp/.h`、`src/core/lsp/LspClient.cpp/.h`

---

### C04 补全系统增强

**现状**：部分实现。双轨补全、键盘交互、tooltip 已有，但缺少分类标注、预览面板、LSP 缓存。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | 补全项分类标注（LSP/本地/片段图标区分） | P0 | 在 `TextCompleter` model 中根据 `RoleIsLsp` 和 snippet 来源设置不同图标/前缀标签 | `RoleIsLsp`/`RoleLspItem` 已有（TextCompleter.h L154-L155） |
| 2 | LSP 补全结果缓存 | P0 | 新增 `m_completionCache: QHash<QString, QList<CompletionItem>>`，同一文件未编辑时复用上次结果 | 文档单词缓存已有（TextCompleter.cpp L633-L704），可参照 |
| 3 | 补全项预览面板（显示完整签名/代码片段） | P1 | 新增 `CompletionPreviewWidget`，选中项变化时在侧边显示完整函数签名或 snippet 展开内容 | tooltip 已有简单预览（L734-L740） |

**涉及文件**：`src/ui/editor/TextCompleter.cpp/.h`、新增 `src/ui/editor/CompletionPreviewWidget.cpp/.h`

---

## 三、P1 阶段：构建配置修复（必须做）

### C05 解决构建配置硬编码问题

**现状**：未实现。CMakeLists.txt 仍硬编码 Qt/OpenSSL 绝对路径。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | CMakeLists.txt 改用 find_package + 环境变量 | P0 | 移除硬编码路径，改用 `find_package(Qt6 REQUIRED)` + `CMAKE_PREFIX_PATH` 环境变量 | 无 |
| 2 | 构建设置页（Qt 路径/编译器/构建类型） | P0 | 在 `SettingsPage` 新增构建配置 tab，支持配置 Qt 路径、编译器路径、Debug/Release | `SettingsPage` 已有多 tab 结构 |
| 3 | 自动检测系统已安装 Qt 版本 | P1 | 扫描注册表（Windows）或标准路径（Linux/Mac），提供下拉选择 | 无 |
| 4 | 多构建目录（Debug/Release 分离） | P1 | CMake 支持 `CMAKE_BUILD_TYPE` 切换，构建设置页提供切换选项 | 无 |

**涉及文件**：`CMakeLists.txt`、`src/ui/settings/SettingsPage.cpp/.h`

---

## 四、P2 阶段：核心交互体验提升（建议做）

### H01 终端功能增强

**现状**：部分实现。本地终端 + 多标签 + 搜索已有，缺联动、高亮、复用。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | 终端与编辑器联动（选中代码右键"在终端运行"） | P2 | `MyTextEdit` 右键菜单新增"在终端运行"，获取选中文本，通过 `TerminalBackend::write()` 发送 | `TerminalBackend` 已有 `write()` 方法 |
| 2 | 终端输出语法高亮（命令/错误/日志着色） | P2 | 在 `TerminalView::parseAnsiData()` 基础上，新增正则匹配错误/警告模式并着色 | ANSI 解析已有（TerminalView.cpp L49-L60） |
| 3 | 终端复用（不同标签页共享终端） | P3 | `EmbeddedTerminal` 支持将已有 `TerminalBackend` 实例绑定到新标签 | 架构上可扩展 |

**涉及文件**：`src/ui/terminal/EmbeddedTerminal.cpp/.h`、`src/ui/terminal/TerminalView.cpp/.h`、`src/ui/editor/MyTextEdit.cpp`

---

### H02 代码片段系统完善

**现状**：部分实现。CRUD、占位符、持久化已有，缺管理 UI、$SELECTION、VSCode 格式兼容。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | Snippet 管理 UI 页 | P2 | 新增 `SnippetManagerDialog`，列表/新增/编辑/删除，集成到设置页 | `SnippetManager` 后端完整 |
| 2 | $SELECTION 变量 | P2 | `expandSnippet()` 新增 `$SELECTION` 替换为编辑器当前选中内容 | `expandSnippet()` 已有（SnippetManager.cpp L138-L158） |
| 3 | VSCode snippet 格式导入导出 | P3 | 新增 `importFromVscode()` 方法，解析 VSCode JSON snippet 格式并转换为内部格式 | JSON 持久化已有 |

**涉及文件**：`src/core/snippet/SnippetManager.cpp/.h`、新增 `src/ui/snippet/SnippetManagerDialog.cpp/.h`

---

### H03 Git 集成增强

**现状**：部分实现。GitManager 核心操作完整，GitPanel 基础 UI 有，缺状态栏动态更新、行级标注、历史面板。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | Git 状态栏实时显示分支名/修改文件数 | P2 | `Widget::updateStatusBar()` 中调用 `GitManager::currentBranch()` + `fileStatuses().count()`，文件切换/保存时刷新 | `GitManager` API 完整；状态栏 label 已有占位 |
| 2 | 提交历史日志面板 | P2 | `GitPanel` 新增历史 tab，调用 `GitManager::log()` 展示，点击条目显示 diff | `GitManager::log()` 已实现 |
| 3 | 编辑器内行级 Git 标注 | P3 | `MyTextEdit` 左侧 gutter 区域绘制颜色条（绿=新增/蓝=修改/红=删除），需调用 `GitManager::diff()` 解析行级变更 | 无，需新增 gutter 渲染逻辑 |

**涉及文件**：`src/ui/shell/Widget.cpp`、`src/ui/sidebar/GitPanel.cpp/.h`、`src/ui/editor/MyTextEdit.cpp/.h`

---

### H04 多窗口/工作区支持

**现状**：部分实现。标签拖出独立窗口已有，缺 Workspace 概念。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | Workspace 概念 + .scnb-workspace 持久化 | P2 | 新增 `WorkspaceManager`，保存/恢复打开文件列表、侧边栏状态、分割布局；文件格式为 JSON | `ConfigManager` 已有 JSON 持久化可参照 |
| 2 | 多文件夹工作区 | P3 | `ExplorerPanel` 支持添加多个根文件夹，每个文件夹独立文件树 | 单文件夹文件树已有 |

**涉及文件**：新增 `src/core/workspace/WorkspaceManager.cpp/.h`、`src/ui/sidebar/ExplorerPanel.cpp/.h`

---

### H05 快捷键系统对标 VSCode

**现状**：部分实现。注册/修改/冲突检测/持久化已有，缺预设方案、搜索、录制。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | VSCode 快捷键预设方案一键切换 | P2 | 新增 `ShortcutPreset` 枚举，提供 Default/VSCode 两种预设，设置页一键切换 | `registerDefaults()` 已有默认方案（ShortcutManager.cpp L76-L250） |
| 2 | 快捷键搜索（按功能/按键） | P2 | 设置页新增搜索框，过滤快捷键列表 | 无 |
| 3 | 按键录制（按下组合键自动识别） | P2 | 新增 `KeySequenceEdit` 控件，覆写 `keyPressEvent` 捕获组合键 | 无 |
| 4 | 快捷键重置/导出/导入 | P3 | 重置调用 `registerDefaults()`；导出/导入为 JSON 文件 | JSON 持久化已有 |

**涉及文件**：`src/core/shortcut/ShortcutManager.cpp/.h`、`src/ui/settings/SettingsPage.cpp/.h`

---

## 五、P3 阶段：扩展功能边界（可以做）

### M01 远程开发增强

**现状**：SSH/SFTP/配置面板已有，缺远程工作区、LSP 部署、文件缓存、终端重连。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | 远程文件缓存（减少重复 SFTP 传输） | P3 | 新增 `RemoteFileCache`，基于文件路径+mtime 缓存内容，打开时先查缓存 | `SftpClient` 已有完整传输 API |
| 2 | 远程终端持久化（重连后恢复会话） | P3 | 使用 `tmux`/`screen` 包装远程 Shell，重连时 attach | `SshTerminalWidget` 已有 |
| 3 | 远程 LSP 自动部署 | P3 | 检测远程是否有 clangd，无则通过 SFTP 上传本地 clangd 二进制 | `SftpClient` + `LspManager` 已有 |
| 4 | 远程工作区挂载 | P3 | 新增 `RemoteWorkspaceManager`，将远程目录映射为本地临时目录 | `RemoteFileTree` + `SftpClient` 已有 |

**涉及文件**：`src/core/remote/` 目录下新增、`src/ui/remote/` 目录下新增

---

### M02 Markdown 功能升级

**现状**：分屏预览、大纲、双向滚动、导出已有，缺折叠、mermaid、CSS、暗黑适配、富文本复制。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | 大纲折叠 | P3 | `MdTocPanel` 树节点支持展开/折叠，默认全部展开 | `MdTocPanel` 树形结构已有 |
| 2 | Markdown 自定义 CSS | P3 | 新增设置项，预览区 `QTextBrowser::document().setDefaultStyleSheet()` 加载用户 CSS | 无 |
| 3 | 暗黑模式适配 | P3 | 根据当前主题切换预览区 CSS（暗色背景+浅色文字） | `ThemeManager` 已有主题切换 |
| 4 | 复制 Markdown 为富文本 | P3 | `MdExporter` 新增 `copyAsRichText()`，将 HTML 写入剪贴板 | `MdExporter` 已有 HTML 导出 |
| 5 | mermaid / PlantUML 支持 | P4 | 集成 mermaid.js（通过 QWebEngineView 渲染）或调用本地 mmdc 命令行 | 无，依赖较重 |

**涉及文件**：`src/ui/markdown/MarkdownMode.cpp/.h`、`src/ui/markdown/MdTocPanel.cpp/.h`、`src/core/markdown/MdExporter.cpp/.h`

---

### M03 编辑器体验精细化

**现状**：代码格式化、编码检测、状态栏指示器已有，缺 editorconfig、拼写检查、折叠增强、列选择。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | LF/CRLF 配置切换 | P3 | 状态栏 EOL label 改为可点击下拉框，切换时转换文件行尾 | 状态栏 EOL label 已有（Widget.cpp L519-L545） |
| 2 | 代码折叠增强（按区域/注释/自定义折叠） | P3 | `CodeFoldingManager` 新增 region/注释折叠规则解析 | `CodeFoldingManager` 已有基础折叠 |
| 3 | 列选择模式（Shift+Alt+拖拽） | P3 | `MyTextEdit` 覆写 `mousePressEvent/mouseMoveEvent`，Shift+Alt 时进入列选模式，绘制列选高亮 | 无 |
| 4 | .editorconfig 支持 | P4 | 新增 `EditorConfigParser`，打开文件时向上查找 `.editorconfig`，应用缩进/换行/编码设置 | 编码检测已有 |
| 5 | 拼写检查（hunspell） | P4 | 集成 hunspell 库，后台线程检查，波浪下划线标注 | 无，需引入第三方库 |

**涉及文件**：`src/ui/editor/MyTextEdit.cpp/.h`、`src/core/editor/CodeFoldingManager.cpp/.h`、新增 `src/core/editor/EditorConfigParser.cpp/.h`

---

### M04 任务/调试系统

**现状**：TaskManager + TasksPanel 基础功能已有，缺 tasks.json 兼容、调试、输出过滤。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | 输出面板过滤 + 错误跳转 | P3 | `TasksPanel` 输出区新增过滤输入框；正则匹配 `file:line` 格式，点击跳转编辑器 | `TasksPanel` 输出区已有 |
| 2 | 兼容 VSCode tasks.json | P4 | 新增 `TasksJsonParser`，解析 tasks.json 并转换为内部 Task 结构 | `TaskManager` 数据结构已有 |
| 3 | 基础调试（GDB/LLDB） | P4 | 新增 `DebugManager`，通过 `QProcess` 驱动 GDB MI 接口，实现断点/单步/变量查看 | 无，工作量大 |

**涉及文件**：`src/core/task/TaskManager.cpp/.h`、`src/ui/sidebar/TasksPanel.cpp/.h`、新增 `src/core/debug/DebugManager.cpp/.h`

---

### M05 国际化与本地化

**现状**：翻译文件骨架 + tr() 已有，缺运行时切换、区域设置、布局适配。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | 运行时切换语言 | P3 | 新增语言切换菜单，调用 `QTranslator::load()` + `qApp->installTranslator()`，刷新所有 UI | `QTranslator` 框架已有 |
| 2 | 区域设置（日期/时间/数字格式） | P4 | 使用 `QLocale` 根据语言设置格式 | Qt 内置支持 |
| 3 | UI 布局适配不同语言 | P4 | 关键控件设置最小宽度，长文本使用省略号或换行 | 无 |

**涉及文件**：`src/core/config/ConfigManager.cpp/.h`、`src/ui/shell/Widget.cpp`

---

## 六、P4 阶段：生态扩展（可选做）

### L01 插件系统

**现状**：未实现。

| 序号 | 待开发子项 | 优先级 | 实现建议 |
|------|-----------|--------|----------|
| 1 | 轻量插件架构（基于 Qt 插件系统） | P4 | 定义 `ScnbPluginInterface`（QPluginLoader），提供编辑器/UI/命令/快捷键 API |
| 2 | 插件市场（本地文件夹 + HTTP 下载） | P4 | 新增 `PluginManager` + `PluginStoreDialog` |
| 3 | 插件开发 SDK | P4 | 提供 `scnb-plugin-sdk.pri`，含接口头文件和示例 |
| 4 | 首批插件：主题/语言高亮/工具 | P4 | 以插件形式重构现有主题和语法高亮 |

---

### L02 云同步/配置备份

**现状**：未实现。

| 序号 | 待开发子项 | 优先级 | 实现建议 |
|------|-----------|--------|----------|
| 1 | 配置同步到本地文件夹/云存储 | P4 | 新增 `ConfigSyncManager`，支持 OneDrive/自定义路径，监听文件变化 |
| 2 | 配置快照 | P4 | 保存/恢复配置版本，基于时间戳 |
| 3 | 多设备增量同步 | P4 | 基于文件 mtime 的增量更新 |

---

### L03 多语言 LSP 完善

**现状**：LanguageRegistry 已注册 7 种语言，LspManager 支持 clangd/pylsp/ts-langserver 启动。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | LSP 自动安装提示 | P4 | 打开对应文件类型时检测 LSP 是否可用，不可用则弹窗提示安装 | `LanguageRegistry` 已有语言映射 |
| 2 | LSP 自定义参数配置 | P4 | `LspManager` 新增 per-language 启动参数配置，保存到 ConfigManager | `LspManager` 启动逻辑已有 |

**涉及文件**：`src/core/lsp/LspManager.cpp/.h`、`src/core/lsp/LanguageRegistry.cpp/.h`

---

### L04 工具面板增强

**现状**：RegexTester/JsonValidator/SqliteBrowser/DiffViewer/ImagePreviewer 已有基础功能。

| 序号 | 待开发子项 | 优先级 | 实现建议 | 已有基础 |
|------|-----------|--------|----------|----------|
| 1 | RegexTester 替换预览 | P4 | 新增替换输入框，实时显示替换结果 | `RegexTester` 已有匹配功能 |
| 2 | JSON Schema 校验 | P4 | 集成 `json-schema-validator` 库 | `JsonValidator` 已有校验基础 |
| 3 | SQL 语法高亮/结果导出 | P4 | `SqliteBrowser` 查询区使用 SQL 高亮，结果支持 CSV/JSON 导出 | `SqliteBrowser` 已有查询功能 |
| 4 | Hex 编辑器 | P4 | 新增 `HexEditorWidget`，二进制文件以十六进制视图编辑 | 无 |
| 5 | XML 格式化工具 | P4 | 新增 `XmlFormatter`，基于 QXmlStreamReader/Writer | 无 |

**涉及文件**：`src/ui/tools/` 目录下各文件

---

### L05 无障碍支持

**现状**：未实现。

| 序号 | 待开发子项 | 优先级 | 实现建议 |
|------|-----------|--------|----------|
| 1 | 高对比度主题 | P4 | `ThemeManager` 新增高对比度主题预设 |
| 2 | 编辑器字体放大/缩小（Ctrl++/Ctrl+-） | P4 | `MyTextEdit` 新增快捷键调整字体大小 |
| 3 | 键盘全操作 | P5 | 确保所有功能可通过键盘访问，Tab 焦点链完整 |
| 4 | 屏幕阅读器适配 | P5 | Qt AT-SPI 适配，设置 `accessibleName/accessibleDescription` |

---

## 七、额外建议（非功能需求）

| 序号 | 子项 | 优先级 | 实现建议 | 已有基础 |
|------|------|--------|----------|----------|
| 1 | 日志系统增强（分级+过滤+导出） | P2 | 扩展 `utils/Logger.hpp`，新增 DEBUG/INFO/WARN/ERROR 级别，支持按级别/模块过滤，导出为文件 | 基础日志已有 |
| 2 | 测试体系（Qt Test） | P2 | 新增 `tests/` 目录，覆盖 LspClient、TextCompleter、ConfigManager 等核心模块 | 无 |
| 3 | 崩溃监控（CrashHandler） | P3 | 集成 `QBreakpad` 或自建 `SetUnhandledExceptionFilter`，生成 dump 文件 | 无 |
| 4 | 用户反馈渠道 | P3 | 菜单「帮助→反馈问题」，自动附加日志/版本/系统信息，打开 GitHub Issue 或邮件 | 无 |
| 5 | 版本自动更新 | P4 | 启动时 HTTP 请求版本号，对比后提示下载 | 无 |

---

## 八、实施验证机制

每个需求落地后需通过以下验证：

1. **编译通过**：Windows（MSVC + MinGW）双工具链编译无错误
2. **功能不回归**：已有功能（编辑/补全/LSP/终端/Git）正常工作
3. **性能不下降**：主线程操作响应时间不超过基线 10%
4. **文档更新**：新增功能补充到「开发迭代说明.md」，更新快捷键/配置文档

---

## 九、需求完成度统计

| 优先级 | 需求 ID | 总子项数 | 已实现 | 待开发 | 完成率 |
|--------|---------|---------|--------|--------|--------|
| 核心 | C01 | 5 | 3 | 2 | 60% |
| 核心 | C02 | 5 | 0 | 5 | 0% |
| 核心 | C03 | 7 | 3 | 4 | 43% |
| 核心 | C04 | 11 | 8 | 3 | 73% |
| 核心 | C05 | 4 | 0 | 4 | 0% |
| 高 | H01 | 8 | 5 | 3 | 63% |
| 高 | H02 | 9 | 6 | 3 | 67% |
| 高 | H03 | 13 | 10 | 3 | 77% |
| 高 | H04 | 5 | 2 | 3 | 40% |
| 高 | H05 | 7 | 3 | 4 | 43% |
| 中 | M01 | 10 | 6 | 4 | 60% |
| 中 | M02 | 11 | 6 | 5 | 55% |
| 中 | M03 | 8 | 3 | 5 | 38% |
| 中 | M04 | 7 | 3 | 4 | 43% |
| 中 | M05 | 5 | 2 | 3 | 40% |
| 低 | L01 | 4 | 0 | 4 | 0% |
| 低 | L02 | 3 | 0 | 3 | 0% |
| 低 | L03 | 4 | 2 | 2 | 50% |
| 低 | L04 | 10 | 5 | 5 | 50% |
| 低 | L05 | 4 | 0 | 4 | 0% |
| **合计** | | **135** | **67** | **68** | **50%** |
