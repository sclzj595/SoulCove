# Development Guide

This document covers build configuration, coding conventions, debugging, LSP
integration and shortcut reference for SoulCove contributors.

## Build Requirements

- Qt 6.5+ (mingw_64 recommended)
- CMake 3.16+
- MinGW-w64 14+ (or MSVC)
- clangd 19+ (for LSP features)
- OpenSSL 3.x + zlib (only required for SSH remote support)

## Build Steps

```bash
mkdir build
cd build

# Qt prefix can be provided via cache variable, Qt6_DIR env, or CMAKE_PREFIX_PATH
cmake -DSCNB_QT_PREFIX=F:/Qt/6.5.3/mingw_64 -G "MinGW Makefiles" ..
cmake --build . --config Release
```

Build outputs (when `CMAKE_BUILD_TYPE=Release` is set, outputs go to
`build/Release/`):
- `SoulCoveIDE.exe` — SoulCove IDE (full IDE)
- `SoulCoveNotebook.exe` — SoulCove Notebook (code editor)
- `SoulCoveNotebookLite.exe` — SoulCove Notebook Lite (Markdown notebook)

Qt DLLs and plugins are copied to the output directory automatically (bound to
the `SoulCoveIDE` target; all three products share the same output directory).

`compile_commands.json` is exported to `build/` for clangd indexing.

## CMake Configuration

### Qt Path Detection

Priority (high to low):
1. `-DSCNB_QT_PREFIX=...` (explicit, highest priority)
2. `-DCMAKE_PREFIX_PATH=...` (CMake variable)
3. `Qt6_DIR` environment variable (pointing to `lib/cmake/Qt6`)
4. `CMAKE_PREFIX_PATH` environment variable (semicolon-separated)

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `SCNB_QT_PREFIX` | *(empty)* | Qt installation prefix, e.g. `F:/Qt/6.5.3/mingw_64` |
| `SCNB_WITH_OPENSSL` | `ON` | Enable OpenSSL support (required by SSH) |
| `SCNB_WITH_SSH` | `ON` | Enable SSH remote support (libssh2 + SFTP) |

When OpenSSL is not found, SSH support is automatically disabled to avoid
link failures. Source code guards SSH sections with `#ifdef SCNB_WITH_SSH` /
`#ifdef SCNB_WITH_OPENSSL`.

### OpenSSL / zlib Auto-detection

When SSH is enabled, CMake auto-detects OpenSSL 3.x and zlib by scanning:
- `OPENSSL_ROOT_DIR` / `ZLIB_ROOT` environment variables
- MinGW `opt/` directory next to the compiler
- Common MinGW root (e.g. `G:/MinGW/mingw64/opt`)

OpenSSL 3.x is preferred (libssh2 1.11 requires the 3.0+ API); 1.x is used
only as a fallback with a warning.

### Multi Build Directory

When `CMAKE_BUILD_TYPE` is set (single-config generators like MinGW Makefiles),
outputs are separated into `build/Debug/` and `build/Release/`. Without it,
outputs go to `build/` directly (backward compatible).

## Coding Standards

- **Language**: C++17.
- **Framework**: Qt 6 Widgets. Prefer Qt signal/slot and smart pointers
  (`std::unique_ptr` / `QPointer`) over raw `new`/`delete`.
- **Header guards**: `#pragma once`.
- **Interface prefix**: pure interface classes are prefixed with `I`
  (e.g. `ILspClient`, `IEditorEdit`, `ICompleter`).
- **Comments**: Doxygen-style (`///`, `@brief`). Chinese comments are allowed
  for in-depth explanations.
- **User-visible strings**: always wrap with `tr()` for i18n.
- **Layering**: UI must not call core classes directly when an interface or
  controller exists. See [architecture.md](architecture.md).

## Logging & Debugging

- **Logger**: `utils/Logger.hpp` writes to `debug.log`.
- **LSP logs**: prefixed with `[LspClient]` / `[LspManager]` /
  `[LspCoordinator]`.
- **clangd logs**: stderr output; can be enabled in the settings page.
- **Performance**: `PerformanceMonitor` (singleton) reports `calls / avg /
  total` per tag via `summaryReport()`.

## LSP Integration

### clangd Setup

1. Ensure `compile_commands.json` is generated (CMake exports it by default).
2. Configure the clangd path in the settings page, or let the program
   auto-detect it.
3. The program automatically:
   - Searches upward for `CMakeLists.txt` / `.git` /
     `compile_commands.json` to infer the project root.
   - Extracts the compiler path from `compile_commands.json` and passes it to
     clangd via `--query-driver`.
   - Caches the compiler driver path to avoid repeated detection.

### Supported LSP Methods

| Method | Feature |
|--------|---------|
| `textDocument/completion` | Code completion |
| `textDocument/definition` | Go to Definition (F12 / Ctrl+Click) |
| `textDocument/hover` | Hover preview (200ms delay) |
| `textDocument/references` | Find References (Shift+F12) |
| `textDocument/documentSymbol` | Document symbols (outline) |
| `textDocument/signatureHelp` | Signature help |
| `textDocument/publishDiagnostics` | Diagnostics |
| `didOpen` / `didChange` / `didSave` / `didClose` | Document lifecycle |

## Internationalization

- Translation sources: `src/i18n/SoulCove_zh_CN.ts`, `SoulCove_en_US.ts`.
- Wrap user-visible strings with `tr()`.
- `.ts` files are compiled to `.qm` and embedded into each product via
  `qt6_add_translations` at build time (resource path `:/i18n/`).

## Configuration File

`src/config/sys_param.ini` stores:
- Theme settings
- Editor settings (font / tab width / indent style)
- LSP server paths
- Shortcut bindings
- Window state

## Shortcut Reference

Full shortcuts are available in the settings page
(`File → Settings → Shortcuts`). Common shortcuts:

| Shortcut | Action |
|----------|--------|
| Ctrl+N | New file |
| Ctrl+O | Open file |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save As |
| Ctrl+W | Close tab |
| Ctrl+Tab | Switch tab |
| Ctrl+F | Find |
| Ctrl+H | Replace |
| Ctrl+Space | Trigger completion |
| Ctrl+Shift+Space | Force completion |
| F12 | Go to Definition |
| Ctrl+Click | Go to Definition |
| Ctrl+← | Navigate back |
| Shift+F12 | Find References |
| Ctrl+Shift+O | Document symbols |
| Ctrl+Shift+P | Command palette |
| Ctrl+/ | Toggle comment |
| Ctrl+D | Select next occurrence |
| Alt+Click | Add secondary cursor |
| Alt+Click (on definition) | Definition preview popup |
| Ctrl+wheel | Zoom font |

## Internal Development History

Detailed iteration history (V1.3 – V1.9) and the original optimization
checklist (O1 – O44) are recorded in
[`开发迭代说明.md`](../开发迭代说明.md). These internal dev versions were
consolidated into the public **v1.0.0** release.
