# SoulCove 需求清单实现核查说明

> 基于代码实际审查，仅列出**已实现或部分实现**的功能。未实现、仅占位或仅文档提及的需求未列出。

---

## 一、核心优先级

### C01 完善 LSP 链路稳定性 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| 每个请求生成唯一自增 ID，响应按 ID 路由到方法 | 已实现 | `src/core/lsp/LspClient.cpp` L128-L141（`createRequest` 自增 `m_requestId`）、L490（`m_pendingRequests.take(id)` 路由） |
| Hover 请求记录最新 ID，丢弃 stale 响应 | 已实现 | `src/core/lsp/LspClient.cpp` L320-L324（记录 `m_lastHoverRequestId`）、L588-L595（`id != m_lastHoverRequestId` 时丢弃） |
| documentSymbol 响应按 requestId → uri 精确路由 | 已实现 | `src/core/lsp/LspClient.h` L82（`m_symbolRequestUri`）；`LspClient.cpp` L356-L360（写入映射）、L632-L634（取出路由） |
| 通用 5s 请求超时 | 未实现 | — |
| completion/definition/references 按 ID 丢弃旧响应 | 未实现 | 仍依赖 `LspManager::m_currentRequestFile`，存在并发覆盖风险（O1 技术债） |

### C03 完善导航/跳转体验 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| 跳转定义前 push 当前位置到导航栈 | 已实现 | `src/ui/shell/Widget.cpp` L2720-L2723（`m_navStack.append({path, line, col})`，深度限制 50） |
| Ctrl+← 导航回退，恢复文件/行/列 | 已实现 | `src/ui/shell/Widget.cpp` L2731-L2770（`navigateBack()`：打开文件 + 定位光标 + 滚动） |
| 导航栈数据结构 | 已实现 | `src/ui/shell/Widget.h` L268-L274（`NavigationEntry{filePath, line, col}` + `QList<NavigationEntry> m_navStack`） |
| 前进（Ctrl+→） | 未实现 | — |
| 导航栈清空、持久化 | 未实现 | — |
| 定义预览（Alt+Click 悬浮） | 未实现 | — |
| Go to Implementation（Ctrl+F12） | 未实现 | `LspClient` 无 `textDocument/implementation` 请求 |

### C04 补全系统增强 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| 双轨补全：LSP 项优先，本地单词随后 | 已实现 | `src/ui/editor/TextCompleter.cpp` L712-L783（LSP 项按 `sortText` 排序后追加本地词） |
| LSP 项 vs 本地项区分 | 已实现 | `src/ui/editor/TextCompleter.h` L154-L155（`RoleIsLsp`/`RoleLspItem` 自定义 data role） |
| 弹窗键盘交互：上下键循环选中 | 已实现 | `src/ui/editor/TextCompleter.cpp` L893-L903（`Key_Up`/`Key_Down` 循环） |
| Tab/Enter 确认选中项 | 已实现 | `src/ui/editor/TextCompleter.cpp` L884-L891（`Key_Tab`/`Key_Enter`/`Key_Return`） |
| Esc 关闭弹窗 | 已实现 | `src/ui/editor/TextCompleter.cpp` L905-L909（`Key_Escape` → `hideCompletion()`） |
| LSP 项 tooltip 显示 kind + documentation | 已实现 | `src/ui/editor/TextCompleter.cpp` L734-L740（简单预览） |
| 成员补全模式（. / -> / :: 触发） | 已实现 | `src/ui/editor/TextCompleter.h` L148-L151；`TextCompleter.cpp` L796-L810 |
| 文档单词缓存（MD5/长度检测避免重复扫描） | 已实现 | `src/ui/editor/TextCompleter.cpp` L633-L704 |
| 200ms 节流防抖 | 已实现 | `src/ui/editor/TextCompleter.cpp` L83-L85；L609-L611 |
| 补全项分类标注（图标/标签区分 LSP/本地/片段） | 未实现 | — |
| 完整补全项预览面板 | 未实现 | — |
| LSP 补全结果缓存（同文件不重复请求） | 未实现 | — |

---

## 二、高优先级

### H01 终端功能增强 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| 本地终端集成（CMD/PowerShell） | 已实现 | `src/ui/terminal/TerminalBackend.cpp` L48-L102（`cmd.exe` / `powershell.exe` 启动） |
| 多终端标签页 | 已实现 | `src/ui/terminal/EmbeddedTerminal.cpp` L30-L103（`QTabBar` + `QStackedWidget`，新建/关闭/切换） |
| 终端搜索栏（Ctrl+F） | 已实现 | `src/ui/terminal/EmbeddedTerminal.cpp` L75-L161（搜索输入 + 上/下导航 + 结果计数） |
| Ctrl+Shift+` 新建终端快捷键 | 已实现 | `src/ui/terminal/EmbeddedTerminal.cpp` L93-L96 |
| ANSI 转义序列解析渲染 | 已实现 | `src/ui/terminal/TerminalView.cpp` L49-L60（`parseAnsiData`） |
| 终端与编辑器联动（右键"在终端运行"） | 未实现 | — |
| 终端输出语法高亮（命令/错误/日志） | 未实现 | — |
| 终端复用（不同标签页共享终端） | 未实现 | — |

### H02 代码片段系统 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| SnippetManager 单例 CRUD | 已实现 | `src/core/snippet/SnippetManager.cpp` L45-L92（add/remove/update/snippet） |
| 按语言筛选 | 已实现 | `src/core/snippet/SnippetManager.cpp` L50-L58（`snippetsForLanguage()`，支持 "all" 通用语言） |
| JSON 持久化 | 已实现 | `src/core/snippet/SnippetManager.cpp` L162-L199（`saveToFile`/`loadFromFile`，存储到 `config/snippets.json`） |
| 前缀触发检测 | 已实现 | `src/core/snippet/SnippetManager.cpp` L117-L134（`findTrigger()`，精确匹配 + 前缀匹配） |
| 占位符展开（$1/$2/${1:default}/$0） | 已实现 | `src/core/snippet/SnippetManager.cpp` L138-L158（`expandSnippet()`，替换为 `<|N|>` 光标标记） |
| 搜索 | 已实现 | `src/core/snippet/SnippetManager.cpp` L96-L113（按 name/prefix/description/language 搜索） |
| snippet 管理 UI 页 | 未实现 | — |
| $SELECTION 变量 | 未实现 | — |
| VSCode snippet 格式导入导出 | 未实现 | — |

### H03 Git 集成增强 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| GitManager：分支查询/切换 | 已实现 | `src/core/vcs/GitManager.cpp` L77-L83（`currentBranch`）、L162-L171（`checkoutBranch`） |
| GitManager：文件状态 | 已实现 | `src/core/vcs/GitManager.cpp` L105-L158（`fileStatuses()`，解析 `git status --porcelain`） |
| GitManager：stage/unstage | 已实现 | `src/core/vcs/GitManager.cpp` L173-L193（`stageFile`/`unstageFile`） |
| GitManager：commit | 已实现 | `src/core/vcs/GitManager.cpp` L195-L204 |
| GitManager：push/pull | 已实现 | `src/core/vcs/GitManager.cpp` L248-L269 |
| GitManager：diff | 已实现 | `src/core/vcs/GitManager.cpp` L217-L235（支持工作区 + 暂存区 diff） |
| GitManager：log | 已实现 | `src/core/vcs/GitManager.cpp` L237-L246（`git log --oneline --decorate`） |
| GitManager：discard | 已实现 | `src/core/vcs/GitManager.cpp` L206-L215 |
| GitPanel：分支下拉框 + 文件状态树 + 操作按钮 | 已实现 | `src/ui/sidebar/GitPanel.cpp` L23-L133（分支切换、暂存/提交/推/拉/放弃） |
| MergeConflictResolver：检测并解决冲突 | 已实现 | `src/core/vcs/MergeConflictResolver.cpp` L5-L150（detect/resolve，支持 ours/theirs/both） |
| Git 状态栏实时显示分支名/修改数 | 未实现 | 状态栏仅有占位 `labelBranch`（硬编码 "main"），未动态更新 |
| 编辑器内行级 Git 标注 | 未实现 | — |
| 提交历史日志面板 | 未实现 | — |

### H04 多窗口/工作区 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| 标签拖出为独立窗口 | 已实现 | `src/ui/editor/EditorTabBar.cpp` L383-L469（`detachTabToWindow()`，创建 `QMainWindow`） |
| 文件树拖拽移动文件 | 已实现 | `src/ui/sidebar/ExplorerPanel.cpp` L496-L525（`handleTreeDropEvent`） |
| Workspace 概念 | 未实现 | — |
| .scnb-workspace 持久化 | 未实现 | — |
| 多文件夹工作区 | 未实现 | — |

### H05 快捷键系统 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| ShortcutManager：注册/修改/查询 | 已实现 | `src/core/shortcut/ShortcutManager.cpp` L76-L250（`registerDefaults` + `setShortcut` + `shortcut`） |
| 快捷键冲突检测 | 已实现 | `src/core/shortcut/ShortcutManager.cpp` L303-L315（`checkConflict()`，返回冲突命令 ID 列表） |
| JSON 持久化 | 已实现 | `src/core/shortcut/ShortcutManager.cpp`（`saveConfig`/`loadConfig`） |
| VSCode 预设方案一键切换 | 未实现 | — |
| 快捷键重置/导出/导入 | 未实现 | — |
| 快捷键搜索 | 未实现 | — |
| 按键录制 | 未实现 | — |

---

## 三、中优先级

### M01 远程开发增强 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| SSH 连接管理（libssh2 封装） | 已实现 | `src/core/remote/SshClient.h`（连接/认证/Shell 通道/命令执行） |
| SFTP 文件传输 | 已实现 | `src/core/remote/SftpClient.h`（目录浏览/上传/下载/流式读写） |
| SSH 会话管理器（多连接 + 配置持久化） | 已实现 | `src/core/remote/SshSessionManager.cpp` L81-L119（密码/公钥/端口/心跳保存到 ConfigManager） |
| SSH 配置面板 UI | 已实现 | `src/ui/remote/SshConfigPanel.cpp` L33-L200（别名/主机/端口/认证/高级设置/测试连接） |
| 远程文件树浏览 | 已实现 | `src/ui/remote/RemoteFileTree.h`（SFTP 文件树 + 打开/上传/下载） |
| 远程终端 | 已实现 | `src/ui/terminal/SshTerminalWidget.h`（ITerminalWidget + ISshClient） |
| 远程工作区挂载 | 未实现 | — |
| 远程 LSP 自动部署 | 未实现 | — |
| 远程文件缓存 | 未实现 | — |
| 远程终端会话重连恢复 | 未实现 | — |

### M02 Markdown 功能升级 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| 分屏编辑 + 实时预览（maddy 解析） | 已实现 | `src/ui/markdown/MarkdownMode.cpp` L25-L130（QSplitter + QTextBrowser） |
| 目录大纲面板（树形 + 点击跳转） | 已实现 | `src/ui/markdown/MdTocPanel.cpp` L14-L160（树形 TOC + 高亮当前标题 + 点击跳转） |
| 双向滚动同步 | 已实现 | `src/ui/markdown/MarkdownMode.cpp` L103-L109（编辑器↔预览区滚动比例同步） |
| 导出 PDF/HTML | 已实现 | `src/core/markdown/MdExporter.h`（HTML/PDF 导出） |
| 图片灯箱预览 | 已实现 | `src/ui/markdown/ImageLightBox.h`（模态窗口 + 缩放） |
| 自适应防抖（150-500ms） | 已实现 | `src/ui/markdown/MarkdownMode.cpp` L80-L96 |
| 大纲折叠 | 未实现 | — |
| mermaid / PlantUML | 未实现 | — |
| 自定义 CSS | 未实现 | — |
| 暗黑模式专门适配 | 未实现 | — |
| 复制为富文本 | 未实现 | — |

### M03 编辑器体验精细化 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| 代码格式化（clang-format + 内置 fallback） | 已实现 | `src/core/format/CodeFormatter.cpp` L22-L89（检测 clang-format，5s 超时，回退内置缩进格式化） |
| 编码自动检测（BOM + 统计分析） | 已实现 | `src/core/fileio/EncodingDetector.cpp` L38-L140（UTF-8/GBK/GB18030/ISO-8859-1/ASCII） |
| 状态栏编码/换行符/缩进指示器 | 已实现 | `src/ui/shell/Widget.cpp` L519-L545（encoding combo + EOL label + spaces label） |
| .editorconfig 支持 | 未实现 | — |
| hunspell 拼写检查 | 未实现 | — |
| LF/CRLF 配置切换 | 未实现 | — |
| 代码折叠增强（区域/注释/自定义） | 未实现 | — |
| 列选择模式 | 未实现 | — |

### M04 任务/调试系统 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| TaskManager：任务 CRUD + 分组 | 已实现 | `src/core/task/TaskManager.cpp` L33-L76（add/remove/update/tasksByGroup） |
| TaskManager：任务运行/停止 | 已实现 | `src/core/task/TaskManager.cpp` L82-L135（`runTask`/`stopTask`/`stopAll`，QProcess 管理） |
| TasksPanel：任务树 + 输出区 + 运行/停止按钮 | 已实现 | `src/ui/sidebar/TasksPanel.cpp` L21-L200（分组树 + 右键菜单 + 实时输出） |
| tasks.json 兼容 | 未实现 | — |
| 任务依赖 | 未实现 | — |
| 调试功能（GDB/LLDB） | 未实现 | — |
| 输出过滤/错误跳转 | 未实现 | — |

### M05 国际化与本地化 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| 翻译文件骨架（zh_CN/en_US） | 已实现 | `src/i18n/SoulCove_zh_CN.ts` / `SoulCove_en_US.ts` |
| 代码中使用 tr() 包裹用户可见字符串 | 已实现 | 全局使用 |
| 运行时切换语言 | 未实现 | — |
| 区域设置 | 未实现 | — |
| UI 布局适配 | 未实现 | — |

---

## 四、低优先级

### L03 多语言 LSP 完善 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| LanguageRegistry：cpp/python/js/ts/go/java/rust 注册 | 已实现 | `src/core/lsp/LanguageRegistry.cpp` L15-L63（后缀映射 + LSP langId + 内存缓存） |
| LspManager：clangd/pylsp/ts-langserver 启动 | 已实现 | `src/core/lsp/LspManager.cpp`（按 langId 路由到对应服务器） |
| LSP 自动安装 | 未实现 | — |
| LSP 自定义参数配置 | 未实现 | — |

### L04 工具面板增强 — 部分实现

| 子项 | 状态 | 实现位置 |
|------|------|----------|
| RegexTester：正则匹配 + 富文本结果 + 复制 | 已实现 | `src/ui/tools/RegexTester.cpp`（选项：大小写/多行/dotAll/语法类型） |
| JsonValidator：JSON 校验 + 格式化 | 已实现 | `src/core/format/JsonValidator.cpp`（校验含行号列号、美化输出） |
| SqliteBrowser：数据库浏览 | 已实现 | `src/ui/tools/SqliteBrowser.h` |
| DiffViewer：并排对比 + 同步滚动 | 已实现 | `src/ui/tools/DiffViewer.cpp` L106-L113 |
| ImagePreviewer：图片预览 | 已实现 | `src/ui/tools/ImagePreviewer.h` |
| 替换预览 | 未实现 | — |
| JSON Schema 校验 | 未实现 | — |
| SQL 语法高亮/结果导出 | 未实现 | — |
| Hex 编辑器 / XML 格式化 | 未实现 | — |
| 工具面板固定到侧边栏/多标签 | 未实现 | — |

---

## 五、额外建议

| 子项 | 状态 | 说明 |
|------|------|------|
| 日志系统 | 已实现 | `utils/Logger.hpp` 基础日志，输出到 `debug.log` |
| 测试体系（Qt Test） | 未实现 | — |
| 崩溃监控（CrashHandler） | 未实现 | — |
| 用户反馈渠道 | 未实现 | — |
| 版本自动更新 | 未实现 | — |

---

## 六、未实现需求汇总

以下需求在代码中**未找到实际实现**（仅文档提及或完全缺失）：

- **C02** 主线程性能优化：性能监控面板、编辑器增量渲染
- **C05** 构建配置硬编码：CMakeLists.txt 仍硬编码 Qt/OpenSSL 绝对路径；无构建设置页、Qt 自动检测、多构建目录
- **H01** 终端与编辑器联动、终端输出语法高亮、终端复用
- **H02** snippet 管理 UI、$SELECTION、VSCode 格式导入导出
- **H03** Git 状态栏实时更新、编辑器行级标注、提交历史面板
- **H04** Workspace 概念、.scnb-workspace 持久化、多文件夹工作区
- **H05** VSCode 预设方案、重置/导出/导入、搜索、按键录制
- **L01** 插件系统（架构/市场/SDK）
- **L02** 云同步/配置备份
- **L05** 无障碍支持（屏幕阅读器/高对比度/全键盘操作）
