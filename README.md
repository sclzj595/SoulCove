# scNotebook

A modern cross-platform code editor built with Qt 6.

scNotebook is a lightweight yet powerful code editor designed for C/C++ development. It provides an IDE-like experience while maintaining the flexibility and responsiveness of a native Qt Widgets application.

The project ships three products from a single shared core:

| Product | Entry | Target |
|---------|-------|--------|
| **scIDE** | `products/ide/main.cpp` | Full IDE (LSP + Editor + Terminal + Remote + Git + Debug) |
| **scEditor** | `products/editor/main.cpp` | Local-only code editor (no LSP / no remote) |
| **scNotebook** | `products/notebook/main.cpp` | Markdown notebook (live preview + PDF/HTML export) |

Product differences are controlled by `ProductConfig` and injected by `UIFactory` at startup.

---

## ✨ Features

### Editing
- Multi-tab editor with split view and tear-off windows
- Code folding (indent + brace based)
- Minimap
- Line numbers, current line highlight, bracket matching
- Multi-cursor editing (Alt+Click)
- Column selection (Alt+drag)
- Find / Replace with regex
- Incremental rendering
- Doxygen comment generation
- Spell checker
- `.editorconfig` support

### Language Intelligence
- Clangd LSP integration (JSON-RPC 2.0)
- Auto completion (LSP + local word, dual-track)
- Hover documentation (Markdown rich text)
- Go to Definition (F12 / Ctrl+Click)
- Definition preview (Alt+Click popup)
- Find References (Shift+F12)
- Document Symbols (outline)
- Signature Help
- Semantic Highlight
- `compile_commands.json` auto-detection
- Compiler driver auto-detection (MinGW / clang)
- Server crash exponential backoff reconnect
- Stale response discard + hover debounce

### Markdown
- Live Preview (split view, bidirectional scroll sync)
- Mermaid Diagrams
- Syntax Highlight
- Custom Themes / Dark Mode
- TOC outline panel
- Image lightbox
- Export PDF / HTML
- Math formulas (KaTeX)

### Git
- Git Blame (async loading, inline annotation)
- Repository Detection
- Commit history panel
- Merge conflict resolver

### Build & Debug
- `tasks.json` (VSCode-compatible)
- GDB/MI Debugger
- Breakpoints management
- Debug View (variables / call stack / breakpoints)
- Integrated Terminal (ANSI 256 color, output filtering, Ctrl+C interrupt)

### Workspace
- Multi Workspace
- Workspace Persistence (`.scnb-workspace`)
- Session Restore
- Navigation history (Ctrl+← back)

### Remote Development
- SSH connection management (libssh2)
- SFTP file browser
- Remote terminal
- SSH tunnel LSP (remote clangd)
- Remote file cache (LRU + mtime validation)
- tmux session management
- Remote LSP auto-deployment

### Tools
- Command Palette (Ctrl+Shift+P)
- Regex Tester
- JSON Validator / Formatter
- SQLite Browser
- Image Previewer
- Diff Viewer

### Internationalization
- English
- 中文

### UI/UX
- Frameless window + custom title bar
- Acrylic / Mica effect (Windows 11)
- Multi-theme (Dark / Light / Follow System)
- Customizable shortcuts (VSCode preset + conflict detection)
- Sidebar (Explorer / Search / Git / Outline / Tasks)

---

## Screenshots

(Add screenshots here)

---

## Build

### Requirements
- Qt 6.5+ (mingw_64 recommended)
- CMake 3.16+
- MinGW-w64 14+ (or MSVC)
- clangd 19+ (for LSP features)
- OpenSSL 3.x + zlib (only required for SSH remote support)

### Build

```bash
mkdir build
cd build

# Qt prefix can be provided via cache variable, Qt6_DIR env, or CMAKE_PREFIX_PATH
cmake -DSCNB_QT_PREFIX=F:/Qt/6.5.3/mingw_64 -G "MinGW Makefiles" ..
cmake --build . --config Release
```

Build outputs:
- `build/Release/scIDE.exe` — full IDE
- `build/Release/scEditor.exe` — code editor
- `build/Release/scNotebook.exe` — Markdown notebook

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `SCNB_QT_PREFIX` | *(empty)* | Qt installation prefix, e.g. `F:/Qt/6.5.3/mingw_64` |
| `SCNB_WITH_OPENSSL` | `ON` | Enable OpenSSL support (required by SSH) |
| `SCNB_WITH_SSH` | `ON` | Enable SSH remote support (libssh2 + SFTP) |

When OpenSSL is not found, SSH support is automatically disabled.

`compile_commands.json` is exported to `build/` for clangd indexing.

---

## Roadmap

- [x] LSP
- [x] Markdown
- [x] Git
- [x] Terminal
- [x] Workspace
- [x] Remote Development
- [x] Debug
- [x] Tasks
- [x] Snippets
- [x] Hover
- [ ] Plugin System
- [ ] AI Assistant
- [ ] Extension Marketplace

See [docs/roadmap.md](docs/roadmap.md) for the full release plan and milestones.

---

## Documentation

- [docs/roadmap.md](docs/roadmap.md) — Release plan and milestones
- [docs/architecture.md](docs/architecture.md) — Architecture design
- [docs/development.md](docs/development.md) — Development guide and build details
- [CHANGELOG.md](CHANGELOG.md) — Release notes

---

## License

MIT License. See [LICENSE](LICENSE).

---

## Contributing

Pull Requests are welcome. For major changes, please open an issue first to discuss what you would like to change. See [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Acknowledgements

- [Qt](https://www.qt.io/)
- [clangd](https://clangd.llvm.org/)
- [CMake](https://cmake.org/)
- [libssh2](https://libssh2.org/)
- [maddy](https://github.com/progsource/maddy)
- OpenSSL
- zlib
