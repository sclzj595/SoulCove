# Roadmap

This document describes the SoulCove release plan and the long-term
development milestones.

> Internal development versions V1.3 – V1.9 were consolidated into the first
> public release **v1.0.0**. GitHub users will only see semantic-version tags
> (`v1.0.0`, `v1.1.0`, `v2.0.0`, ...), not the internal dev versions.

---

## Release Plan

| Version | Release Name | Theme |
|---------|--------------|-------|
| **v1.0.0** | Initial Public Release | First open-source release. All current capabilities: LSP, Markdown, Git, Terminal, Remote, Multi Workspace, Debug, Tasks, Snippets, Hover. |
| **v1.1.0** | Core Stability | Eliminate crashes, memory leaks, LSP exceptions, file encoding issues, undo/redo bugs. |
| **v1.2.0** | Performance Update | Faster editor, lower memory, faster syntax highlight, incremental rendering, theme switching optimization. |
| **v1.3.0** | LSP Improvement | VSCode-level LSP stability. |
| **v1.4.0** | UI Polish | UI / UX optimization. |
| **v2.0.0** | Architecture Upgrade | Fully mature architecture, plugin-ready. |

### v1.0.0 — First Public Release

Includes the full current capability set:

- LSP (clangd integration)
- Markdown (live preview, Mermaid, export)
- Git (blame, history, merge conflict)
- Terminal (ANSI, output filtering)
- Remote (SSH, SFTP, remote LSP, tmux)
- Multi Workspace
- Debug (GDB/MI)
- Tasks (`tasks.json`)
- Snippets
- Hover (Markdown rich text)

### v1.1.0 — Core Stability

Goal: eliminate all crashes.

- Crash fixes
- Memory leak fixes
- LSP exception handling
- File encoding correctness
- Undo / Redo correctness

### v1.2.0 — Performance Update

Goal: performance across the editor.

- Edit 1,000,000 lines without lag
- LSP with 10,000 symbols instant open
- Theme switch in ~10ms
- Incremental rendering
- Lower memory usage

### v1.3.0 — LSP Improvement

Goal: VSCode-level LSP stability.

### v1.4.0 — UI Polish

Goal: UI / UX optimization.

### v2.0.0 — Architecture Upgrade

Goal: the architecture is fully mature and ready for plugins.

---

## Milestones (Long-term Direction)

After v1.0.0, development moves from a bug-fix mindset (`O1`, `O2`, ...) into a
real software lifecycle organized around milestones.

```
1.0
 │
 ▼
Stability        (Milestone 1)
 │
 ▼
Performance      (Milestone 2)
 │
 ▼
Plugin System    (Milestone 3)
 │
 ▼
AI               (Milestone 4)
 │
 ▼
Workspace        (Milestone 5)
 │
 ▼
Marketplace      (Milestone 6)
 │
 ▼
2.0
```

### Milestone 1 — Core Stability (→ v1.1.0)

Eliminate all crashes:
- Crashes
- Memory leaks
- LSP exceptions
- File encoding
- Undo / Redo

Only stability work in this milestone.

### Milestone 2 — Performance (→ v1.2.0)

Everything around performance:
- Edit 1,000,000 lines without lag
- LSP with 10,000 symbols, instant open
- Theme switch in ~10ms

### Milestone 3 — Plugin System

The real next major direction. Build:
- `PluginManager`
- `PluginLoader`
- `PluginInterface`
- `PluginAPI`

Future: Markdown, Git, Terminal, Debugger can all be pluginized.

### Milestone 4 — AI

The biggest competitive advantage. Support:
- Ollama
- OpenAI
- Claude
- Gemini
- DeepSeek
- Qwen

Like VSCode: chat, code completion, explain code, fix bugs, generate code.

### Milestone 5 — Workspace

Real VSCode Workspace:
- Multiple folders
- Multiple Git repos
- Multiple compile databases
- Multiple LSP servers

### Milestone 6 — Marketplace

Extension marketplace. Users download:
- Plugins
- Themes
- Snippets
- Languages
- Debuggers

All from the marketplace.

---

## Future Direction Summary

```
1.0 → Stability → Performance → Plugin → AI → Workspace → Marketplace → 2.0
```
