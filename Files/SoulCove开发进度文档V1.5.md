# SoulCove 现代化 Qt 编辑器 — 开发进度文档（V1.5）

> 最后更新：2026-06-16 | 终端集成增强阶段 — T1-T3 已完成，进入深度增强开发

---

## 一、需求完成度总览

### 已完成 ✅

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
| 19 | 内嵌终端（CMD+PowerShell多标签） | 6 | EmbeddedTerminal + QProcess |
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
| **32** | **终端内容搜索（T1）** | **8** | **搜索栏+高亮+上/下跳转+计数** |
| **33** | **终端复制粘贴增强（T2）** | **8** | **完整右键菜单+Ctrl+C/V+清屏+切换类型** |
| **34** | **终端样式完全联动主题（T3）** | **8** | **字体/颜色随主题实时刷新** |

### 待完成 🔄

| # | 需求项 | 优先级 | 所属阶段 | 说明 |
|---|--------|--------|---------|------|
| **T4** | **ANSI转义序列解析与渲染** | **高** | 终端 | 解析终端输出的颜色/粗体/下划线等ANSI码，在QTextEdit中富文本渲染 |
| **T5** | **命令历史记录（上下键翻阅）** | **高** | 终端 | 每个会话独立历史栈，Up/Down翻阅，支持模糊搜索 |
| **T6** | **终端分屏/水平布局** | **中** | 终端 | 支持终端面板水平分割，同时查看多个终端 |
| **T7** | **快捷键自定义与冲突检测** | **高** | 配置 | 设置页面配置所有快捷键，检测重复 |
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

## 二、当前正在开发

### 终端深度增强（V1.5 阶段：T4-T6）

**当前状态**：T1-T3 已全部实现并合入主分支，当前进入终端体验优化阶段。

#### T4: ANSI转义序列解析与渲染 ✏️ 进行中
- 解析 `\033[...m` SGR（Select Graphic Rendition）序列
- 支持：前景色(30-37/90-97)、背景色(40-47/100-107)、粗体(1)、斜体(3)、下划线(4)、删除线(9)
- 256色和RGB真彩支持
- 在 QTextEdit 中用 QTextCharFormat 渲染富文本
- 状态机解析，处理不完整/嵌套序列

#### T5: 命令历史记录
- 每个 TerminalSession 独立维护 `QStringList m_commandHistory`
- Up 键：向上翻阅历史（循环）
- Down 键：向下翻阅
- 输入过程中按 Up/Down 可替换当前输入内容
- 历史条数上限可配（默认500条）

#### T6: 终端分屏布局
- 终端面板支持水平方向 QSplitter 分割
- 右键菜单新增"水平分屏"/"关闭分屏"
- 各分屏区域独立会话管理

---

## 三、接口清单（src/interfaces/）

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
| **ITerminalWidget** | **EmbeddedTerminal** | **✅ T1-T3已完成，T4-T6开发中** |
| IUiLibrary | *(无实现)* | 待实现 |

---

## 四、主题系统架构

### 当前注册的主题（5套）

| Key | 显示名 | 类型 | 强调色 |
|-----|--------|------|--------|
| `purple` | 暗黑紫 | 暗色 | `#c678dd` |
| `blue` | 经典蓝 | 暗色 | `#61afef` |
| `black` | 极致黑 | 暗色 | `#abb2bf` |
| `light` | 亮色经典 | 亮色 | `#0078d4` |
| `pink` | 白粉 | 亮色(偏粉) | `#e84a8b` |

### QSS生成机制

```
ThemePalette（33个颜色变量）
    ↓ generateQSS()
QMap<QString, QString> 映射表
    ↓ repl() 遍历替换 {{key}} 占位符
完整QSS字符串
    ↓ qApp->setStyleSheet()
全局样式生效
```

---

## 五、已知BUG清单（V1.5 更新）

### 已修复 ✅

| # | 问题 | 修复方案 |
|---|------|---------|
| 1-12 | V1.4 所有已修复BUG | 见 V1.4 文档 |

### 待修复 ⚠️

| # | 问题 | 优先级 | 文件 |
|---|------|--------|------|
| R1 | clearSearchHighlights() 中 `doc` 变量未定义（编译错误） | **高** | EmbeddedTerminal.cpp:560 |
| R2 | 设置变更部分不立即生效 | 中 | SettingsPage.cpp |
| R3 | 搜索不支持非UTF-8编码 | 中 | SideBar.cpp |
| R4 | Markdown编辑/预览无滚动同步 | 低 | MarkdownMode.cpp |
| R5 | 终端可见性/侧边栏宽度不持久化 | 低 | ConfigManager.h |
| R6 | 右键"在文件夹中打开"打开父目录 | 低 | widget.cpp |

---

## 六、项目文件结构（关键模块）

```
SoulCoveFinal/
├── src/
│   ├── main.cpp                    # 入口 + 应用图标
│   ├── widget.h/cpp                # 主窗口（布局/信号连接/槽函数）
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
│   │   ├── TitleBar.h/cpp          # 标题栏（VSCode文本菜单/窗口控制）
│   │   ├── SideBar.h/cpp           # 侧边栏（活动栏/文件树/搜索/TODO）
│   │   ├── EditorTabBar.h/cpp      # 标签页栏（多标签/拖拽/脏标记）
│   │   ├── EmbeddedTerminal.h/cpp  # 终端（多标签/搜索/右键菜单/主题联动）
│   │   ├── SettingsPage.h/cpp      # 设置页面（4分类/搜索/恢复默认）
│   │   ├── MarkdownMode.h/cpp      # MD预览（分屏/实时渲染）
│   │   └── FramelessWindow.h/cpp   # 无边框窗口（拖拽/8边缘缩放）
│   │
│   ├── interfaces/                 # 抽象接口层（15个接口）
│   ├── factory/UIFactory.h/cpp     # 工厂类（组件创建统一入口）
│   └── resources.qrc               # 资源文件（图标/app_icon）
│
├── icon/                           # 图标资源
├── config/sys_param.ini            # 默认配置模板
├── styles/modern.qss              # 旧静态样式（已废弃，由ThemeManager替代）
├── CMakeLists.txt                  # 构建脚本（含install依赖复制）
└── Files/                          # 项目文档目录
```

---

## 七、版本变更记录

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| V1.0 → V1.3 | 06月初 | 基础功能迭代（窗口/标签/侧边栏/主题/MD/语法高亮） |
| V1.4 | 06-15 | 全量审计，标记T1-T3为待完成，整理BUG清单 |
| **V1.5** | **06-16** | **T1-T3标记为已完成(#32-34)，新增T4-T6终端深度增强TODO，发现R1编译BUG** |
