# SoulCove 现代化 Qt 编辑器 — 开发进度文档（V1.7）

> 最后更新：2026-06-18 | 全链路代码审查 + 6 大新功能 + PDF/弹窗优化 | 编译通过 ✅

---

## 一、需求完成度总览

### 已完成 ✅（55+项）

#### V1.0-V1.6 已完成功能（39项）

| # | 需求项 | 阶段 | 备注 |
|---|--------|------|------|
| 1-39 | V1.6 及之前所有功能 | 1-9 | 见 V1.6 文档 |

#### V1.7 新增已完成功能（16项）

| # | 需求项 | 阶段 | 备注 |
|---|--------|------|------|
| **40** | **现代化弹窗 ModernDialog** | **10** | **替代全部 QMessageBox/QInputDialog，圆角+阴影+淡入动画** |
| **41** | **PDF 导出样式优化** | **10** | **GitHub 风格，pt 单位，内联 style 强制生效，span 包裹正文** |
| **42** | **文件编码自动检测 (T8)** | **10** | **EncodingDetector 集成到 FileOperator，自动识别 UTF-8/GBK/UTF-16** |
| **43** | **文件外部修改监听 (T18)** | **10** | **QFileSystemWatcher，外部修改弹窗提示 reload** |
| **44** | **多光标编辑 (T17)** | **10** | **Alt+Click 添加光标，同步编辑/删除/移动** |
| **45** | **SFTP 客户端封装** | **10** | **SftpClient 类，libssh2 SFTP 封装，目录浏览/上传/下载** |
| **46** | **远程文件树 UI** | **10** | **RemoteFileTree，懒加载/右键菜单/双击下载打开** |
| **47** | **命令面板 Ctrl+Shift+P (T10)** | **10** | **CommandPalette，20+ 命令注册** |
| **48** | **快捷键自定义 (T7)** | **10** | **ShortcutManager + ShortcutFilter** |
| **49** | **括号匹配高亮 (T11)** | **10** | **正向/反向查找，配对高亮** |
| **50** | **代码格式化 (T12)** | **10** | **CodeFormatter，clang-format/内置格式化** |
| **51** | **迷你地图 Minimap (T16)** | **10** | **80px 宽，200ms 延迟更新，点击跳转** |
| **52** | **Git 面板 (T13)** | **10** | **GitPanel + GitManager，diff 查看/分支管理** |
| **53** | **LSP 客户端框架 (T14)** | **10** | **LspClient，JSON-RPC 2.0 通信** |
| **54** | **代码片段管理 (M14)** | **10** | **SnippetManager，命令面板触发** |
| **55** | **任务管理 (M15)** | **10** | **TaskManager，VSCode tasks.json 兼容** |

### 待完成 🔄

| # | 需求项 | 优先级 | 说明 |
|---|--------|--------|------|
| T19 | SFTP 远程文件编辑集成 | 中 | RemoteFileTree → SshConfigPanel 联动 → 标签页远程文件编辑 |
| T20 | 断线重连机制 | 中 | SSH 断连检测 + 指数退避重连 |
| T21 | 终端分屏 (T6) | 低 | 水平分割多终端 |
| T22 | 扩展/插件系统 (T15) | 低 | 第三方功能扩展接口 |
| T23 | 文件同步 | 低 | 本地↔远程双向同步 |

---

## 二、V1.7 本次迭代变更详情

### 2.1 现代化弹窗 ModernDialog

**新建文件**: `src/ui/ModernDialog.h/cpp`

替代全项目 40+ 处 `QMessageBox::` 和 `QInputDialog::` 调用：
- 圆角(12px) + 阴影 + 淡入动画(150ms)
- 自动适配 ThemeManager 主题色
- 支持 Info / Warning / Question / Error 四种图标
- 静态工厂方法：`information()`, `warning()`, `critical()`, `question()`, `confirm()`, `getText()`, `getItem()`

**涉及文件**: widget.cpp, EditorTabBar.cpp, MarkdownMode.cpp, SettingsPage.cpp, GitPanel.cpp, SqliteBrowser.cpp, SshConfigPanel.cpp

### 2.2 PDF 导出样式优化

**修改文件**: `src/core/MdExporter.cpp`

- 全部改用 `pt` 单位（分辨率无关）
- 标题字号：h1=20pt / h2=17pt / h3=15pt / h4=14pt / h5-h6=13pt
- 代码块：GitHub 风格 `#f6f8fa` 背景 + `#1a1a1a` 文字
- 表格：`#d0d7de` 边框 + `#f6f8fa` 表头底色
- `<span style="font-size:14pt;">` 包裹 `<p>` 内容，绕过 QTextDocument 对块级元素 font-size 的忽略
- `<h1>-<h6>` 内联 style 强制覆盖 QTextDocument 内置放大

### 2.3 文件编码自动检测 (T8)

**修改文件**: `src/core/FileOperator.cpp`

- 集成 `EncodingDetector::detect()` 到 `openFile()` 流程
- 当编码为 "Auto" 或 "UTF-8"（默认）时自动检测
- 支持：UTF-8/UTF-8 BOM/UTF-16LE/UTF-16BE/GBK/GB18030/ISO-8859-1/ASCII
- BOM 检测 + 统计特征分析（字节分布、高频字符）

### 2.4 文件外部修改监听 (T18)

**修改文件**: `src/widget.h/cpp`

- `QFileSystemWatcher` 监听当前标签页文件
- 外部修改时弹窗提示（ModernDialog）
- 内部保存时抑制监听（`m_suppressFileWatch` 标志）
- 切换标签页时自动更新监听路径

### 2.5 多光标编辑 (T17)

**修改文件**: `src/mytextedit.h/cpp`

- Alt+Click 添加次级光标
- 同步编辑：字符输入/Backspace/Delete/Enter/方向键
- ExtraSelection 高亮显示次级光标
- Esc 清除所有次级光标
- 与括号匹配高亮共存

### 2.6 SFTP 客户端 (SSH Phase 3)

**新建文件**: `src/core/SftpClient.h/cpp`

- 基于 libssh2 SFTP API 封装
- 目录操作：listDir/mkdir/remove/rename/exists
- 文件传输：download/upload（4KB 分块，进度回调）
- 流式读写：readFile/writeFile
- ScopedBlocking RAII 守卫（阻塞模式切换）

**修改文件**: `src/core/SshClient.h` — 添加 `rawSession()` / `rawSocket()` 访问器

### 2.7 远程文件树 UI

**新建文件**: `src/ui/RemoteFileTree.h/cpp`

- QTreeWidget 继承，懒加载目录
- 双击文件 → 下载到临时目录 → 发出 fileOpenRequested 信号
- 右键菜单：打开/下载/上传/删除/重命名/新建文件夹/刷新
- 📁/📄 emoji 图标，与本地文件树风格一致

### 2.8 代码审查修复（P0-P3）

| 优先级 | 问题 | 修复 |
|--------|------|------|
| P0 | m_sshConfigPanel 悬空指针 | customTabDestroyed 回调补充 SSH 配置置空 |
| P0 | bindCurrentEditor 信号槽连接累积 | 正确 disconnect 旧 editor + Qt::UniqueConnection |
| P1 | ModernDialog findChild 空指针 | 判空后调用 |
| P1 | 5 处硬编码绝对路径 | 替换为 applicationDirPath() |
| P2 | 过时注释 + qDebug 死代码 | 清理 |
| P3 | 冗余 #include | 删除 |

### 2.9 孤儿文件清理

- 删除 `src/ui/SshConfigDialog.h/cpp`（完整实现但未编译，功能已被 SshConfigPanel 替代）

---

## 三、接口清单（src/interfaces/）

| 接口 | 实现类 | 状态 |
|------|--------|------|
| IConfigManager | ConfigManager | ✅ |
| IFileOperator | FileOperator | ✅ T8 编码检测集成 |
| IMarkdownParser | MaddyParser / MarkdownParser | ✅ |
| IEditorEdit | MyTextEdit | ✅ T17 多光标 |
| ICompleter | TextCompleter | ✅ |
| ILineNumber | LineNumberArea | ✅ |
| IObserver / ISubject | Widget / Subject | ✅ |
| IThemeManager | ThemeManager | ✅ |
| IFramelessWindow | FramelessWindow | ✅ |
| ITabWidget | EditorTabBar | ✅ |
| ISideFileBar | SideBar | ✅ |
| IMarkdownViewer | MarkdownMode | ✅ |
| ISyntaxHighlighter | CodeSyntaxHighlighter | ✅ |
| ITerminalWidget | EmbeddedTerminal | ✅ |
| ISshClient | SshClient | ✅ + SftpClient 扩展 |
| IUiLibrary | DefaultUiLibrary | ✅ |

---

## 四、项目文件结构（V1.7 更新）

```
SoulCoveFinal/
├── src/
│   ├── main.cpp
│   ├── widget.h/cpp                # 主窗口（+T18 文件监听）
│   ├── mytextedit.h/cpp            # 编辑器（+T17 多光标）
│   ├── textCompleter.h/cpp
│   ├── lineNumberArea.h/cpp
│   │
│   ├── core/
│   │   ├── ThemeManager.h/cpp
│   │   ├── ConfigManager.h/cpp
│   │   ├── FileOperator.h/cpp      # +T8 编码自动检测
│   │   ├── EncodingDetector.h/cpp  # T8 编码检测器
│   │   ├── CodeSyntaxHighlighter   # 语法高亮
│   │   ├── CodeFormatter.h/cpp     # T12 代码格式化
│   │   ├── CodeHighlighter.h/cpp   # 代码块高亮
│   │   ├── MarkdownParser.h/cpp    # MD 解析器
│   │   ├── MdExporter.h/cpp        # MD 导出 HTML/PDF
│   │   ├── MaddyParser.h/cpp       # maddy 三方库
│   │   ├── GitManager.h/cpp        # Git 管理
│   │   ├── SnippetManager.h/cpp    # 代码片段
│   │   ├── TaskManager.h/cpp       # 任务管理
│   │   ├── LspClient.h/cpp         # LSP 客户端
│   │   ├── ShortcutManager.h/cpp   # T7 快捷键管理
│   │   ├── ShortcutFilter.h/cpp    # T7 快捷键过滤
│   │   ├── JsonValidator.h/cpp     # JSON 校验
│   │   ├── Subject.h/cpp           # 观察者基类
│   │   ├── DefaultUiLibrary.h/cpp  # IUiLibrary 实现
│   │   ├── SshClient.h/cpp         # SSH 客户端 (+rawSession)
│   │   ├── SshSessionManager.h/cpp # SSH 会话管理
│   │   └── SftpClient.h/cpp        # [新] SFTP 客户端
│   │
│   ├── ui/
│   │   ├── TitleBar.h/cpp
│   │   ├── SideBar.h/cpp
│   │   ├── EditorTabBar.h/cpp
│   │   ├── EmbeddedTerminal.h/cpp
│   │   ├── TerminalView.h/cpp
│   │   ├── TerminalBackend.h/cpp
│   │   ├── SettingsPage.h/cpp
│   │   ├── MarkdownMode.h/cpp
│   │   ├── FramelessWindow.h/cpp
│   │   ├── CommandPalette.h/cpp    # T10 命令面板
│   │   ├── DiffViewer.h/cpp        # 差异对比
│   │   ├── GitPanel.h/cpp          # Git 面板
│   │   ├── RegexTester.h/cpp       # 正则测试器
│   │   ├── ImagePreviewer.h/cpp    # 图片预览
│   │   ├── ImageLightBox.h/cpp     # 图片灯箱
│   │   ├── SqliteBrowser.h/cpp     # SQLite 浏览器
│   │   ├── MdTocPanel.h/cpp        # MD 目录面板
│   │   ├── ModernDialog.h/cpp      # [新] 现代化弹窗
│   │   ├── SshConfigPanel.h/cpp    # SSH 配置面板
│   │   ├── SshTerminalWidget.h/cpp # SSH 远程终端
│   │   └── RemoteFileTree.h/cpp    # [新] 远程文件树
│   │
│   ├── interfaces/                 # 抽象接口层（16个接口）
│   └── factory/UIFactory.h/cpp
│
├── thirdparty/libssh2/             # SSH 库
├── config/sys_param.ini
├── styles/modern.qss
├── CMakeLists.txt                  # +SftpClient +RemoteFileTree +ModernDialog
└── Files/                          # 项目文档
```

---

## 五、版本变更记录

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| V1.0→V1.3 | 06月初 | 基础功能迭代 |
| V1.4 | 06-15 | 全量审计 |
| V1.5 | 06-16 | T1-T3 完成 |
| V1.6 | 06-16 | 终端模块化重构 + 全链路审查 |
| **V1.7** | **06-18** | **ModernDialog + PDF优化 + T8编码检测 + T18文件监听 + T17多光标 + SFTP + RemoteFileTree + P0-P3修复 + 16项功能文档同步** |
