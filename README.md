# SoulCove

<p align="center">
  <img src="assets/logo.png" width="128" alt="SoulCove Logo">
</p>

<p align="center">
  <b>A Modern Native Code Editing Platform Built with Qt 6</b><br>
  Lightweight • High Performance • Cross Platform • Developer First
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Qt-6.5+-41CD52?logo=qt">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus">
  <img src="https://img.shields.io/badge/CMake-3.16+-064F8C?logo=cmake">
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey">
  <img src="https://img.shields.io/badge/License-MIT-green">
</p>

---

## Overview

**SoulCove** is a modern native development platform built with **Qt 6 Widgets**.

Unlike Electron-based editors, SoulCove focuses on delivering a responsive, memory-efficient, and fully native desktop experience without sacrificing modern IDE capabilities.

The project follows a **single shared core + multiple products** architecture. Every product shares the same editor framework while exposing different feature sets according to its target audience.

---

# Product Family

| Product | Positioning | Target Users |
|----------|-------------|--------------|
| **SoulCove Notebook Lite** | Markdown notebook | Writers, note-taking, documentation |
| **SoulCove Notebook** | Lightweight code editor | Daily development, scripting, small projects |
| **SoulCove IDE** | Full-featured native IDE | Professional software development |

All three products are built from the same codebase.

```
SoulCove
│
├── Notebook Lite
│      Markdown / Notes
│
├── Notebook
│      Native Code Editor
│
└── IDE
       Full IDE
```

Feature availability is determined by **ProductConfig** and injected through **UIFactory** during application startup.

---

# Why SoulCove?

Modern editors provide incredible functionality, but many rely heavily on Electron, which often results in increased memory usage and slower startup times.

SoulCove takes a different approach.

Built entirely with **Qt Widgets** and modern C++, it aims to provide:

- Native desktop performance
- Fast startup
- Low memory footprint
- Smooth rendering
- IDE-level capabilities
- Modern user experience

SoulCove is not intended to imitate VS Code.

Instead, it embraces a native architecture while adopting the workflows developers already know.

---

# Features

## Editing

- Multi-tab editing
- Split editor
- Tear-off windows
- Minimap
- Line numbers
- Current line highlight
- Brace matching
- Code folding
- Multi-cursor editing
- Column selection
- Incremental rendering
- Regex Find / Replace
- Spell checker
- Doxygen comment generation
- `.editorconfig` support

---

## Language Intelligence

Powered by **clangd**.

- LSP (JSON-RPC 2.0)
- Auto Completion
- Hover Documentation
- Signature Help
- Semantic Highlighting
- Go to Definition
- Peek Definition
- Find References
- Document Symbols
- Workspace Symbols
- Diagnostics
- Auto compiler detection
- compile_commands.json discovery
- Server auto-restart
- Response version filtering
- Hover debounce optimization

---

## Markdown

- Live Preview
- Bidirectional Scroll Sync
- Mermaid
- KaTeX
- Syntax Highlight
- TOC Sidebar
- Image Preview
- Export HTML
- Export PDF
- Custom Themes

---

## Git

- Repository Detection
- Git Blame
- Commit History
- Merge Conflict Resolver

---

## Build & Debug

- VSCode-compatible `tasks.json`
- GDB/MI Debugger
- Integrated Terminal
- Breakpoint Management
- Variables View
- Call Stack
- Output Console

---

## Workspace

- Multi Workspace
- Workspace Persistence
- Session Restore
- Navigation History

---

## Remote Development

- SSH
- SFTP
- Remote Terminal
- Remote clangd
- SSH Tunnel
- Remote File Cache
- tmux Session Management
- Remote LSP Deployment

---

## Productivity Tools

- Command Palette
- Regex Tester
- JSON Formatter
- SQLite Browser
- Diff Viewer
- Image Preview

---

## User Experience

- Frameless Window
- Custom Title Bar
- Acrylic / Mica (Windows 11)
- Dark Theme
- Light Theme
- Follow System
- VSCode Keymap
- Shortcut Conflict Detection
- Explorer
- Search
- Outline
- Git Sidebar
- Tasks Sidebar

---

# Screenshots

Coming Soon.

```
assets/screenshots/

editor.png

ide.png

notebook.png

markdown.png

terminal.png
```

---

# Project Structure

```
SoulCove
│
├── products
│   ├── ide
│   ├── editor
│   └── notebook
│
├── core
├── editor
├── ui
├── plugins
├── lsp
├── markdown
├── git
├── terminal
├── remote
├── framework
├── docs
└── assets
```

---

# Build

## Requirements

- Qt 6.5+
- CMake 3.16+
- C++20 Compiler
- MinGW-w64 14+ or MSVC
- clangd 19+
- OpenSSL 3.x
- zlib
- libssh2 (optional)

---

## Configure

```bash
mkdir build
cd build

cmake ^
  -DSCNB_QT_PREFIX=F:/Qt/6.5.3/mingw_64 ^
  -G "MinGW Makefiles" ^
  ..
```

---

## Build

```bash
cmake --build . --config Release
```

---

## Outputs

| Product | Executable |
|----------|------------|
| SoulCove IDE | `scIDE.exe` |
| SoulCove Notebook | `scEditor.exe` |
| SoulCove Notebook Lite | `SoulCove.exe` |

---

# Configuration

| Option | Default |
|----------|---------|
| SCNB_QT_PREFIX | Qt installation path |
| SCNB_WITH_OPENSSL | ON |
| SCNB_WITH_SSH | ON |

If OpenSSL is unavailable, SSH functionality will be disabled automatically.

---

# Roadmap

## Completed

- Native Editor
- LSP
- Markdown
- Git
- Terminal
- Workspace
- Remote Development
- Debugger
- Tasks
- Snippets

## In Progress

- Plugin System
- Theme Marketplace
- Extension API

## Planned

- AI Assistant
- MCP Integration
- AI Code Completion
- AI Refactoring
- AI Chat
- AI Workspace
- Cloud Sync
- Extension Marketplace

---

# Documentation

```
docs/

architecture.md

development.md

roadmap.md

plugin-sdk.md

contributing.md
```

---

# Philosophy

SoulCove is built around four principles:

- **Native First** — Qt Widgets instead of Electron
- **Performance Matters** — Fast startup and efficient memory usage
- **Shared Core** — One architecture powering multiple products
- **Developer Experience** — Familiar workflows with native responsiveness

---

# Technology Stack

| Layer | Technology |
|--------|------------|
| Language | C++20 |
| UI | Qt 6 Widgets |
| Build | CMake |
| LSP | clangd |
| Markdown | maddy |
| Debug | GDB/MI |
| SSH | libssh2 |
| Crypto | OpenSSL |
| Compression | zlib |

---

# Contributing

Issues and Pull Requests are welcome.

If you plan to introduce major architectural changes, please open an issue first for discussion.

---

# License

Released under the MIT License.

See `LICENSE` for details.

---

# Acknowledgements

- Qt
- clangd
- LLVM
- CMake
- libssh2
- OpenSSL
- zlib
- maddy

---

<p align="center">

<b>SoulCove</b>

Native • Fast • Elegant

Built with ❤️ using Qt 6

</p>
