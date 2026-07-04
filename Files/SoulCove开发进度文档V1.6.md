# SoulCove 现代化 Qt 编辑器 — 开发进度文档（V1.6）

> 最后更新：2026-06-16 | 终端模块化重构完成 + 全链路代码审查 | 编译通过 ✅

---

## 一、需求完成度总览

### 已完成 ✅（35项）

| # | 需求项 | 阶段 | 备注 |
|---|--------|------|------|
| 1 | 窗口最大化/还原图标同步 | 1 | changeEvent + updateMaximizeIcon |
| 2 | 设置按钮点击无响应 → 内嵌标签页 | 1 | addCustomTab + customTabDestroyed |
| 3 | Tab 脏文件白圆点标记 | 2 | ● 白圆点 VSCode风格 |
| 4 | 侧边栏双击打开文件 | 2 | fileOpenRequested → openFileTab |
| 5 | Sidebar-Tab 双向同步 | 2 | selectFileByPath 路径匹配 |
| 6 | 侧边栏保存/新建后自动刷新 | 2 | refreshFileList |
| 7 | 关闭脏Tab弹窗二次确认 | 2 | QMessageBox 三按钮 |
| 8 | Sidebar 右键上下文菜单 | 2 | 新建/删除/重命名/文件夹打开 |
| 9 | StatusBar 文件修改状态指示器 | 2 | ● 已修改 / 保存后清除 |
| 10 | 设置面板重构（分类导航+搜索） | 3 | 外观/编辑器/终端/智能提示 4分类 |
| 11 | 多主题工厂架构（5套） | 3 | purple/blue/black/light/pink |
| 12 | QSS全变量化（零硬编码颜色） | 3 | QMap命名占位符，33个变量 |
| 13 | Tab标签栏UI美化 | 3 | 圆角/悬浮/选中强调线/关闭动画 |
| 14 | Markdown分屏编辑+实时预览 | 4 | MarkdownMode + maddy三方库 |
| 15 | MD预览跟随主题 | 4 | MaddyParser::defaultStyleSheet() |
| 16 | 文件夹树形结构（QTreeWidget） | 6 | 递归填充 + emoji图标 |
| 17 | 语法高亮（12种语言+主题联动） | 6 | CodeSyntaxHighlighter + updateThemeColors |
| 18 | 智能提示（模糊匹配+延迟初始化） | 6 | TextCompleter FuzzyMatch模式 |
| 19 | 内嵌终端基础版（CMD+PowerShell多标签） | 6 | EmbeddedTerminal + QProcess |
| 20 | 文件搜索功能 | 6 | 递归内容搜索，双击打开 |
| 21 | 拖拽文件打开 | 6 | dragEnterEvent + dropEvent |
| 22 | 配置导出/导入（JSON格式） | 6 | SettingsPage 导出导入功能 |
| 23 | 应用窗口图标设置 | 6 | vscode.png → setWindowIcon |
| 24 | CMakeLists依赖自动复制 | 6 | DLL/plugins/icon/resources |
| 25 | 补全器延迟初始化（修复崩溃） | 7 | bindCurrentEditor中按需创建 |
| 26 | 侧边栏可拖拽宽度（QSplitter） | 7 | min=160 max=600 默认260 |
| 27 | 白粉主题（更粉饱和度） | 7 | #e84a8b 热粉红强调色 |
| 28 | 标题栏VSCode文本菜单（文件/编辑/选择） | 7 | 单击弹出QMenu下拉菜单 |
| 29 | 状态栏现代化（VSCode布局） | 7 | 分支/问题/LnCol/编码/EOL/语言/空格 |
| 30 | 启动时不自动创建空白标签页 | 7 | VSCode风格：用户主动操作才创建 |
| 31 | 左侧资源管理器→打开文件夹模式 | 7 | 不再硬编码Files/目录 |
| 32 | 终端内容搜索（T1） | 8 | 搜索栏+高亮+上/下跳转+计数 |
| 33 | 终端复制粘贴增强（T2） | 8 | 完整右键菜单+Ctrl+C/V+清屏+切换类型 |
| 34 | 终端样式完全联动主题（T3） | 8 | 字体/颜色随主题实时刷新 |
| **35** | **终端模块化架构重构** | **9** | **TerminalView+TerminalBackend+Panel** |
| **36** | **VSCode风格欢迎页** | **9** | **Logo+按钮+快捷键+拖拽提示** |
| **37** | **终端面板标题栏** | **9** | **VSCode Panel: 终端/问题/输出 标签** |
| **38** | **ANSI转义序列渲染** | **9** | **16色+亮色 SGR解析，状态机缓冲** |
| **39** | **命令历史记录（Up/Down）** | **9** | **独立栈、去重、500条上限、行内替换** |

### 待完成 🔄

| # | 需求项 | 优先级 | 所属阶段 | 说明 |
|---|--------|--------|---------|------|
| T7 | **快捷键自定义与冲突检测** | **高** | 配置 | 设置页面配置所有快捷键，检测重复 |
| T8 | 文件编码自动识别与转换 | 中 | 编辑 | 打开文件时自动检测 GBK/UTF-8/UTF-16 等 |
| T9 | 大文件编辑性能优化 | 中 | 编辑 | 分块渲染、延迟加载（>100MB目标） |
| T10 | 命令面板 (Ctrl+Shift+P) | 中 | UI | VSCode风格全局命令搜索框 |
| T11 | 括号匹配高亮/彩虹括号 | 中 | 编辑 | 配对括号视觉指示 |
| T12 | 自动缩进/格式化 | 中 | 编辑 | 保存时或按键时自动格式化 |
| T13 | Git 集成（侧边栏 Git 面板） | 低 | 扩展 | 分支切换/diff查看/提交 |
| T14 | LSP 集成（语义级提示） | 低 | 智能 | 对接 pylsp/jdt.ls 语言服务 |
| T15 | 扩展/插件系统 | 低 | 架构 | 第三方功能扩展接口 |
| T16 | 迷你地图（Minimap） | 低 | UI | 编辑器右侧代码缩略图 |
| T17 | 多光标编辑 | 低 | 编辑 | Alt+Click 多位置光标 |
| T18 | 文件监听（外部修改刷新） | 低 | 编辑 | 外部工具修改文件时自动 reload |

---

## 二、V1.6 本次迭代变更详情

### 2.1 终端模块化架构重构（核心变更）

**之前**：`EmbeddedTerminal` 单体类 = QLineEdit(输入) + QTextEdit只读(输出) = **假终端**

**现在**：三层分离架构

```
EmbeddedTerminal (容器 — 多标签/搜索/右键菜单/主题联动)
├── QTabBar (终端标签栏 — CMD 1 / PS 1 / ...)
├── SearchBar (Ctrl+F 内容搜索)
└── QStackedWidget
    └── TerminalSession × N
        ├── [TerminalView]     ← 新建：QTextEdit 子类（输入输出合一）
        │   ├── m_inputMarker   光标追踪（用户不能编辑已输出内容）
        │   ├── keyPressEvent() 行内编辑（Enter/Backspace/Home/End/Up/Down）
        │   ├── parseAnsiData() ANSI 解析渲染（SGR 16色）
        │   └── navigateHistory() Up/Down 命令历史
        │
        └── [TerminalBackend]  ← 新建：QProcess 封装
            ├── start(CMD/PS)   进程启动（SeparateChannels）
            ├── write()         stdin 写入
            ├── sendInterrupt() Ctrl+C (\x03)
            └── readyReadOutput/Error 信号 → 视图渲染
```

### 2.2 新增文件清单

| 文件 | 职责 | 行数 |
|------|------|------|
| `src/ui/TerminalView.h` | 统一终端视图声明 | ~80 |
| `src/ui/TerminalView.cpp` | 输出显示+行内输入+ANSI+键盘交互 | ~520 |
| `src/ui/TerminalBackend.h` | 进程后端声明 | ~55 |
| `src/ui/TerminalBackend.cpp` | CMD/PS进程管理 | ~120 |

### 2.3 VSCode 风格欢迎页

- 空编辑区时显示（有标签时隐藏）
- 应用图标 Logo（QSS opacity:0.35 柔和水印）
- "SoulCove" 标题 + "现代化代码编辑器" 副标题
- 3个操作按钮：打开文件 / 打开文件夹 / 新建文件
- 快捷键提示：Ctrl+O / Ctrl+N / Ctrl+` / Ctrl+Shift+P
- 使用提示：拖拽文件到窗口 / 侧边栏双击打开

### 2.4 终端面板（VSCode Panel 模式）

```
┌─────────────────────────────────────┐
│  终端    问题    输出          ✕    │ ← 面板标题栏（互斥标签）
├─────────────────────────────────────┤
│ ┌──────────┐                        │
│ │ PS 1  ✕  │  ← 终端标签栏（暗色主题） │
│ └──────────┘                        │
│ PS F:\...\build> ls                 │ ← PowerShell 交互
│ Directory: ...                      │
│ ▌                                   │
└─────────────────────────────────────┘
```

- 默认 Shell 改为 **PowerShell**（支持 `ls` / `cd` / 管道等现代命令）
- 面板标题栏：`终端` / `问题` / `输出` 三个互斥切换标签
- 终端标签栏：强制暗色样式覆盖 Windows 原生白底
- 侧边栏活动栏新增 **▶ 终端按钮**

---

## 三、全链路代码审查报告（V1.6）

> 审查范围：24个代码单元（12组头+源），静态分析

### 🔴 严重（必须修复）— 5项

| # | 问题 | 文件:行 | 说明 | 建议 |
|---|------|---------|------|------|
| R1 | **lambda悬空引用** | EmbeddedTerminal.cpp:265 | `&palette` 引用局部变量，lambda 异步执行时可能崩溃 | 改为值捕获或成员变量 |
| R2 | **delete前未断开信号** | TerminalBackend.cpp:99 | stop() 中 delete m_process 前未 disconnect，可能触发已删除对象的槽函数 | 先 disconnectAll 或用 deleteLater |
| R3 | **m_inputMarker初始值0** | TerminalView.cpp:75 | 首次 appendOutput 前如果文档非空，marker=0 导致用户无法删除欢迎文本 | 构造函数中 clear() 后立即设 marker |
| R4 | **onTabCloseRequested索引重映射** | EmbeddedTerminal.cpp:220 | 关闭标签后 QMap 索引不连续，后续查找可能 miss | 改用 widget 指针作为 key 或重建映射 |
| R5 | **TerminalView::clearScreen()重置marker为0** | TerminalView.cpp:97 | 清屏后 marker=0，但文档已有内容时导致不一致 | clear 后重新设 marker 为文档末尾 |

### 🟡 中等（建议修复）— 18项

| # | 问题 | 文件 | 说明 |
|---|------|------|------|
| M1 | ANSI解析性能 | TerminalView.cpp | 每次收到数据都逐字符遍历整个缓冲区，高频输出时可能卡顿 |
| M2 | setStyleSheet在applyTheme重复调用 | EmbeddedTerminal.cpp:674 | 每次主题切换都重建完整字符串，应缓存 |
| M3 | TerminalBackend::write()无flush保证 | TerminalBackend.cpp:107 | 数据可能在缓冲区停留，影响实时性 |
| M4 | 欢迎页Logo QSS opacity兼容性 | widget.cpp | 部分Qt版本不支持QSS opacity属性 |
| M5 | 侧边栏终端按钮无tooltip | SideBar.cpp | 用户不知道▶按钮是终端入口 |
| M6 | executeCommand()回显方式 | EmbeddedTerminal.cpp:330 | 通过showWelcome回显命令，与真实shell回显行为不同 |
| M7 | TerminalView不支持鼠标选中复制 | TerminalView.cpp | 缺少鼠标三击选行功能 |
| M8 | 搜索结果高亮不跟随主题 | EmbeddedTerminal.cpp | 高亮颜色硬编码#ffcc00 |
| M9 | ConfigManager::fontSize()不存在风险 | EmbeddedTerminal.cpp:661 | 如果配置中没有该key会fallback到默认值11 |
| M10 | TerminalSession.pageWidget无父级清理 | EmbeddedTerminal.h | cleanupSession只清理backend和view，pageWidget由stackedWidget管理 |
| M11 | PowerShell -Command参数过长 | TerminalBackend.cpp:45 | 初始化命令拼接在一行，复杂场景可能截断 |
| M12 | TerminalView无垂直滚动条美化 | modern.qss | 终端滚动条使用原生粗大样式 |
| M13 | 面板关闭后m_terminalVisible状态 | widget.cpp:946 | 关闭时设false但未通知侧边栏按钮取消选中 |
| M14 | setWorkingDirectory对已运行会话发cd命令 | EmbeddedTerminal.cpp:315 | 发送的cd命令本身也会产生输出，可能干扰用户 |
| M15 | welcomePage对象名未注册QSS选择器 | widget.cpp | QLabel#welcomeIcon 在modern.qss中有定义但内容为空 |
| M16 | TerminalView构造函数字体硬编码 | TerminalView.cpp:23 | Consolas/11px硬编码，未从ConfigManager读取 |
| M17 | onReadyReadStandardError直接走appendOutput | EmbeddedTerminal.cpp:280 | stderr没有特殊颜色标识 |
| M18 | EmbeddedTerminal析构函数未调用terminateSession | EmbeddedTerminal.h | 析构只调了terminateSession但h/cpp中无显式析构实现 |

### 🟢 低优先级（可改进）— 10项

| # | 问题 | 文件 | 说明 |
|---|------|------|------|
| L1 | 注释语言混用 | 多处 | 中文注释为主，部分遗留英文 |
| L2 | CMakeLists.txt中TerminalView/Backend描述 | CMakeLists.txt | 注释为中文，与其他条目一致 ✅ |
| L3 | modern.qss中welcomeIcon规则为空 | modern.qss:670 | 应添加 opacity 规则或移除空块 |
| L4 | TerminalBackend析构调用stop() | TerminalBackend.cpp:12 | 正确，但stop()内部delete后置null，双重安全 |
| L5 | EmbeddedTerminal无拖拽分割支持 | — | VSCode终端面板支持拖拽调整高度 |
| L6 | 无终端分屏功能 | — | TODO T6 |
| L7 | 无SSH远程终端支持 | — | 未来扩展方向 |
| L8 | 无终端字体大小调节 | SettingsPage | 终端字体只能跟随编辑器字体 |
| L9 | 无终端环境变量自定义配置 | — | 未来可在设置页添加 |
| L10 | 无终端配色方案自定义 | — | 当前固定VSCode Dark+配色 |

### 项目健康度评分：**7.5/10**

- ✅ 架构清晰，三层终端分离设计合理
- ✅ 接口抽象完善，ITerminalWidget 接口稳定
- ⚠️ 存在5个需要尽快修复的严重问题（主要是生命周期和索引安全）
- ⚠️ 体验细节还需打磨（滚动条美化、tooltip、选中复制）

---

## 四、接口清单（src/interfaces/）

| 接口 | 实现类 | 状态 |
|------|--------|------|
| IConfigManager | ConfigManager | 已完成 |
| IFileOperator | FileOperator | 已完成 |
| IMarkdownParser | MaddyParser / MarkdownParser | 已完成 |
| IEditorEdit | MyTextEdit | 已完成 |
| ICompleter | TextCompleter | 已完成 |
| ILineNumber | LineNumberArea | 已完成 |
| IObserver / ISubject | Widget / Subject | 已完成 |
| IThemeManager | ThemeManager | 已完成 |
| IFramelessWindow | FramelessWindow | 已完成 |
| ITabWidget | EditorTabBar | 已完成 |
| ISideFileBar | SideBar | 已完成 |
| IMarkdownViewer | MarkdownMode | 已完成 |
| ISyntaxHighlighter | CodeSyntaxHighlighter | 已完成 |
| ITerminalWidget | EmbeddedTerminal | **✅ V1.6 模块化重构完成** |
| IUiLibrary | *(无实现)* | 待实现 |

**新增接口需求（建议）**：
- `ITerminalView` — TerminalView 的抽象接口（当前直接暴露具体类）
- `ITerminalProcess` — TerminalBackend 的抽象接口（支持未来 SSH/PTY 后端）

---

## 五、项目文件结构（V1.6 更新）

```
SoulCoveFinal/
├── src/
│   ├── main.cpp                    # 入口 + 应用图标
│   ├── widget.h/cpp                # 主窗口（布局/欢迎页/面板/信号连接）
│   ├── mytextedit.h/cpp            # 编辑器核心（补全/语法/行号）
│   ├── textCompleter.h/cpp         # 智能补全（模糊匹配/FuzzyMatch）
│   │
│   ├── core/
│   │   ├── ThemeManager.h/cpp      # 主题管理（单例/5主题/QSS生成）
│   │   ├── ConfigManager.h/cpp     # 配置管理（INI/JSON导出导入）
│   │   ├── CodeSyntaxHighlighter   # 语法高亮（12语言/主题联动）
│   │   └── MaddyParser.h/cpp       # MD解析（maddy三方库）
│   │
│   ├── ui/
│   │   ├── TitleBar.h/cpp          # 标题栏（VSCode文本菜单/窗口控制/app_icon）
│   │   ├── SideBar.h/cpp           # 侧边栏（活动栏/文件树/搜索/TODO/**终端按钮**）
│   │   ├── EditorTabBar.h/cpp      # 标签页栏（多标签/拖拽/脏标记）
│   │   ├── EmbeddedTerminal.h/cpp  # **终端容器（Panel模式/多标签/搜索/右键菜单）**
│   │   ├── **TerminalView.h/cpp**  # **[新增] 统一视图（输入输出合一/ANSI/历史）**
│   │   ├── **TerminalBackend.h/cpp** # **[新增] 进程后端（CMD/PS/QProcess封装）**
│   │   ├── SettingsPage.h/cpp      # 设置页面（4分类/搜索/恢复默认）
│   │   ├── MarkdownMode.h/cpp      # MD预览（分屏/实时渲染）
│   │   └── FramelessWindow.h/cpp   # 无边框窗口（拖拽/8边缘缩放）
│   │
│   ├── interfaces/                 # 抽象接口层（15个接口）
│   ├── factory/UIFactory.h/cpp     # 工厂类（组件创建统一入口）
│   └── resources.qrc               # 资源文件（图标/app_icon/vscode.png）
│
├── icon/                           # 图标资源
├── config/sys_param.ini            # 默认配置模板
├── styles/modern.qss              # QSS样式（含终端面板/标签栏/搜索栏）
├── CMakeLists.txt                  # 构建脚本（含TerminalView/Backend编译）
└── Files/                          # 项目文档目录
```

---

## 六、已知BUG清单（V1.6 更新）

### 待修复 ⚠️

| # | 问题 | 优先级 | 文件 | 来源 |
|---|------|--------|------|------|
| **R1** | **lambda悬空引用 &palette** | **🔴高** | EmbeddedTerminal.cpp:265 | V1.6审查 |
| **R2** | **delete前未断开信号** | **🔴高** | TerminalBackend.cpp:99 | V1.6审查 |
| **R3** | **m_inputMarker初始值0** | **🔴高** | TerminalView.cpp:75 | V1.6审查 |
| R4 | onTabCloseRequested索引重映射 | 🟡中 | EmbeddedTerminal.cpp:220 | V1.6审查 |
| R5 | clearScreen()重置marker为0 | 🟡中 | TerminalView.cpp:97 | V1.6审查 |
| R6 | 设置变更部分不立即生效 | 中 | SettingsPage.cpp | V1.5遗留 |
| R7 | 搜索不支持非UTF-8编码 | 中 | SideBar.cpp | V1.5遗留 |
| R8 | Markdown编辑/预览无滚动同步 | 低 | MarkdownMode.cpp | V1.5遗留 |
| R9 | 终端可见性/侧边栏宽度不持久化 | 低 | ConfigManager.h | V1.5遗留 |
| R10 | 右键"在文件夹中打开"打开父目录 | 低 | widget.cpp | V1.5遗留 |

---

## 七、下一步开发建议（按优先级排序）

### P0 — 本轮必做（基于代码审查发现）

| 序号 | 任务 | 工作量 | 说明 |
|------|------|--------|------|
| 1 | **修复 R1-R5 审查发现的5个严重问题** | ~30min | lambda悬空引用、信号断连、marker初始化、索引映射 |
| 2 | **终端标签白底最终确认** | ~10min | 内联setStyleSheet是否完全生效 |

### P1 — 高优先级（用户体验关键）

| 序号 | 任务 | 对标VSCode | 说明 |
|------|------|-----------|------|
| 3 | **快捷键自定义 (T7)** | ✅ | 设置页面配置所有快捷键，检测冲突 |
| 4 | **命令面板 Ctrl+Shift+P (T10)** | ✅ | 全局命令模糊搜索 |
| 5 | **括号匹配高亮 (T11)** | ✅ | 配对括号视觉指示 |
| 6 | **终端字体/颜色独立配置** | ✅ | 不再绑定编辑器字体 |
| 7 | **终端滚动条美化** | ✅ | VSCode极细滚动条样式 |
| 8 | **文件编码自动检测 (T8)** | ✅ | 打开时自动识别 GBK/UTF-8 |

### P2 — 中优先级（功能增强）

| 序号 | 任务 | 说明 |
|------|------|------|
| 9 | 自动缩进/格式化 (T12) | 保存时自动格式化 |
| 10 | 大文件性能优化 (T9) | 分块渲染 >100MB |
| 11 | 终端分屏 (T6) | 水平分割多终端 |
| 12 | 迷你地图 (T16) | 编辑器右侧缩略图 |
| 13 | 多光标编辑 (T17) | Alt+Click |

### P3 — 扩展方向（长期规划）

| 序号 | 任务 | 说明 |
|------|------|------|
| 14 | Git 集成 (T13) | 侧边栏Git面板 |
| 15 | LSP 集成 (T14) | pylsp/jdt.ls 语义补全 |
| 16 | 远程终端 (SSH) | TerminalBackend 抽象扩展 |
| 17 | 插件系统 (T15) | 第三方功能扩展接口 |
| 18 | 文件监听 (T18) | 外部修改自动reload |

---

## 八、版本变更记录

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| V1.0→V1.3 | 06月初 | 基础功能迭代（窗口/标签/侧边栏/主题/MD/语法高亮） |
| V1.4 | 06-15 | 全量审计，标记T1-T3为待完成 |
| V1.5 | 06-16 | T1-T3完成(#32-34)，新增T4-T6，发现R1编译BUG |
| **V1.6** | **06-16** | **终端模块化重构(TerminalView+Backend)+欢迎页+Panel+ANSI+历史记录+默认PS+标签样式修复+全链路审查(5严重/18中等)** |
