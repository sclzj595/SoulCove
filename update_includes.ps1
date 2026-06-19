# 批量更新所有 #include 路径
# 策略：用哈希表定义所有替换映射，遍历所有 .h/.cpp 文件执行替换

$srcDir = "src"

# 完整的替换映射表（旧路径 → 新路径）
# 注意：键值用 #include "..." 的形式，确保精确匹配
$replacements = [ordered]@{
    # === 根目录文件改名（同时改路径和文件名）===
    '"widget.h"'            = '"ui/shell/Widget.h"'
    '"mytextedit.h"'        = '"ui/editor/MyTextEdit.h"'
    '"lineNumberArea.h"'    = '"ui/editor/LineNumberArea.h"'
    '"textCompleter.h"'     = '"ui/editor/TextCompleter.h"'

    # === interfaces/ 子目录化 ===
    '"interfaces/ICompleter.h"'            = '"interfaces/editor/ICompleter.h"'
    '"interfaces/IEditorEdit.h"'           = '"interfaces/editor/IEditorEdit.h"'
    '"interfaces/ILineNumber.h"'           = '"interfaces/editor/ILineNumber.h"'
    '"interfaces/ISyntaxHighlighter.h"'    = '"interfaces/editor/ISyntaxHighlighter.h"'
    '"interfaces/IConfigManager.h"'        = '"interfaces/core/IConfigManager.h"'
    '"interfaces/IFileOperator.h"'         = '"interfaces/core/IFileOperator.h"'
    '"interfaces/IObserver.h"'             = '"interfaces/core/IObserver.h"'
    '"interfaces/IThemeManager.h"'         = '"interfaces/core/IThemeManager.h"'
    '"interfaces/IFramelessWindow.h"'      = '"interfaces/ui/IFramelessWindow.h"'
    '"interfaces/IMarkdownViewer.h"'       = '"interfaces/ui/IMarkdownViewer.h"'
    '"interfaces/ISideFileBar.h"'          = '"interfaces/ui/ISideFileBar.h"'
    '"interfaces/ITabWidget.h"'            = '"interfaces/ui/ITabWidget.h"'
    '"interfaces/ITerminalWidget.h"'       = '"interfaces/ui/ITerminalWidget.h"'
    '"interfaces/IUiLibrary.h"'            = '"interfaces/ui/IUiLibrary.h"'
    '"interfaces/ILspClient.h"'            = '"interfaces/lsp/ILspClient.h"'
    '"interfaces/ISshClient.h"'            = '"interfaces/remote/ISshClient.h"'
    '"interfaces/IMarkdownParser.h"'       = '"interfaces/markdown/IMarkdownParser.h"'

    # === core/ 子目录化（带 core/ 前缀的）===
    '"core/Subject.h"'                = '"core/base/Subject.h"'
    '"core/ScreenGuard.h"'            = '"core/base/ScreenGuard.h"'
    '"core/ConfigManager.h"'          = '"core/config/ConfigManager.h"'
    '"core/ThemeManager.h"'           = '"core/config/ThemeManager.h"'
    '"core/DefaultUiLibrary.h"'       = '"core/config/DefaultUiLibrary.h"'
    '"core/FileOperator.h"'           = '"core/fileio/FileOperator.h"'
    '"core/EncodingDetector.h"'       = '"core/fileio/EncodingDetector.h"'
    '"core/CodeSyntaxHighlighter.h"'  = '"core/editor/CodeSyntaxHighlighter.h"'
    '"core/CodeHighlighter.h"'        = '"core/editor/CodeHighlighter.h"'
    '"core/HeaderSymbolScanner.h"'    = '"core/editor/HeaderSymbolScanner.h"'
    '"core/CodeFormatter.h"'          = '"core/format/CodeFormatter.h"'
    '"core/JsonValidator.h"'          = '"core/format/JsonValidator.h"'
    '"core/MaddyParser.h"'            = '"core/markdown/MaddyParser.h"'
    '"core/MarkdownParser.h"'         = '"core/markdown/MarkdownParser.h"'
    '"core/MdExporter.h"'             = '"core/markdown/MdExporter.h"'
    '"core/HtmlCssResolver.h"'        = '"core/markdown/HtmlCssResolver.h"'
    '"core/SnippetManager.h"'         = '"core/snippet/SnippetManager.h"'
    '"core/ShortcutManager.h"'        = '"core/shortcut/ShortcutManager.h"'
    '"core/ShortcutFilter.h"'         = '"core/shortcut/ShortcutFilter.h"'
    '"core/IShortcutCommand.h"'       = '"interfaces/shortcut/IShortcutCommand.h"'
    '"core/LspClient.h"'              = '"core/lsp/LspClient.h"'
    '"core/LspManager.h"'             = '"core/lsp/LspManager.h"'
    '"core/GitManager.h"'             = '"core/vcs/GitManager.h"'
    '"core/MergeConflictResolver.h"'  = '"core/vcs/MergeConflictResolver.h"'
    '"core/SshClient.h"'              = '"core/remote/SshClient.h"'
    '"core/SftpClient.h"'             = '"core/remote/SftpClient.h"'
    '"core/SshSessionManager.h"'      = '"core/remote/SshSessionManager.h"'
    '"core/TaskManager.h"'            = '"core/task/TaskManager.h"'

    # === core/ 内部纯文件名 include（不带 core/ 前缀的）===
    '"Subject.h"'                = '"core/base/Subject.h"'
    '"ScreenGuard.h"'            = '"core/base/ScreenGuard.h"'
    '"ConfigManager.h"'          = '"core/config/ConfigManager.h"'
    '"ThemeManager.h"'           = '"core/config/ThemeManager.h"'
    '"DefaultUiLibrary.h"'       = '"core/config/DefaultUiLibrary.h"'
    '"FileOperator.h"'           = '"core/fileio/FileOperator.h"'
    '"EncodingDetector.h"'       = '"core/fileio/EncodingDetector.h"'
    '"CodeSyntaxHighlighter.h"'  = '"core/editor/CodeSyntaxHighlighter.h"'
    '"CodeHighlighter.h"'        = '"core/editor/CodeHighlighter.h"'
    '"HeaderSymbolScanner.h"'    = '"core/editor/HeaderSymbolScanner.h"'
    '"CodeFormatter.h"'          = '"core/format/CodeFormatter.h"'
    '"JsonValidator.h"'          = '"core/format/JsonValidator.h"'
    '"MaddyParser.h"'            = '"core/markdown/MaddyParser.h"'
    '"MarkdownParser.h"'         = '"core/markdown/MarkdownParser.h"'
    '"MdExporter.h"'             = '"core/markdown/MdExporter.h"'
    '"HtmlCssResolver.h"'        = '"core/markdown/HtmlCssResolver.h"'
    '"SnippetManager.h"'         = '"core/snippet/SnippetManager.h"'
    '"ShortcutManager.h"'        = '"core/shortcut/ShortcutManager.h"'
    '"ShortcutFilter.h"'         = '"core/shortcut/ShortcutFilter.h"'
    '"IShortcutCommand.h"'       = '"interfaces/shortcut/IShortcutCommand.h"'
    '"LspClient.h"'              = '"core/lsp/LspClient.h"'
    '"LspManager.h"'             = '"core/lsp/LspManager.h"'
    '"GitManager.h"'             = '"core/vcs/GitManager.h"'
    '"MergeConflictResolver.h"'  = '"core/vcs/MergeConflictResolver.h"'
    '"SshClient.h"'              = '"core/remote/SshClient.h"'
    '"SftpClient.h"'             = '"core/remote/SftpClient.h"'
    '"SshSessionManager.h"'      = '"core/remote/SshSessionManager.h"'
    '"TaskManager.h"'            = '"core/task/TaskManager.h"'

    # === ui/ 子目录化（带 ui/ 前缀的）===
    '"ui/FramelessWindow.h"'   = '"ui/shell/FramelessWindow.h"'
    '"ui/TitleBar.h"'          = '"ui/shell/TitleBar.h"'
    '"ui/EditorTabBar.h"'      = '"ui/editor/EditorTabBar.h"'
    '"ui/FindReplaceBar.h"'    = '"ui/editor/FindReplaceBar.h"'
    '"ui/EditorSplitView.h"'   = '"ui/editor/EditorSplitView.h"'
    '"ui/MarkdownMode.h"'      = '"ui/markdown/MarkdownMode.h"'
    '"ui/HtmlPreviewMode.h"'   = '"ui/markdown/HtmlPreviewMode.h"'
    '"ui/MdTocPanel.h"'        = '"ui/markdown/MdTocPanel.h"'
    '"ui/ImageLightBox.h"'     = '"ui/markdown/ImageLightBox.h"'
    '"ui/SideBar.h"'           = '"ui/sidebar/SideBar.h"'
    '"ui/GitPanel.h"'          = '"ui/sidebar/GitPanel.h"'
    '"ui/EmbeddedTerminal.h"'  = '"ui/terminal/EmbeddedTerminal.h"'
    '"ui/TerminalView.h"'      = '"ui/terminal/TerminalView.h"'
    '"ui/TerminalBackend.h"'   = '"ui/terminal/TerminalBackend.h"'
    '"ui/SshTerminalWidget.h"' = '"ui/terminal/SshTerminalWidget.h"'
    '"ui/RemoteFileTree.h"'    = '"ui/remote/RemoteFileTree.h"'
    '"ui/SshConfigPanel.h"'    = '"ui/remote/SshConfigPanel.h"'
    '"ui/SqliteBrowser.h"'     = '"ui/tools/SqliteBrowser.h"'
    '"ui/RegexTester.h"'       = '"ui/tools/RegexTester.h"'
    '"ui/CommandPalette.h"'    = '"ui/tools/CommandPalette.h"'
    '"ui/ImagePreviewer.h"'    = '"ui/tools/ImagePreviewer.h"'
    '"ui/DiffViewer.h"'        = '"ui/tools/DiffViewer.h"'
    '"ui/ModernDialog.h"'      = '"ui/dialog/ModernDialog.h"'
    '"ui/SettingsPage.h"'      = '"ui/settings/SettingsPage.h"'

    # === ui/ 内部纯文件名 include（不带 ui/ 前缀的）===
    '"FramelessWindow.h"'   = '"ui/shell/FramelessWindow.h"'
    '"TitleBar.h"'          = '"ui/shell/TitleBar.h"'
    '"EditorTabBar.h"'      = '"ui/editor/EditorTabBar.h"'
    '"FindReplaceBar.h"'    = '"ui/editor/FindReplaceBar.h"'
    '"EditorSplitView.h"'   = '"ui/editor/EditorSplitView.h"'
    '"MarkdownMode.h"'      = '"ui/markdown/MarkdownMode.h"'
    '"HtmlPreviewMode.h"'   = '"ui/markdown/HtmlPreviewMode.h"'
    '"MdTocPanel.h"'        = '"ui/markdown/MdTocPanel.h"'
    '"ImageLightBox.h"'     = '"ui/markdown/ImageLightBox.h"'
    '"SideBar.h"'           = '"ui/sidebar/SideBar.h"'
    '"GitPanel.h"'          = '"ui/sidebar/GitPanel.h"'
    '"EmbeddedTerminal.h"'  = '"ui/terminal/EmbeddedTerminal.h"'
    '"TerminalView.h"'      = '"ui/terminal/TerminalView.h"'
    '"TerminalBackend.h"'   = '"ui/terminal/TerminalBackend.h"'
    '"SshTerminalWidget.h"' = '"ui/terminal/SshTerminalWidget.h"'
    '"RemoteFileTree.h"'    = '"ui/remote/RemoteFileTree.h"'
    '"SshConfigPanel.h"'    = '"ui/remote/SshConfigPanel.h"'
    '"SqliteBrowser.h"'     = '"ui/tools/SqliteBrowser.h"'
    '"RegexTester.h"'       = '"ui/tools/RegexTester.h"'
    '"CommandPalette.h"'    = '"ui/tools/CommandPalette.h"'
    '"ImagePreviewer.h"'    = '"ui/tools/ImagePreviewer.h"'
    '"DiffViewer.h"'        = '"ui/tools/DiffViewer.h"'
    '"ModernDialog.h"'      = '"ui/dialog/ModernDialog.h"'
    '"SettingsPage.h"'      = '"ui/settings/SettingsPage.h"'
}

# 获取所有 .h 和 .cpp 文件
$files = Get-ChildItem -Path $srcDir -Recurse -Include *.h,*.cpp
Write-Host "Found $($files.Count) files to process"

$changedFiles = 0
$totalReplacements = 0

foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw
    $originalContent = $content
    $fileReplacements = 0

    foreach ($key in $replacements.Keys) {
        $value = $replacements[$key]
        # 只替换 #include 中的路径
        # 匹配 #include "xxx" 的模式
        $pattern = "#include\s+$key"
        $replacement = "#include $value"
        $newContent = [regex]::Replace($content, [regex]::Escape("#include " + $key), "#include " + $value)

        if ($newContent -ne $content) {
            $fileReplacements++
            $content = $newContent
        }
    }

    if ($content -ne $originalContent) {
        Set-Content -Path $file.FullName -Value $content -NoNewline
        $changedFiles++
        $totalReplacements += $fileReplacements
        Write-Host "  Updated: $($file.FullName.Substring($file.FullName.IndexOf('src')))"
    }
}

Write-Host "`n=== Summary ==="
Write-Host "Files changed: $changedFiles / $($files.Count)"
Write-Host "Total replacements: $totalReplacements"
