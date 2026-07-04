# 指定CMake最低版本要求
cmake_minimum_required(VERSION 3.16)
# 项目名称
project(SoulCove)

# 设置C++标准为C++17
set(CMAKE_CXX_STANDARD 17)

# Windows平台：阻止windows.h自动包含旧的winsock.h
# WIN32_LEAN_AND_MEAN: 减少windows.h的包含内容
# -include winsock2.h: 强制在每个编译单元最开始包含winsock2.h
#   这样winsock2.h会先定义_WINSOCKAPI_，阻止windows.h包含旧的winsock.h
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include winsock2.h")
endif()
# 启用Qt的元对象编译器(MOC)自动处理
set(CMAKE_AUTOMOC ON)
# 启用Qt的资源文件(RCC)自动处理
set(CMAKE_AUTORCC ON)
# 禁用UI文件(UIC)自动处理(手动处理UI)
set(CMAKE_AUTOUIC OFF)

# 导出编译数据库 compile_commands.json（供 clangd 等 LSP 服务器解析项目结构）
# clangd 会自动在源文件父目录及 build/ 子目录中查找此文件
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ====== P1 C05-1: 构建配置选项（条件编译）======
# 通过 -DSCNB_WITH_OPENSSL=OFF / -DSCNB_WITH_SSH=OFF 可禁用相应支持
option(SCNB_WITH_OPENSSL "Enable OpenSSL support (required by SSH/libssh2)" ON)
option(SCNB_WITH_SSH "Enable SSH remote support (libssh2 + SFTP)" ON)
# Qt 安装前缀：显式指定时优先级高于环境变量
# 用法：cmake -DSCNB_QT_PREFIX=F:/IDE.2/QT/6.5.3/mingw_64 ..
set(SCNB_QT_PREFIX "" CACHE PATH "Qt installation prefix (e.g. C:/Qt/6.5.3/mingw_64)")

# ====== P1 C05-1: Qt 路径自动检测（find_package + 环境变量）======
# 优先级（从高到低）：
#   1. -DSCNB_QT_PREFIX=...          （显式指定，最高优先级）
#   2. -DCMAKE_PREFIX_PATH=...       （CMake 变量）
#   3. 环境变量 Qt6_DIR               （指向 lib/cmake/Qt6）
#   4. 环境变量 CMAKE_PREFIX_PATH     （多路径分号分隔）
if(SCNB_QT_PREFIX)
    # 显式指定的 SCNB_QT_PREFIX 优先级最高，追加到 CMAKE_PREFIX_PATH 头部
    list(PREPEND CMAKE_PREFIX_PATH "${SCNB_QT_PREFIX}")
    message(STATUS "SCNB_QT_PREFIX 已设置: ${SCNB_QT_PREFIX}")
elseif(DEFINED ENV{Qt6_DIR})
    # Qt6_DIR 环境变量（形如 .../lib/cmake/Qt6），通过 CMAKE_PREFIX_PATH 间接传递
    get_filename_component(_qt6_prefix_from_dir "$ENV{Qt6_DIR}/../../.." ABSOLUTE)
    if(EXISTS "${_qt6_prefix_from_dir}")
        list(PREPEND CMAKE_PREFIX_PATH "${_qt6_prefix_from_dir}")
        message(STATUS "从环境变量 Qt6_DIR 检测到 Qt 前缀: ${_qt6_prefix_from_dir}")
    endif()
endif()

# 兜底：若 CMAKE_PREFIX_PATH 仍为空，尝试从环境变量继承
if(NOT CMAKE_PREFIX_PATH AND DEFINED ENV{CMAKE_PREFIX_PATH})
    set(CMAKE_PREFIX_PATH $ENV{CMAKE_PREFIX_PATH})
endif()

# 查找项目所需的Qt模块（REQUIRED：找不到则配置失败）
# Core:核心功能; Widgets:GUI组件; Core5Compat:Qt5兼容性组件(用于编码支持等); Sql:SQLite数据库支持
# PrintSupport:PDF打印/导出功能（MdExporter使用）; Concurrent:异步扫描（OutlinePanel线程化）
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Core5Compat LinguistTools Sql PrintSupport Network Concurrent)

# 输出检测到的 Qt 版本信息（P1 C05-1 状态信息）
message(STATUS "Qt6 版本: ${Qt6_VERSION}")
message(STATUS "Qt6 目录: ${Qt6_DIR}")

# 从 Qt6 imported target 反推安装路径（用于 include_directories 和 DLL 拷贝）
# 替代原来的硬编码 set(QT_PATH "F:/IDE.2/QT/6.5.3/mingw_64")
get_target_property(_qt_core_location Qt6::Core LOCATION)
get_filename_component(QT_BIN_DIR "${_qt_core_location}" DIRECTORY)
get_filename_component(QT_PATH "${QT_BIN_DIR}" DIRECTORY)
message(STATUS "Qt 安装前缀(反推): ${QT_PATH}")

# ====== 三方库: maddy (C++ header-only Markdown解析器) ======
# 源码存放于 third_party/maddy/（来自 https://github.com/progsource/maddy, MIT license）
# 可直接替换为 cmark/md4c 等其他实现（通过 IMarkdownParser 接口）

# ====== 三方库: libssh2 (SSH2协议C库) ======
# 从源码编译，使用 Qt 自带 MinGW 11.2 构建
# 源码: third_party/libssh2 (git submodule), 构建: third_party/libssh2-build
if(SCNB_WITH_SSH)
    set(LIBSSH2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libssh2-build")
endif()

# ====== P1 C05-1: OpenSSL（find_package + 条件编译）======
# SCNB_WITH_OPENSSL=ON 时启用 OpenSSL 支持（libssh2/SSH 依赖）
# 优先 find_package 检测；找不到则回退到 OPENSSL_ROOT_DIR 环境变量（兼容 MinGW 自编译 OpenSSL）
if(SCNB_WITH_OPENSSL)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL: find_package 成功 版本=${OPENSSL_VERSION}")
    else()
        message(STATUS "OpenSSL: find_package 未找到，尝试 OPENSSL_ROOT_DIR 环境变量")
    endif()
    # 兼容回退：OPENSSL_ROOT_DIR 环境变量（MinGW 自编译 OpenSSL 无 CMake config 时使用）
    if(NOT DEFINED OPENSSL_ROOT_DIR AND DEFINED ENV{OPENSSL_ROOT_DIR})
        set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
    endif()
    # 自动检测 OpenSSL 3.x（libssh2 1.11 需要 OpenSSL 3.0+ API）
    # 候选路径：环境变量 → 当前 MinGW opt → G:/MinGW/mingw64/opt（用户自装 MinGW）
    if(NOT OPENSSL_ROOT_DIR AND CMAKE_CXX_COMPILER)
        get_filename_component(_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_mingw_root "${_mingw_bin_dir}/.." ABSOLUTE)
        set(_openssl_candidates
            "${_mingw_root}/opt"
            "G:/MinGW/mingw64/opt"
        )
        foreach(_cand ${_openssl_candidates})
            if(EXISTS "${_cand}/lib/libssl.a" AND EXISTS "${_cand}/include/openssl/opensslv.h")
                file(READ "${_cand}/include/openssl/opensslv.h" _ov_content)
                if(_ov_content MATCHES "OPENSSL_VERSION_MAJOR[ \t]+3")
                    set(OPENSSL_ROOT_DIR "${_cand}")
                    message(STATUS "OpenSSL: 自动检测到 OpenSSL 3.x: ${OPENSSL_ROOT_DIR}")
                    break()
                elseif(NOT OPENSSL_ROOT_DIR)
                    # 记录 1.x 作为回退（仅当没有 3.x 时）
                    set(_openssl_fallback "${_cand}")
                endif()
            endif()
        endforeach()
        if(NOT OPENSSL_ROOT_DIR AND _openssl_fallback)
            set(OPENSSL_ROOT_DIR "${_openssl_fallback}")
            message(STATUS "OpenSSL: 仅检测到 1.x（libssh2 可能不兼容）: ${OPENSSL_ROOT_DIR}")
        endif()
    endif()
    set(OPENSSL_ROOT ${OPENSSL_ROOT_DIR})
    message(STATUS "OpenSSL 根目录: ${OPENSSL_ROOT}")
    # OpenSSL 找不到时联动禁用 SSH（libssh2 依赖 OpenSSL），避免链接失败
    if(NOT OpenSSL_FOUND AND NOT OPENSSL_ROOT)
        message(STATUS "OpenSSL 未找到，自动禁用 SSH 远程支持")
        set(SCNB_WITH_SSH OFF CACHE BOOL "SSH disabled (OpenSSL not found)" FORCE)
    endif()
else()
    message(STATUS "OpenSSL: 已通过 SCNB_WITH_OPENSSL=OFF 禁用")
    set(OPENSSL_ROOT "")
    # OpenSSL 禁用时联动禁用 SSH（libssh2 依赖 OpenSSL）
    set(SCNB_WITH_SSH OFF CACHE BOOL "SSH disabled (OpenSSL disabled)" FORCE)
endif()

# ====== zlib（OpenSSL/libssh2 依赖，仅 SSH 启用时需要）======
if(SCNB_WITH_SSH)
    if(NOT DEFINED ZLIB_ROOT AND DEFINED ENV{ZLIB_ROOT})
        set(ZLIB_ROOT $ENV{ZLIB_ROOT})
    endif()
    # 自动检测 zlib（优先与 OpenSSL 同一 MinGW 发行版）
    if(NOT ZLIB_ROOT AND CMAKE_CXX_COMPILER)
        get_filename_component(_mingw_bin_dir2 "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_mingw_root2 "${_mingw_bin_dir2}/.." ABSOLUTE)
        # 从 OpenSSL 路径推断对应的 MinGW 根目录（如 G:/MinGW/mingw64）
        set(_zlib_mingw_roots "${_mingw_root2}")
        if(OPENSSL_ROOT_DIR)
            get_filename_component(_ssl_mingw_root "${OPENSSL_ROOT_DIR}/.." ABSOLUTE)
            list(PREPEND _zlib_mingw_roots "${_ssl_mingw_root}")
        endif()
        foreach(_zroot ${_zlib_mingw_roots})
            if(EXISTS "${_zroot}/x86_64-w64-mingw32/lib/libz.a")
                set(ZLIB_ROOT "${_zroot}/x86_64-w64-mingw32")
                message(STATUS "zlib: 自动检测到 zlib: ${ZLIB_ROOT}")
                break()
            elseif(EXISTS "${_zroot}/opt/lib/libz.a")
                set(ZLIB_ROOT "${_zroot}/opt")
                message(STATUS "zlib: 自动检测到 opt zlib: ${ZLIB_ROOT}")
                break()
            endif()
        endforeach()
    endif()
    message(STATUS "zlib 路径: ${ZLIB_ROOT}")
endif()

message(STATUS "SSH 远程支持: ${SCNB_WITH_SSH}")

# 包含目录设置
# 注：Qt6::Core 等 imported target 已自动提供 Qt 头文件路径，此处保留显式包含仅为兼容旧代码
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
)
# P1 C05-1: libssh2/OpenSSL 头文件按条件包含（SCNB_WITH_SSH 禁用时不包含）
if(SCNB_WITH_SSH)
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libssh2/include  # libssh2 头文件
        ${OPENSSL_ROOT}/include                 # OpenSSL 头文件（libssh2依赖）
    )
endif()

# 源文件列表（按模块分组，对应目录结构）
set(SOURCES
    # ===== ui/shell/ 窗口外壳层 =====
    src/ui/shell/Widget.cpp                   # 主窗口实现（阶段2将拆分为 MainWindow + Controllers）
    src/ui/shell/FramelessWindow.cpp          # 无边框窗口基类（DWM磨砂+边缘缩放）
    src/ui/shell/TitleBar.cpp                 # 自定义标题栏组件

    # ===== ui/editor/ 编辑器视图层 =====
    src/ui/editor/MyTextEdit.cpp              # 自定义文本编辑框（IEditorEdit实现）
    src/ui/editor/TextCompleter.cpp           # 智能补全功能（ICompleter实现）
    src/ui/editor/CompletionIcons.cpp         # C04-10: 补全项分类标注图标（单例，代码绘制）
    src/ui/editor/CompletionPreviewWidget.cpp # C04-11: 补全项预览面板（签名/文档/snippet展开）
    src/ui/editor/LineNumberArea.cpp          # 行号区域（ILineNumber实现）
    src/ui/editor/EditorTabBar.cpp            # VSCode风格文件标签页栏（ITabWidget实现）
    src/ui/editor/FindReplaceBar.cpp          # 查找替换面板（正则+大小写+全部替换）
    src/ui/editor/EditorSplitView.cpp         # 编辑器分栏视图（共享文档同步编辑）
    src/ui/editor/HoverPopup.cpp              # H1: Markdown 悬停预览弹窗（富文本渲染）
    src/ui/editor/DefinitionPreviewPopup.cpp  # C03-6: 定义预览弹窗（Ctrl+Alt+Click 悬浮）

    # ===== ui/markdown/ Markdown UI 层 =====
    src/ui/markdown/MarkdownMode.cpp          # Markdown分屏编辑预览（IMarkdownViewer实现）
    src/ui/markdown/HtmlPreviewMode.cpp       # HTML分屏编辑预览（三模式切换）
    src/ui/markdown/MdTocPanel.cpp            # Markdown目录树面板
    src/ui/markdown/ImageLightBox.cpp         # 图片灯箱预览（模态窗口+缩放）

    # ===== ui/sidebar/ 侧边栏层 =====
    src/ui/sidebar/SideBar.cpp                # 左侧资源管理面板（ISideFileBar实现）
    src/ui/sidebar/GitPanel.cpp               # 源代码管理面板 UI
    src/ui/sidebar/GitHistoryPanel.cpp        # P2-H03 子项2: Git 提交历史面板
    src/ui/sidebar/OutlinePanel.cpp           # 大纲面板（LSP符号/离线正则扫描）
    src/ui/sidebar/TasksPanel.cpp             # 任务面板（任务树/输出/运行停止）
    src/ui/sidebar/SearchPanel.cpp            # 搜索面板（文本/正则/符号搜索+全局替换）
    src/ui/sidebar/ExplorerPanel.cpp          # 资源管理器面板（文件树/工具栏/拖拽移动）

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

    # ===== ui/snippet/ 代码片段管理 UI 层（P2-H02）=====
    src/ui/snippet/SnippetManagerDialog.cpp   # 代码片段管理对话框（CRUD+导入导出+VSCode兼容）

    # ===== ui/settings/ 设置层 =====
    src/ui/settings/SettingsPage.cpp          # 设置页面（标签页内嵌）

    # ===== ui/shortcut/ 快捷键 UI 层（P2-H05 子项3）=====
    src/ui/shortcut/KeySequenceEdit.cpp       # 快捷键录制输入框（双击录制/Backspace/Esc）

    # ===== ui/debug/ 调试视图层（P3-M04 子项3）=====
    src/ui/debug/DebugView.cpp                # 调试视图面板（变量表/调用栈/断点列表/工具栏）

    # ===== core/base/ 基础架构层 =====
    src/core/base/Subject.cpp                 # 观察者主题基类
    src/core/base/ScreenGuard.cpp             # 屏幕安全守卫（跨屏/高DPI保护）

    # ===== core/debug/ 调试与性能监控层 =====
    src/core/debug/PerformanceMonitor.cpp     # C02-4 性能监控面板（调试模式，单例）
    src/core/debug/DebugManager.cpp           # P3-M04 子项3: GDB MI 调试管理器（QProcess驱动）

    # ===== core/config/ 配置与主题层 =====
    src/core/config/ConfigManager.cpp         # 配置管理器（单例，IConfigManager实现）
    src/core/config/ThemeManager.cpp          # 主题管理器（单例+工厂，IThemeManager实现）
    src/core/config/DefaultUiLibrary.cpp      # 默认UI库实现（IUiLibrary接口）

    # ===== core/i18n/ 国际化与本地化层（P3-M05）=====
    src/core/i18n/I18nManager.cpp             # 国际化管理器（单例，QTranslator热插拔+QLocale）

    # ===== core/build/ 构建配置层（P1 C05-3）=====
    src/core/build/QtDetector.cpp             # Qt 安装检测器（扫描系统 Qt 版本，供构建设置页使用）

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
    src/core/editor/EditorConfigParser.cpp    # P3-M03 子项4: .editorconfig 解析器（glob匹配/INI解析）
    src/core/editor/SpellChecker.cpp          # P3-M03 子项5: 轻量拼写检查器（内置+用户词典/编辑距离建议）

    # ===== core/format/ 格式化与校验层 =====
    src/core/format/CodeFormatter.cpp         # 代码格式化器（clang-format/内置）
    src/core/format/JsonValidator.cpp         # JSON校验和格式化工具

    # ===== core/markdown/ Markdown核心层 =====
    src/core/markdown/MaddyParser.cpp         # maddy三方库Markdown解析器（IMarkdownParser实现）
    src/core/markdown/MarkdownParser.cpp      # 轻量Markdown解析器（fallback）
    src/core/markdown/MdExporter.cpp          # Markdown导出器（HTML/PDF）
    src/core/markdown/HtmlCssResolver.cpp     # HTML CSS解析器（link标签内联注入）
    src/core/markdown/MermaidRenderer.cpp     # P3-M02 子项5: Mermaid图表渲染器（mmdc调用+缓存）

    # ===== core/snippet/ 代码片段层 =====
    src/core/snippet/SnippetManager.cpp       # 代码片段管理器（CRUD+触发+展开）

    # ===== core/shortcut/ 快捷键层 =====
    src/core/shortcut/ShortcutManager.cpp     # 快捷键管理器（自定义/冲突检测/持久化）
    src/core/shortcut/ShortcutFilter.cpp      # 快捷键过滤器（Command+Filter+Observer）

    # ===== core/lsp/ LSP层（IDE专属）=====
    src/core/lsp/LspClient.cpp                # LSP通信框架（JSON-RPC 2.0，ILspClient实现）
    src/core/lsp/LspManager.cpp               # LSP管理器（门面+工厂，多语言路由）
    src/core/lsp/LanguageRegistry.cpp         # R2: 语言注册表（langId↔suffix 单一数据源）

    # ===== core/vcs/ 版本控制层（IDE专属）=====
    src/core/vcs/GitManager.cpp               # Git版本控制管理器（git.exe方案）
    src/core/vcs/GitBlameReader.cpp           # P2-H03 子项3: Git blame 行级标注解析器
    src/core/vcs/MergeConflictResolver.cpp    # Git合并冲突解决器

    # ===== core/remote/ 远程连接层（IDE专属）=====
    src/core/remote/SshClient.cpp             # SSH客户端核心（libssh2封装，ISshClient实现）
    src/core/remote/SftpClient.cpp            # SFTP客户端（libssh2 SFTP封装）
    src/core/remote/SshSessionManager.cpp     # SSH会话管理器（多连接/配置持久化）
    # P3-M01 远程开发增强：缓存/LSP部署/工作区挂载
    src/core/remote/RemoteFileCache.cpp       # 远程文件缓存（LRU 100MB + mtime 校验 + 会话隔离）
    src/core/remote/RemoteLspDeployer.cpp     # 远程 LSP(clangd) 自动部署（检测/上传/校验）
    src/core/remote/RemoteWorkspaceManager.cpp# 远程工作区挂载（SFTP 双向同步替代真实挂载）

    # ===== core/task/ 任务系统层（IDE专属）=====
    src/core/task/TaskManager.cpp             # 任务管理器（VSCode tasks.json兼容）
    src/core/task/TasksJsonParser.cpp         # P3-M04 子项2: VSCode tasks.json 解析/序列化

    # ===== core/workspace/ 工作区层（P2-H04 多文件夹工作区）=====
    src/core/workspace/WorkspaceManager.cpp   # 工作区管理器（单例，.scnb-workspace 持久化）

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
    src/controller/IdleTabTracker.cpp         # R4: 闲置标签页追踪器（SRP，从 EditorTabBar 提取）
)

# 资源文件列表(图标、配置文件等)
set(RESOURCES
    src/resources.qrc        # 资源文件索引
)

# ====== 产品线构建（notebook → editor → IDE）======
# 所有产品共享同一份核心源码（静态库 scCore），通过不同的 main.cpp 入口
# 和 ProductConfig 配置实现功能差异化。

# ====== P1 C05-4: 多构建目录支持（Debug/Release 分离）======
# 当显式指定 CMAKE_BUILD_TYPE 时（单配置生成器如 MinGW Makefiles），
# 按构建类型分离输出目录：build/Debug/、build/Release/
# 未指定时输出到 build/ 根目录，保持向后兼容（现有 build/ 目录不受影响）
if(CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    message(STATUS "多构建目录: 输出到 ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
else()
    message(STATUS "多构建目录: 未启用（CMAKE_BUILD_TYPE 未指定），输出到 ${CMAKE_BINARY_DIR}")
endif()

# 创建静态库（所有产品共享的核心源码）
add_library(scCore STATIC ${SOURCES} ${RESOURCES})

# P1 C05-1: 条件编译定义（供源码 #ifdef 使用）
if(SCNB_WITH_OPENSSL)
    target_compile_definitions(scCore PUBLIC SCNB_WITH_OPENSSL)
endif()
if(SCNB_WITH_SSH)
    target_compile_definitions(scCore PUBLIC SCNB_WITH_SSH)
endif()

# 链接库到静态库
if(WIN32)
    target_link_libraries(scCore PUBLIC
        Qt6::Core           # 链接Qt核心库
        Qt6::Widgets        # 链接Qt界面库
        Qt6::Core5Compat    # 链接Qt5兼容性库
        Qt6::Sql            # 链接SQLite数据库库
        Qt6::PrintSupport   # 链接打印/PDF导出支持库
        Qt6::Network        # 链接网络库（SSH模块使用QTcpSocket）
        Qt6::Concurrent     # 链接并发库（OutlinePanel异步扫描）
        dwmapi             # Windows DWM API（无边框磨砂效果）
        user32             # Windows User API（窗口消息）
    )
    # P1 C05-1: libssh2/OpenSSL/zlib 仅在 SSH 启用时链接（条件编译）
    if(SCNB_WITH_SSH)
        target_link_libraries(scCore PUBLIC
            ${LIBSSH2_ROOT}/src/libssh2.a         # libssh2 静态链接（本地编译）
            ${OPENSSL_ROOT}/lib/libssl.a          # OpenSSL (libssh2依赖)
            ${OPENSSL_ROOT}/lib/libcrypto.a       # OpenSSL crypto (libssh2依赖)
            ${ZLIB_ROOT}/lib/libz.a               # zlib (OpenSSL依赖，路径由 ZLIB_ROOT 环境变量配置)
            ws2_32             # Windows Sockets (libssh2依赖)
            crypt32            # Windows Certificate Store (OpenSSL依赖)
            advapi32           # Windows Registry API (OpenSSL依赖)
        )
    endif()
else()
    target_link_libraries(scCore PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Core5Compat
        Qt6::Sql
        Qt6::Concurrent
    )
endif()

# ====== i18n 国际化支持 ======
set(TS_FILES
    src/i18n/SoulCove_zh_CN.ts   # 中文翻译（简体）
    src/i18n/SoulCove_en_US.ts   # 英文翻译
)

# ====== 产品1: SoulCove IDE (全功能) ======
# 产品线终点：完整IDE，支持LSP/终端/Git/任务/大纲
add_executable(SoulCoveIDE products/ide/main.cpp)
target_link_libraries(SoulCoveIDE PRIVATE scCore)

qt6_add_translations(SoulCoveIDE
    TS_FILES ${TS_FILES}
)

# ====== 产品2: SoulCove Notebook (代码编辑器) ======
# 产品线中段：支持文件树/搜索/格式化，无LSP/终端/Git
add_executable(SoulCoveNotebook products/editor/main.cpp)
target_link_libraries(SoulCoveNotebook PRIVATE scCore)

qt6_add_translations(SoulCoveNotebook
    TS_FILES ${TS_FILES}
)

# ====== 产品3: SoulCove Notebook Lite (纯文本编辑器) ======
# 产品线起点：纯文本编辑 + Markdown 预览
add_executable(SoulCoveNotebookLite products/notebook/main.cpp)
target_link_libraries(SoulCoveNotebookLite PRIVATE scCore)

qt6_add_translations(SoulCoveNotebookLite
    TS_FILES ${TS_FILES}
)

# Windows平台:复制运行时依赖的Qt动态库到输出目录
# 注：三个可执行文件共享同一输出目录，DLL 只需拷贝一次（绑定到 SoulCoveIDE 目标）
if(WIN32)
    # 复制Qt核心动态库
    add_custom_command(TARGET SoulCoveIDE POST_BUILD
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
            $<TARGET_FILE_DIR:SoulCoveIDE>       # 目标输出目录
    )

    # 复制Qt平台插件目录(包含qwindows.dll)
    add_custom_command(TARGET SoulCoveIDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/platforms"       # Qt平台插件目录
            $<TARGET_FILE_DIR:SoulCoveIDE>/platforms   # 输出目录的platforms子目录
    )

    # 复制Qt样式插件目录
    add_custom_command(TARGET SoulCoveIDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/styles"          # Qt样式插件目录
            $<TARGET_FILE_DIR:SoulCoveIDE>/styles      # 输出目录的styles子目录
    )

    # 复制Qt图像格式插件目录
    add_custom_command(TARGET SoulCoveIDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/imageformats"    # Qt图像格式插件目录
            $<TARGET_FILE_DIR:SoulCoveIDE>/imageformats  # 输出目录的imageformats子目录
    )

    # 复制SVG图标引擎插件目录(iconengines/qsvgicon.dll)
    add_custom_command(TARGET SoulCoveIDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/iconengines"     # SVG图标引擎插件目录
            $<TARGET_FILE_DIR:SoulCoveIDE>/iconengines  # 输出目录的iconengines子目录
    )

    # 复制应用图标资源(如果有)
    add_custom_command(TARGET SoulCoveIDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/icon"    # 项目图标目录
            $<TARGET_FILE_DIR:SoulCoveIDE>/icon             # 输出目录的icon子目录
    )

    # 复制SQLite数据库驱动插件(sqldrivers/qsqlite.dll)
    add_custom_command(TARGET SoulCoveIDE POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PATH}/plugins/sqldrivers"            # SQLite驱动插件目录
            $<TARGET_FILE_DIR:SoulCoveIDE>/sqldrivers        # 输出目录的sqldrivers子目录
    )
endif()