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

# 源文件列表
set(SOURCES
    src/main.cpp                          # 程序入口
    src/widget.cpp                        # 主窗口实现
    src/mytextedit.cpp                    # 自定义文本编辑框实现
    src/textCompleter.cpp                 # 智能补全功能实现
    src/lineNumberArea.cpp                # 行号区域实现
    # 核心层（接口实现 + 设计模式）
    src/core/ConfigManager.cpp            # 配置管理器（单例）
    src/core/FileOperator.cpp             # 文件操作器（观察者）
    src/core/Subject.cpp                  # 观察者主题基类
    src/core/ThemeManager.cpp             # 主题管理器（单例+工厂）
    src/core/MarkdownParser.cpp           # Markdown→HTML解析器(Simple fallback)
    src/core/MaddyParser.cpp              # maddy三方库Markdown解析器
    src/core/CodeHighlighter.cpp          # 代码块语法高亮后处理
    src/core/CodeSyntaxHighlighter.cpp    # 代码语法高亮器（Python/C++/JS）
    src/core/HeaderSymbolScanner.cpp      # 自定义头文件符号扫描器（多语言 #include/import 解析）
    src/core/HtmlCssResolver.cpp          # HTML CSS 解析器（<link> 标签解析+本地 CSS 内联注入）
    src/core/CodeFormatter.cpp            # M4: 代码格式化器（clang-format/内置）
    src/core/JsonValidator.cpp            # M10: JSON校验和格式化工具
    # 核心层：UI库接口实现
    src/core/DefaultUiLibrary.cpp          # 默认UI库实现（IUiLibrary接口）
    # 工厂层
    src/factory/UIFactory.cpp             # UI组件工厂
    # UI组件层（现代化窗口）
    src/ui/FramelessWindow.cpp            # 无边框窗口基类
    src/ui/TitleBar.cpp                   # 自定义标题栏组件
    src/ui/EditorTabBar.cpp              # VSCode风格文件标签页栏
    src/ui/SideBar.cpp                   # 左侧资源管理面板
    src/ui/SettingsPage.cpp              # 设置页面（标签页内嵌）
    src/ui/MarkdownMode.cpp              # Markdown分屏编辑预览
    src/ui/HtmlPreviewMode.cpp           # HTML分屏编辑预览（源码/预览/分屏三模式切换）
    src/ui/FindReplaceBar.cpp            # 查找替换面板（持久化+正则+大小写+全部替换）
    src/ui/EditorSplitView.cpp           # V1.9: 编辑器分栏视图（共享文档同步编辑）
    src/ui/EmbeddedTerminal.cpp          # 内嵌终端（容器：标签/搜索/主题）
    src/ui/TerminalView.cpp              # 终端统一视图（输入输出合一）
    src/ui/TerminalBackend.cpp           # 终端进程后端（QProcess封装）
    src/ui/CommandPalette.cpp            # 命令面板（Ctrl+Shift+P）
    src/ui/DiffViewer.cpp                # M5: Diff视图（并排对比）
    # M3: Git 集成
    src/core/GitManager.cpp              # Git 版本控制管理器（git.exe 方案）
    src/core/MergeConflictResolver.cpp   # V1.9: Git 合并冲突解决器
    src/ui/GitPanel.cpp                  # 源代码管理面板 UI
    # M9: 正则表达式测试器
    src/ui/RegexTester.cpp               # 正则表达式测试工具面板
    # M11: 图片预览器
    src/ui/ImagePreviewer.cpp            # 图片文件预览组件
    # M14: 代码片段管理
    src/core/SnippetManager.cpp          # 代码片段管理器（CRUD + 触发 + 展开）
    # M8: LSP 语言服务器客户端
    src/core/LspClient.cpp              # LSP 通信框架（JSON-RPC 2.0）
    src/core/LspManager.cpp             # LSP 管理器（门面+工厂，多语言客户端生命周期+信号路由）
    # M13: SQLite 数据库浏览器
    src/ui/SqliteBrowser.cpp            # 数据库浏览器组件
    # M15: 任务系统增强
    src/core/TaskManager.cpp            # 任务管理器（VSCode tasks.json 兼容）
    # T7: 快捷键自定义系统
    src/core/ShortcutManager.cpp        # 快捷键管理器（自定义/冲突检测/持久化）
    src/core/ShortcutFilter.cpp         # 快捷键过滤器（Command+Filter+Observer模式）
    # T8: 文件编码自动检测
    src/core/EncodingDetector.cpp       # 编码检测器（UTF-8/GBK/UTF-16识别与转换）
    # SSH 远程终端模块
    src/interfaces/ISshClient.cpp        # ISshClient MOC触发（纯接口类信号需要MOC）
    src/interfaces/ILspClient.cpp        # ILspClient MOC触发（LSP客户端抽象接口）
    src/core/SshClient.cpp              # SSH客户端核心（libssh2封装）
    src/core/SftpClient.cpp             # SFTP客户端（libssh2 SFTP封装）
    src/core/ScreenGuard.cpp            # 屏幕安全守卫（跨屏/高DPI保护）
    src/core/SshSessionManager.cpp      # SSH会话管理器（多连接/配置持久化）
    src/ui/SshTerminalWidget.cpp        # SSH远程终端UI
    src/ui/SshConfigPanel.cpp          # SSH连接配置面板（标签页内嵌）
    src/ui/RemoteFileTree.cpp          # 远程文件树（SFTP远程文件浏览器）
    # MD增强: TOC面板+图片灯箱+导出功能
    src/ui/MdTocPanel.cpp               # Markdown目录树面板（可视化导航）
    src/ui/ImageLightBox.cpp            # 图片灯箱预览（模态窗口+缩放）
    src/ui/ModernDialog.cpp             # 现代化弹窗组件（替代原生QMessageBox）
    src/core/MdExporter.cpp             # Markdown文档导出器（HTML/PDF）
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