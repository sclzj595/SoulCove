# SoulCove 现代化 Qt 编辑器 — 开发进度文档（V1.4）

> 最后更新：2026-06-15 | 终端集成开发启动前 全量审计

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

### 待完成 🔄

| # | 需求项 | 优先级 | 所属阶段 | 说明 |
|---|--------|--------|---------|------|
| T1 | **终端内容搜索** | **高** | 终端 | 终端输出支持搜索过滤、上下翻页 |
| T2 | **终端复制粘贴增强** | **高** | 终端 | 右键菜单完善、粘贴保留格式 |
| T3 | **终端样式完全联动主题** | **高** | 终端 | 字体/颜色/背景随主题切换实时生效 |
| T4 | **快捷键自定义与冲突检测** | **高** | 配置 | 设置页面配置所有快捷键，检测重复 |
| T5 | **文件编码自动识别与转换** | **中** | 编辑 | 打开文件时自动检测 GBK/UTF-8/UTF-16 等 |
| T6 | **大文件编辑性能优化** | **中** | 编辑 | 分块渲染、延迟加载（>100MB目标） |
| T7 | **命令面板 (Ctrl+Shift+P)** | **中** | UI | VSCode风格全局命令搜索框 |
| T8 | **括号匹配高亮/彩虹括号** | **中** | 编辑 | 配对括号视觉指示 |
| T9 | **自动缩进/格式化** | **中** | 编辑 | 保存时或按键时自动格式化 |
| T10 | **Git 集成（侧边栏 Git 面板）** | **低** | 扩展 | 分支切换/diff查看/提交 |
| T11 | **LSP 集成（语义级提示）** | **低** | 智能 | 对接 pylsp/jdt.ls 语言服务 |
| T12 | **扩展/插件系统** | **低** | 架构 | 第三方功能扩展接口 |
| T13 | **迷你地图（Minimap）** | **低** | UI | 编辑器右侧代码缩略图 |
| T14 | **多光标编辑** | **低** | 编辑 | Alt+Click 多位置光标 |
| T15 | **文件监听（外部修改刷新）** | **低** | 编辑 | 外部工具修改文件时自动 reload |

---

## 二、当前正在开发

### 终端集成增强（T1-T3）

**当前状态**：基础框架已完成（多标签/CMD+PowerShell/QProcess），需增强以下功能：

#### T1: 终端内容搜索
- 在终端面板顶部添加搜索输入框
- 支持在终端输出缓冲区中搜索关键词
- 高亮匹配结果，支持上/下一个跳转
- ESC 或按钮关闭搜索

#### T2: 复制粘贴增强
- 右键菜单：复制选中区域 / 粘贴 / 全选 / 清屏 / 查找
- Ctrl+C/Ctrl+V 快捷键支持
- 粘贴时保留换行和特殊字符格式
- 选择模式：双击选词 / 三击选行 / 拖拽选择矩形区域

#### T3: 样式完全联动主题
- 终端背景色跟随 bgEditor 或独立配置
- 终端前景色跟随 fgPrimary
- 终端字体跟随全局字体配置
- 终端滚动条样式跟随主题
- 切换主题时已打开的终端实时刷新样式

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
| ITerminalWidget | EmbeddedTerminal | 基础完成，待增强(T1-T3) |
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

## 五、已知BUG清单（V1.4 更新）

### 已修复 ✅

| # | 问题 | 修复方案 |
|---|------|---------|
| 1-7 | V1.3 所有严重/中等BUG | 见 V1.3 文档 |
| 8 | 主题切换失效（3个根因） | switchTopic移除早返回+QSS重写+亮色主题 |
| 9 | 滚动条红色（arg错位） | QMap命名占位符替代.arg()链式调用 |
| 10 | 点击第二个文件崩溃 | 补全器延迟初始化+nullptr保护 |
| 11 | Emoji乱码（Windows） | QString::fromUtf8()+Segoe UI Emoji字体 |
| 12 | 侧边栏固定宽度不可拖拽 | 改为QSplitter(Qt::Horizontal) |

### 待修复 ⚠️

| # | 问题 | 优先级 | 文件 |
|---|------|--------|------|
| R1 | 设置变更部分不立即生效 | 中 | SettingsPage.cpp |
| R2 | 搜索不支持非UTF-8编码 | 中 | SideBar.cpp |
| R3 | Markdown编辑/预览无滚动同步 | 低 | MarkdownMode.cpp |
| R4 | 终端可见性/侧边栏宽度不持久化 | 低 | ConfigManager.h |
| R5 | 右键"在文件夹中打开"打开父目录 | 低 | widget.cpp |

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
│   │   ├── EmbeddedTerminal.h/cpp  # 终端（多标签/CMD+PowerShell）
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
