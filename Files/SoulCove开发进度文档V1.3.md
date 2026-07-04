# SoulCove 现代化 Qt 编辑器 — 开发进度文档（V1.3）

> 最后更新：2026-06-15 | 对照需求规格说明书 V1.3 全量审计

---

## 一、需求完成度总览

| 阶段 | 需求项 | 状态 | 备注 |
|------|--------|------|------|
| **阶段1：紧急BUG修复** | | | |
| | 窗口最大化/还原图标同步 | **已完成** | changeEvent + updateMaximizeIcon 三处调用 |
| | 设置按钮点击无响应 | **已完成** | onSettingsClicked → addCustomTab 标签页内嵌 |
| **阶段2：核心交互补齐** | | | |
| | Tab 脏文件白圆点标记 | **已完成** | updateTabText 使用 ● 白圆点（VSCode风格） |
| | 侧边栏双击打开文件 | **已完成** | fileOpenRequested → openFileTab |
| | Sidebar-Tab 双向同步 | **已完成** | 切换Tab时侧边栏高亮对应文件（selectFileByPath） |
| | 侧边栏保存/新建后自动刷新 | **已完成** | refreshFileList 在另存为/新建/删除/重命名后调用 |
| | 关闭脏Tab弹窗二次确认 | **已完成** | closeTab 中 QMessageBox 三按钮 |
| | Sidebar 右键上下文菜单 | **已完成** | 新建/删除/重命名/在文件夹中打开 |
| | StatusBar 文件修改状态指示器 | **已完成** | ● 已修改 / 保存后清除 |
| **阶段3：设置面板重构 & 主题升级** | | | |
| | 设置面板Tab内嵌（非弹窗） | **已完成** | SettingsPage + addCustomTab |
| | 多主题工厂架构 | **已完成** | ThemeManager + 3主题(purple/blue/black) |
| | 默认紫色主题 | **已完成** | 全局QSS + 色板变量化 |
| | Tab标签栏UI美化 | **已完成** | 圆角/悬浮/选中强调线/关闭按钮hover |
| | SettingsPage内联样式清理 | **已完成** | 移除setStyleSheet，改用QSS选择器 |
| **阶段4：三方库 & MD功能** | | | |
| | Markdown分屏编辑+实时预览 | **已完成** | MarkdownMode + maddy三方库 |
| | MD预览跟随主题 | **已完成** | MaddyParser::defaultStyleSheet() 动态CSS |
| | QCustomUi 集成 | **未开始** | 需引入三方库，当前用自研QSS替代 |
| **阶段5：架构收口** | | | |
| | 全模块抽象接口 | **已完成** | 15个I接口（见下文） |
| | 工厂模式统一创建 | **部分完成** | UIFactory覆盖6种组件，MD解析器走工厂 |
| | 单例模式 | **已完成** | ConfigManager + ThemeManager（QMutex线程安全） |
| | 观察者模式 | **部分完成** | 文件操作用观察者；主题/配置用Qt信号槽 |
| | 消除static_cast向下转型 | **已完成** | IFileOperator接口扩展attachObserver等 |
| **阶段6：新功能实现** | | | |
| | 文件夹树形结构 | **已完成** | QTreeWidget递归填充 |
| | 代码语法高亮 | **已完成** | Python/C++/JS/JSON，VSCode Dark+配色 |
| | Tab关闭按钮显隐动画 | **已完成** | VSCode风格悬浮时才显示 |
| | Tab切换过渡动画 | **已完成** | 150ms淡入效果 |
| | ConfigManager观察者通知 | **已完成** | configChanged信号 |
| | 侧边栏4面板堆栈 | **已完成** | Explorer/Search/TODO/Extensions |
| | 内嵌终端 | **已完成** | EmbeddedTerminal + QProcess + Ctrl+\` |
| | 搜索功能 | **已完成** | 递归文件内容搜索，双击打开 |
| | 拖拽文件打开 | **已完成** | dragEnterEvent + dropEvent |

---

## 二、接口清单（src/interfaces/）

| 接口 | 实现类 | 状态 |
|------|--------|------|
| IConfigManager | ConfigManager | 已完成 |
| IFileOperator | FileOperator | 已完成（V1.3扩展attachObserver/setContentReader/Writer） |
| IMarkdownParser | MaddyParser / MarkdownParser | 已完成 |
| IEditorEdit | MyTextEdit | 已完成 |
| ICompleter | TextCompleter | 已完成 |
| ILineNumber | LineNumberArea | 已完成 |
| IObserver / ISubject | Widget / Subject | 已完成 |
| IThemeManager | ThemeManager | **V1.3新增** |
| IFramelessWindow | FramelessWindow | **V1.3新增** |
| ITabWidget | EditorTabBar | **V1.3新增** |
| ISideFileBar | SideBar | **V1.3新增** |
| IMarkdownViewer | MarkdownMode | **V1.3新增** |
| ISyntaxHighlighter | CodeSyntaxHighlighter | **V1.3新增** |
| ITerminalWidget | EmbeddedTerminal | **V1.3新增** |
| IUiLibrary | *(无实现)* | **V1.3新增，待实现** |

---

## 三、主题切换链路（已修复）

```
SettingsPage → themeChanged → ThemeManager::switchTheme()
    → qApp->setStyleSheet(palette.generateQSS())  // 全局QSS替换
    → emit ThemeManager::themeChanged(key)
        → SideBar::refreshActivityStyles()         // 侧边栏按钮刷新
        → TextCompleter::applyTheme()              // 补全弹窗样式刷新
        → MarkdownMode::refreshPreview()           // MD预览CSS刷新
        → Widget::onThemeChanged()                 // 所有编辑器行号区重绘
```

---

## 四、Sidebar-Tab 双向同步链路

```
Tab切换 → EditorTabBar::currentChanged
    → Widget::onCurrentEditorChanged()
        → m_tabBar->currentFilePath()
        → m_sideBar->selectFileByPath(filePath)    // Tab→Sidebar高亮

Sidebar双击 → SideBar::fileOpenRequested
    → Widget::onFileOpenFromSidebar()
        → m_tabBar->openFileTab()                  // Sidebar→Tab打开
```

---

## 五、已知BUG清单（按严重程度排序）

### 严重（可能导致崩溃或数据丢失）

| # | 问题 | 文件 | 状态 |
|---|------|------|------|
| 1 | remapTabData()索引重映射错误，关闭非末尾标签后数据丢失 | EditorTabBar.cpp | **已修复** — 改用widget指针匹配重建映射 |
| 2 | 设置页面关闭后悬空指针，再次打开设置崩溃 | widget.cpp | **已修复** — customTabDestroyed信号置空指针 |
| 3 | on_btnOpen_clicked()双重读取，内容写入错误编辑器 | widget.cpp | **已修复** — 移除多余FileOperator调用 |
| 4 | 保存逻辑绕过FileOperator，状态不同步 | widget.cpp | **已修复** — saveFile改为独立QFile写入 |
| 5 | FramelessWindow TopRight/BottomLeft角缩放只调整单轴 | FramelessWindow.cpp | **已修复** — 使用globalPos直接设置角点 |
| 6 | 状态栏objectName不匹配，QSS完全不生效 | UIFactory.cpp | **已修复** — statusBarWidget→statusBar |
| 7 | closeTab保存按钮只发信号不执行实际保存 | EditorTabBar.cpp | **已修复** — saveRequested信号+Widget层执行保存 |

### 中等（功能缺失或不正确）

| # | 问题 | 文件 | 状态 |
|---|------|------|------|
| 8 | 内嵌终端硬编码样式，不跟随主题切换 | EmbeddedTerminal.cpp | **已修复** — applyTheme()动态样式 |
| 9 | QMenu/QMessageBox/QSplitter/QToolTip缺少QSS | ThemeManager.cpp | **已修复** — 新增6组QSS规则 |
| 10 | 设置变更不立即生效（自动保存/补全/行号） | SettingsPage.cpp | 待修复 |
| 11 | FileOperator::openFile()不关闭文件句柄 | FileOperator.cpp | **已修复** — 读取后立即close |
| 12 | closeTab() MarkdownMode标签可能弹出两次保存对话框 | EditorTabBar.cpp | **已修复** — 合并为一次检查 |
| 13 | 搜索功能不支持非UTF-8编码文件 | SideBar.cpp | 待修复 |
| 14 | 静态MaddyParser实例多实例共享+销毁顺序问题 | MarkdownMode.cpp | 待修复 |
| 15 | Markdown编辑/预览无滚动同步 | MarkdownMode.cpp | 待修复 |

### 低（体验不佳或小问题）

| # | 问题 | 文件 | 状态 |
|---|------|------|------|
| 16 | 不支持Windows Aero Snap | FramelessWindow.cpp | 待改进 |
| 17 | 缩放到达最小尺寸后窗口卡住 | FramelessWindow.cpp | 待修复 |
| 18 | main.cpp加载的modern.qss被ThemeManager覆盖 | main.cpp | 待清理 |
| 19 | 右键新建文件不传递目录上下文 | SideBar.cpp | 待改进 |
| 20 | 右键文件夹"在文件夹中打开"打开的是父目录 | widget.cpp | 待修复 |
| 21 | 无Markdown预览模式切换 | MarkdownMode.h | 待改进 |
| 22 | 终端可见性/侧边栏宽度/MD分屏比不持久化 | ConfigManager.h | 待改进 |

---

## 六、架构合规性矩阵

| 审计维度 | 合规状态 | 说明 |
|----------|---------|------|
| 四层架构 | **基本合规** | 接口层→业务层→UI层→工厂层，Widget仍有少量直接依赖 |
| SOLID-依赖倒置 | **基本合规** | 核心模块用接口指针，IFileOperator已扩展消除static_cast |
| 工厂模式 | **部分合规** | UIFactory覆盖6种+MD解析器，TitleBar/SideBar仍直接new |
| 单例模式 | **合规** | ConfigManager(QMutex) + ThemeManager(QMutex) 线程安全 |
| 观察者模式 | **部分合规** | 文件操作用观察者，主题用Qt信号槽，配置缺通知机制 |
| UI库可插拔 | **待改进** | UIFactory返回具体Qt类型，未抽象为IButton等接口 |

---

## 七、性能基线（目标）

| 指标 | 目标 | 当前状态 |
|------|------|---------|
| 启动速度 | ≤500ms | 待测 |
| 空载内存 | ≤30MB | 待测 |
| 大文本滚动 | 60fps | 待测 |
| 主题切换 | 无阻塞 | 已实现 |
