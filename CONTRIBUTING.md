# Contributing to SoulCove

Thanks for your interest in contributing to SoulCove! This document explains
how to set up a development environment and submit changes.

## Getting Started

1. **Fork** the repository and clone your fork with submodules:

   ```bash
   git clone --recurse-submodules https://github.com/<your-username>/SoulCove.git
   ```

   If you already cloned without `--recurse-submodules`, initialize them:

   ```bash
   git submodule update --init --recursive
   ```

2. Make sure you have the build dependencies installed (see
   [README](README.md#build) and [docs/development.md](docs/development.md)).
3. Create a feature branch:

   ```bash
   git checkout -b feature/my-feature
   ```

4. Build and test your changes.
5. Commit and push to your fork, then open a Pull Request.

## Build

```bash
mkdir build && cd build
cmake -DSCNB_QT_PREFIX=<your-qt-prefix> -G "MinGW Makefiles" ..
cmake --build . --config Release
```

`compile_commands.json` is exported to `build/` so clangd can index the project.

## Coding Standards

- **Language**: C++17.
- **Framework**: Qt 6 Widgets. Prefer Qt signal/slot and smart pointers
  (`std::unique_ptr` / `QPointer`) over raw `new`/`delete`.
- **Header guards**: use `#pragma once`.
- **Interface prefix**: pure interface classes are prefixed with `I`
  (e.g. `ILspClient`, `IEditorEdit`).
- **Comments**: Doxygen-style (`///`, `@brief`). Chinese comments are allowed
  for in-depth explanations; user-visible strings must be wrapped with `tr()`.
- **Layering**: respect the architecture layers
  (see [docs/architecture.md](docs/architecture.md)). UI must not call core
  classes directly when an interface or controller exists.

## Commit Messages

Use the conventional format:

```
<type>(<scope>): <subject>

<body>
```

- `type`: `feat`, `fix`, `perf`, `refactor`, `docs`, `build`, `chore`, `i18n`.
- `scope`: affected module (e.g. `lsp`, `editor`, `git`, `terminal`, `remote`).
- `subject`: short imperative description.

Example:

```
feat(lsp): add signature help support
fix(editor): prevent fold timer being shared across instances
```

## Pull Requests

- Keep PRs focused — one logical change per PR.
- For major changes, open an issue first to discuss the design.
- Make sure the project builds cleanly in Release mode for all three products
(SoulCove IDE, SoulCove Notebook, SoulCove Notebook Lite).
- If your change affects user-visible strings, update the `.ts` translation
  files in `src/i18n/`.
- Reference the issue number in the PR description (e.g. `Closes #42`).

## Reporting Bugs

When opening an issue, include:
- SoulCove version (or git commit).
- OS / compiler / Qt version.
- Steps to reproduce.
- Expected vs. actual behavior.
- Relevant logs from `debug.log` (LSP entries are prefixed with
  `[LspClient]` / `[LspManager]` / `[LspCoordinator]`).

## Internationalization

- Translation sources live in `src/i18n/SoulCove_zh_CN.ts` and
  `SoulCove_en_US.ts`.
- Wrap every user-visible string with `tr()`.
- `.ts` files are compiled to `.qm` and embedded into each product at build
  time via `qt6_add_translations`.

## License

By contributing, you agree that your contributions will be licensed under the
project's [MIT License](LICENSE).
