# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [v1.0.0] - 2026-07-04 — First Public Release

First public open-source release. Internal development versions V1.3 – V1.9
were consolidated into this single 1.0.0 release.

### Added
- **Editor**: multi-tab editing, split view, tear-off windows, code folding,
  minimap, line numbers, bracket matching, multi-cursor (Alt+Click),
  column selection (Alt+drag), find/replace with regex, incremental rendering,
  Doxygen comment generation, spell checker, `.editorconfig` support.
- **Language Intelligence**: clangd LSP integration (JSON-RPC 2.0), auto
  completion (dual-track LSP + local word), hover documentation (Markdown rich
  text), Go to Definition, definition preview popup, Find References, Document
  Symbols, Signature Help, semantic highlight, `compile_commands.json`
  auto-detection, compiler driver auto-detection, server crash backoff
  reconnect, stale response discard, hover debounce.
- **Markdown**: live preview with bidirectional scroll sync, Mermaid diagrams,
  syntax highlight, custom themes, dark mode, TOC outline, image lightbox,
  PDF / HTML export, KaTeX math.
- **Git**: Git blame (async + inline annotation), repository detection,
  commit history panel, merge conflict resolver.
- **Build & Debug**: VSCode-compatible `tasks.json`, GDB/MI debugger,
  breakpoint management, debug view (variables / call stack / breakpoints),
  integrated terminal (ANSI 256 color, output filtering, Ctrl+C interrupt).
- **Workspace**: multi-workspace, `.scnb-workspace` persistence, session
  restore, navigation history (Ctrl+← back).
- **Remote Development**: SSH connection management (libssh2), SFTP browser,
  remote terminal, SSH tunnel LSP (remote clangd), remote file cache
  (LRU + mtime), tmux session management, remote LSP auto-deployment.
- **Tools**: command palette (Ctrl+Shift+P), regex tester, JSON
  validator/formatter, SQLite browser, image previewer, diff viewer.
- **Internationalization**: English and 中文.
- **UI/UX**: frameless window, custom title bar, Acrylic/Mica effect
  (Windows 11), multi-theme, customizable shortcuts (VSCode preset +
  conflict detection), sidebar (Explorer / Search / Git / Outline / Tasks).
- **Build system**: CMake with `find_package` Qt detection, conditional
  compilation (`SCNB_WITH_OPENSSL`, `SCNB_WITH_SSH`), `compile_commands.json`
  export, three-product factory (SoulCove IDE / SoulCove Notebook / SoulCove Notebook Lite).

## Release Plan

| Version | Name | Theme |
|---------|------|-------|
| v1.0.0 | Initial Public Release | First open-source release |
| v1.1.0 | Core Stability | Crash / leak / LSP / encoding / undo-redo fixes |
| v1.2.0 | Performance Update | Faster editor, lower memory, incremental rendering |
| v1.3.0 | LSP Improvement | VSCode-level LSP stability |
| v1.4.0 | UI Polish | UI / UX optimization |
| v2.0.0 | Architecture Upgrade | Mature architecture, plugin-ready |

See [docs/roadmap.md](docs/roadmap.md) for milestone details.
