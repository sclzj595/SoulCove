# Architecture

This document describes the layered architecture, design patterns and module
layout of SoulCove.

## Layered Architecture

```
┌─────────────────────────────────────────┐
│  UI Layer (src/ui/)                     │
│  Widget / EditorTabBar / MyTextEdit      │
│  SideBar / MarkdownMode / TerminalView   │
├─────────────────────────────────────────┤
│  Controller Layer (src/controller/)     │
│  FileController / LspCoordinator         │
│  CommandRegistry / EditorActions         │
│  IdleTabTracker                          │
├─────────────────────────────────────────┤
│  Core Layer (src/core/)                 │
│  LspManager / FileOperator               │
│  ThemeManager / CodeSyntaxHighlighter    │
│  GitManager / SshClient / TaskManager    │
├─────────────────────────────────────────┤
│  Interface Layer (src/interfaces/)      │
│  ILspClient / IEditorEdit / ICompleter   │
│  IFileOperator / IThemeManager           │
└─────────────────────────────────────────┘
```

- **UI Layer** — Qt Widgets components. Talks to controllers and interfaces,
  never to concrete core implementations directly when an interface exists.
- **Controller Layer** — Coordinates between UI and core. Routes signals,
  registers commands, tracks idle tabs.
- **Core Layer** — Business logic: LSP, file IO, syntax highlight, Git, SSH,
  Markdown, snippets, tasks, shortcuts, workspace, debug.
- **Interface Layer** — Abstract interfaces that decouple UI from core
  implementations. Enables future swapping (e.g. `IMarkdownParser`,
  `ICompleter`, `ICodeFormatter`).

A shared static library `scCore` contains all of the above. Each product
(SoulCove IDE / SoulCove Notebook / SoulCove Notebook Lite) is a thin executable
that links `scCore` and supplies its own `main.cpp` + `ProductConfig`.

## Design Patterns

| Pattern | Usage |
|---------|-------|
| **Facade** | `LspManager` wraps multiple `LspClient` instances and language routing. |
| **Factory** | `UIFactory` builds the UI according to `ProductConfig`. |
| **Observer** | LSP responses are delivered via Qt signals (async callbacks). `Subject` base in `core/base/`. |
| **Strategy** | `ICompleter` / `IMarkdownParser` / `ICodeFormatter` are swappable implementations. |
| **Command** | `CommandRegistry` + `IShortcutCommand` register and dispatch actions. |
| **Filter** | `ShortcutFilter` intercepts global shortcuts. |
| **RAII** | `std::unique_ptr` manages `QProcess` and resource lifetimes. |

## Three Products (Factory)

All three products share the same core source compiled into `scCore`. Product
differentiation is driven by `ProductConfig` (feature flags) injected by
`UIFactory` at startup.

| Product | Capabilities |
|---------|--------------|
| **SoulCove IDE** | Full IDE — LSP, editor, terminal, remote, Git, debug, tasks, outline. |
| **SoulCove Notebook** | Local-only code editor — file tree, search, formatting. No LSP / no remote. |
| **SoulCove Notebook Lite** | Markdown notebook — text editing + live preview + PDF/HTML export. |

## Directory Layout

```
SoulCove/
├── CMakeLists.txt              # Build configuration
├── products/                   # Three product entry points
│   ├── ide/                    # SoulCove IDE
│   ├── editor/                 # SoulCove Notebook
│   └── notebook/               # SoulCove Notebook Lite
├── src/
│   ├── core/                   # Core business layer
│   │   ├── base/               # Base utilities (ScreenGuard / Subject)
│   │   ├── config/             # Config & theme (ConfigManager / ThemeManager)
│   │   ├── debug/              # Debug & performance (DebugManager / PerformanceMonitor)
│   │   ├── editor/             # Editor core (highlight / folding / minimap / Doxygen)
│   │   ├── fileio/             # File IO (FileOperator / EncodingDetector)
│   │   ├── format/             # Code formatting
│   │   ├── i18n/               # Internationalization
│   │   ├── lsp/                # LSP client (LspClient / LspManager)
│   │   ├── markdown/           # Markdown parsing (maddy wrapper)
│   │   ├── remote/             # Remote dev (SSH / SFTP / cache / LSP deploy)
│   │   ├── shortcut/           # Shortcut system
│   │   ├── snippet/            # Code snippets
│   │   ├── task/               # Task system
│   │   ├── vcs/                # Version control (Git)
│   │   └── workspace/          # Workspace manager
│   ├── controller/             # Controller layer
│   ├── factory/                # Factory layer (UIFactory / ProductConfig)
│   ├── interfaces/             # Interface layer
│   ├── ui/                     # UI layer
│   │   ├── debug/              # Debug view
│   │   ├── dialog/             # Dialogs
│   │   ├── editor/             # Editor widgets (MyTextEdit / TextCompleter / HoverPopup / EditorTabBar)
│   │   ├── markdown/           # Markdown views
│   │   ├── remote/             # Remote UI
│   │   ├── settings/           # Settings page
│   │   ├── shell/              # Main window (Widget / FramelessWindow / TitleBar)
│   │   ├── shortcut/           # Shortcut UI
│   │   ├── sidebar/            # Sidebar panels
│   │   ├── terminal/           # Terminal
│   │   └── tools/              # Tool windows
│   ├── i18n/                   # Translations (zh_CN / en_US)
│   ├── styles/                 # QSS stylesheets
│   └── resources.qrc           # Qt resources
├── third_party/                # Third-party libraries
│   ├── libssh2/                # SSH2 (git submodule)
│   └── maddy/                  # Header-only Markdown parser
├── utils/                      # Utility headers (Logger)
└── docs/                       # Documentation
```

## Conditional Compilation

SSH / OpenSSL support is conditionally compiled via CMake options:

- `SCNB_WITH_OPENSSL` (default `ON`) — enables OpenSSL.
- `SCNB_WITH_SSH` (default `ON`) — enables SSH remote (libssh2 + SFTP).

When OpenSSL is not found, SSH is automatically disabled. Source code guards
these sections with `#ifdef SCNB_WITH_SSH` / `#ifdef SCNB_WITH_OPENSSL`.

## LSP Integration

The LSP subsystem is the most complex part of the architecture:

- `LspClient` — JSON-RPC 2.0 transport over stdio. Manages a single LSP server
  process, request/response correlation, document lifecycle
  (`didOpen` / `didChange` / `didSave` / `didClose`).
- `LspManager` — Facade. Routes requests by language to the right
  `LspClient`, auto-detects servers, infers project root, locates
  `compile_commands.json`, detects compiler driver for clangd `--query-driver`.
- `LspCoordinator` — Controller-layer signal router. Sits between UI editors
  and `LspManager`, dispatches responses back to the originating editor.
- `LanguageRegistry` — single source of truth for `languageId ↔ file suffix`.

Supported LSP methods:
- `textDocument/completion`
- `textDocument/definition`
- `textDocument/hover`
- `textDocument/references`
- `textDocument/documentSymbol`
- `textDocument/publishDiagnostics`
- `textDocument/signatureHelp`
- Document lifecycle notifications

Robustness features:
- Server crash exponential backoff reconnect.
- Hover debounce (200ms) + stale response discard.
- Per-request-type stale detection.
- `compile_commands.json` upward search + workspace root fallback.
