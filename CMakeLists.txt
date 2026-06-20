# 指定CMake最低版本要求
cmake_minimum_required(VERSION 3.16)
# 项目名称
project(scNotebook)

# 设置C++标准为C++17
set(CMAKE_CXX_STANDARD 17)
# 启用Qt的元对象编译器(MOC)自动处理
set(CMAKE_AUTOMOC ON)
# 启用Qt的资源文件(RCC)自动处理
set(CMAKE_AUTORCC ON)
# 禁用UI文件(UIC)自动处理(手动处理UI)
set(CMAKE_AUTOUIC OFF)

# 设置Qt安装路径(根据实际环境修改)
set(QT_PATH "F:/IDE.2/QT/6.5.3/mingw_64")
# 将Qt路径添加到CMake前缀路径,用于查找Qt模块
set(CMAKE_PREFIX_PATH ${QT_PATH})

# 查找项目所需的Qt模块
# Core:核心功能; Widgets:GUI组件; Core5Compat:Qt5兼容性组件(用于编码支持等); Sql:SQLite数据库支持
# PrintSupport:PDF打印/导出功能（MdExporter使用）
find_package(Qt6 COMPONENTS Core Widgets Core5Compat LinguistTools Sql PrintSupport Network REQUIRED)

# ====== 三方库: maddy (C++ header-only Markdown解析器) ======
# 源码存放于 third_party/maddy/（来自 https://github.com/progsource/maddy, MIT license）
# 可直接替换为 cmark/md4c 等其他实现（通过 IMarkdownParser 接口）

# ====== 三方库: libssh2 (SSH2协议C库) ======
# 从源码编译，使用 Qt 自带 MinGW 11.2 构建
# 源码: third_party/libssh2-1.11.1, 构建: third_party/libssh2-build
set(LIBSSH2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libssh2-build")
set(OPENSSL_ROOT "G:/MinGW/mingw64/opt")

# 包含目录设置
include_directories(
    ${QT_PATH}/include                     # Qt根目录头文件
    ${QT_PATH}/include/QtCore              # QtCore模块头文件
    ${QT_PATH}/include/QtGui               # QtGui模块头文件
    ${QT_PATH}/include/QtWidgets           # QtWidgets模块头文件
    ${QT_PATH}/mkspecs/win32-g++           # 编译规格文件
    ${QT_PATH}/include/QtCore5Compat       # Core5Compat模块头文件
    ${QT_PATH}/include/QtNetwork           # QtNetwork模块头文件（SSH模块使用）
    ${CMAKE_CURRENT_SOURCE_DIR}/src        # 项目源文件目录
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party  # 三方库头文件目录 (maddy)
    ${CMAKE_CURRENT_SOURCE_DIR}/utils      # 工具库头文件目录 (Logger)
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libssh2-1.11.1/include  # libssh2 头文件
    ${OPENSSL_ROOT}/include                 # OpenSSL 头文件（libssh2依赖）
)

# 源文件列表（按模块分组，对应目录结构）
set(SOURCES
    # ===== app/ 应用入口层 =====
    src/app/main.cpp                          # 程序入口

    # ===== ui/shell/ 窗口外壳层 =====
    src/ui/shell/Widget.cpp                   # 主窗口实现（阶段2将拆分为 MainWindow + Controllers）
    src/ui/shell/FramelessWindow.cpp          # 无边框窗口基类（DWM磨砂+边缘缩放）
    src/ui/shell/TitleBar.cpp                 # 自定义标题栏组件

    # ===== ui/editor/ 编辑器视图层 =====
    src/ui/editor/MyTextEdit.cpp              # 自定义文本编辑框（IEditorEdit实现）
    src/ui/editor/TextCompleter.cpp           # 智能补全功能（ICompleter实现）
    src/ui/editor/LineNumberArea.cpp          # 行号区域（ILineNumber实现）
    src/ui/editor/EditorTabBar.cpp            # VSCode风格文件标签页栏（ITabWidget实现）
    src/ui/editor/FindReplaceBar.cpp          # 查找替换面板（正则+大小写+全部替换）
    src/ui/editor/EditorSplitView.cpp         # 编辑器分栏视图（共享文档同步编辑）

    # ===== ui/markdown/ Markdown UI 层 =====
    src/ui/markdown/MarkdownMode.cpp          # Markdown分屏编辑预览（IMarkdownViewer实现）
    src/ui/markdown/HtmlPreviewMode.cpp       # HTML分屏编辑预览（三模式切换）
    src/ui/markdown/MdTocPanel.cpp            # Markdown目录树面板
    src/ui/markdown/ImageLightBox.cpp         # 图片灯箱预览（模态窗口+缩放）

    # ===== ui/sidebar/ 侧边栏层 =====
    src/ui/sidebar/SideBar.cpp                # 左侧资源管理面板（ISideFileBar实现）
    src/ui/sidebar/GitPanel.cpp               # 源代码管理面板 UI
    src/ui/sidebar/OutlinePanel.cpp           # 大纲面板（LSP符号/离线正则扫描）
    src/ui/sidebar/TasksPanel.cpp             # 任务面板（任务树/输出/运行停止）

    # ===== ui/terminal/ 终端层 =====
    src/ui/terminal/EmbeddedTerminal.cpp      # 内嵌终端容器（ITerminalWidget实现）
    src/ui/terminal/TerminalView.cpp          # 终端统一视图（ANSI解析+输入输出合一）
    src/ui/terminal/TerminalBackend.cpp       # 终端进程后端（QProcess封装）
    src/ui/terminal/SshTerminalWidget.cpp     # SSH远程终端UI（ITerminalWidget+ISshClient）

    # ===== ui/remote/ 远程文件层 =====
    src/ui/remote/RemoteFileTree.cpp          # 远程文件树（SFTP浏览器）
    src/ui/remote/SshConfigPanel.cpp          # SSH连接配置面板

    # ===== ui/tools/ 工具面板层 =====
    src/ui/tools/CommandPalette.cpp           # 命令面板（Ctrl+Shift+P）
    src/ui/tools/DiffViewer.cpp               # Diff视图（并排对比）
    src/ui/tools/RegexTester.cpp              # 正则表达式测试器
    src/ui/tools/ImagePreviewer.cpp           # 图片文件预览组件
    src/ui/tools/SqliteBrowser.cpp            # SQLite数据库浏览器

    # ===== ui/dialog/ 对话框层 =====
    src/ui/dialog/ModernDialog.cpp            # 现代化弹窗组件

    # ===== ui/settings/ 设置层 =====
    src/ui/settings/SettingsPage.cpp          # 设置页面（标签页内嵌）

    # ===== core/base/ 基础架构层 =====
    src/core/base/Subject.cpp                 # 观察者主题基类
    src/core/base/ScreenGuard.cpp             # 屏幕安全守卫（跨屏/高DPI保护）

    # ===== core/config/ 配置与主题层 =====
    src/core/config/ConfigManager.cpp         # 配置管理器（单例，IConfigManager实现）
    src/core/config/ThemeManager.cpp          # 主题管理器（单例+工厂，IThemeManager实现）
    src/core/config/DefaultUiLibrary.cpp      # 默认UI库实现（IUiLibrary接口）

    # ===== core/fileio/ 文件IO层 =====
    src/core/fileio/FileOperator.cpp          # 文件操作器（观察者，IFileOperator实现）
    src/core/fileio/EncodingDetector.cpp      # 编码检测器（UTF-8/GBK/UTF-16识别）

    # ===== core/editor/ 编辑器核心层 =====
    src/core/editor/CodeSyntaxHighlighter.cpp # 代码语法高亮器（ISyntaxHighlighter实现）
    src/core/editor/CodeHighlighter.cpp       # 代码块HTML高亮后处理
    src/core/editor/HeaderSymbolScanner.cpp   # 头文件符号扫描器（#include/import解析）
    src/core/editor/DoxygenGenerator.cpp      # Doxygen/Docstring 注释生成器
    src/core/editor/CodeFoldingManager.cpp    # 代码折叠管理器（扫描/切换/图标绘制）
    src/core/editor/MinimapRenderer.cpp       # 迷你地图渲染器（缩略图/视口指示/点击跳转）

    # ===== core/format/ 格式化与校验层 =====
    src/core/format/CodeFormatter.cpp         # 代码格式化器（clang-format/内置）
    src/core/format/JsonValidator.cpp         # JSON校验和格式化工具

    # ===== core/markdown/ Markdown核心层 =====
    src/core/markdown/MaddyParser.cpp         # maddy三方库Markdown解析器（IMarkdownParser实现）
    src/core/markdown/MarkdownParser.cpp      # 轻量Markdown解析器（fallback）
    src/core/markdown/MdExporter.cpp          # Markdown导出器（HTML/PDF）
    src/core/markdown/HtmlCssResolver.cpp     # HTML CSS解析器（link标签内联注入）

    # ===== core/snippet/ 代码片段层 =====
    src/core/snippet/SnippetManager.cpp       # 代码片段管理器（CRUD+触发+展开）

    # ===== core/shortcut/ 快捷键层 =====
    src/core/shortcut/ShortcutManager.cpp     # 快捷键管理器（自定义/冲突检测/持久化）
    src/core/shortcut/ShortcutFilter.cpp      # 快捷键过滤器（Command+Filter+Observer）

    # ===== core/lsp/ LSP层（IDE专属）=====
    src/core/lsp/LspClient.cpp                # LSP通信框架（JSON-RPC 2.0，ILspClient实现）
    src/core/lsp/LspManager.cpp               # LSP管理器（门面+工厂，多语言路由）

    # ===== core/vcs/ 版本控制层（IDE专属）=====
    src/core/vcs/GitManager.cpp               # Git版本控制管理器（git.exe方案）
    src/core/vcs/MergeConflictResolver.cpp    # Git合并冲突解决器

    # ===== core/remote/ 远程连接层（IDE专属）=====
    src/core/remote/SshClient.cpp             # SSH客户端核心（libssh2封装，ISshClient实现）
    src/core/remote/SftpClient.cpp            # SFTP客户端（libssh2 SFTP封装）
    src/core/remote/SshSessionManager.cpp     # SSH会话管理器（多连接/配置持久化）

    # ===== core/task/ 任务系统层（IDE专属）=====
    src/core/task/TaskManager.cpp             # 任务管理器（VSCode tasks.json兼容）

    # ===== interfaces/ 接口层（MOC触发文件）=====
    src/interfaces/remote/ISshClient.cpp      # ISshClient MOC触发（纯接口类信号需要MOC）
    src/interfaces/remote/ISftpClient.cpp     # ISftpClient MOC触发（SFTP客户端抽象接口）
    src/interfaces/remote/IRemoteClient.cpp   # IRemoteClient MOC触发（远程会话管理抽象接口）
    src/interfaces/lsp/ILspClient.cpp         # ILspClient MOC触发（LSP客户端抽象接口）
    src/interfaces/vcs/IGitManager.cpp        # IGitManager MOC触发（Git版本控制抽象接口）

    # ===== factory/ 工厂层 =====
    src/factory/UIFactory.cpp                 # UI组件工厂

    # ===== controller/ 控制器层 =====
    src/controller/CommandRegistry.cpp        # 命令注册表（哈希表替代 if-else 链）
    src/controller/EditorActions.cpp          # 编辑器操作（注释切换/大小写/格式化）
    src/controller/FileController.cpp         # 文件 IO 统一入口（消除直通 QFile）
    src/controller/LspCoordinator.cpp         # LSP 信号路由协调器（从 Widget 下沉）
)

# 资源文件列表(图标、配置文件等)
set(RESOURCES
    src/resources.qrc        # 资源文件索引
)

# 创建可执行文件
add_executable(scNotebook ${SOURCES} ${RESOURCES})

# ====== i18n 国际化支持（必须在 add_executable 之后）======
set(TS_FILES
    src/i18n/scNotebook_zh_CN.ts   # 中文翻译（简体）
    src/i18n/scNotebook_en_US.ts   # 英文翻译
)

# 生成 .qm 二进制翻译文件（Qt 6.5.x 兼容）
qt6_add_translations(scNotebook
    TS_FILES ${TS_FILES}
)

# 链接库
if(WIN32)
    target_link_libraries(scNotebook PRIVATE
        Qt6::Core           # 链接Qt核心库
        Qt6::Widgets        # 链接Qt界面库
        Qt6::Core5Compat    # 链接Qt5兼容性库
        Qt6::Sql            # 链接SQLite数据库库
        Qt6::PrintSupport   # 链接打印/PDF导出支持库
        Qt6::Network        # 链接网络库（SSH模块使用QTcpSocket）
        dwmapi             # Windows DWM API（无边框磨砂效果）
        user32             # Windows User API（窗口消息）
        ${LIBSSH2_ROOT}/src/libssh2.a         # libssh2 静态链接（本地编译）
        ${OPENSSL_ROOT}/lib/libssl.a          # OpenSSL (libssh2依赖)
        ${OPENSSL_ROOT}/lib/libcrypto.a       # OpenSSL crypto (libssh2依赖)
        G:/MinGW/mingw64/x86_64-w64-mingw32/lib/libz.a  # zlib (OpenSSL依赖)
        ws2_32             # Windows Sockets (libssh2依赖)
        crypt32            # Windows Certificate Store (OpenSSL依赖)
        advapi32           # Windows Registry API (OpenSSL依赖)
    )
else()
    target_link_libraries(scNotebook PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Core5Compat
        Qt6::Sql
    )
endif()

# Windows平台:复制运行时依赖的Qt动态库到输出目录
if(WIN32)
    # 复制Qt核心动态库
    add_custom_command(TARGET scNotebook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${QT_PATH}/bin/Qt6Core.dll"         # 核心库
            "${QT_PATH}/bin/Qt6Gui.dll"          # 图形库
            "${QT_PATH}/bin/Qt6Widgets.dll"      # 界面组件库
            "${QT_PATH}/bin/Qt6Core5Compat.dll"  # 兼容性库
            "${QT_PATH}/bin/Qt6Network.dll"      # 网络库（SSH模块）
            "${QT_PATH}/bin/Qt6Sql.dll"          # 数据库库(SQLite支持)
            "${QT_PATH}/bin/Qt6PrintSupport.dll" # 打印支持库(Widgets隐式依赖)
            "${QT_PATH}/bin/Qt6Svg.dll"          # SVG图标支持库
            "${QT_PATH}/bin/libgcc_s_seh-1.dll"  # MinGW GCC运行时
            "${QT_PATH}/bin/libstdc++-6.dll"     # MinGW标准库
            "${QT_PATH}/bin/libwinpthread-1.dll" # MinGW线程库
            $<TARGET_FILE_DIR:scNotebook>        # 目标输出目录
    )
    
    # 复制Qt平台插件目录(包含qwindows.dll)
    add_custom_command(TARGET scNotebook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/platforms"       # Qt平台插件目录
            $<TARGET_FILE_DIR:scNotebook>/platforms  # 输出目录的platforms子目录
    )
    
    # 复制Qt样式插件目录
    add_custom_command(TARGET scNotebook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/styles"          # Qt样式插件目录
            $<TARGET_FILE_DIR:scNotebook>/styles  # 输出目录的styles子目录
    )
    
    # 复制Qt图像格式插件目录
    add_custom_command(TARGET scNotebook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/imageformats"    # Qt图像格式插件目录
            $<TARGET_FILE_DIR:scNotebook>/imageformats  # 输出目录的imageformats子目录
    )

    # 复制SVG图标引擎插件目录(iconengines/qsvgicon.dll)
    add_custom_command(TARGET scNotebook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/iconengines"     # SVG图标引擎插件目录
            $<TARGET_FILE_DIR:scNotebook>/iconengines  # 输出目录的iconengines子目录
    )
    
    # 复制应用图标资源(如果有)
    add_custom_command(TARGET scNotebook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/icon"    # 项目图标目录
            $<TARGET_FILE_DIR:scNotebook>/icon        # 输出目录的icon子目录
    )

    # 复制SQLite数据库驱动插件(sqldrivers/qsqlite.dll)
    add_custom_command(TARGET scNotebook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/sqldrivers"            # SQLite驱动插件目录
            $<TARGET_FILE_DIR:scNotebook>/sqldrivers   # 输出目录的sqldrivers子目录
    )
endif()