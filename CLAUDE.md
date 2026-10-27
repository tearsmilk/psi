# Repo notes for Claude

- Пси / PSI (Ψ) — универсальная CAD-система, этап 00–01 (Draft MVP): C++20, Qt 6 Widgets, CMake + Ninja, macOS 11+ Intel x64.
  Russian UI, Claude-like style (ivory/charcoal, terracotta #D97757 accent, serif headings, minimal animation) — `ui/theme.cpp`.
- Layout: `core/` (geometry, units, attributes, document, command-system, project), `modules/drawing/` (sheets),
  `modules/draft/` (tools, snap, input parser, demo scenario), `io/native` (`.psi` = ZIP + JSON, schema `kSchemaVersion`),
  `io/dxf`, `io/pdf`, `rendering/` (renderer + `Viewport`), `ui/` (theme, icons), `apps/psi-desktop/` (main window,
  docks, sheets page, dialogs, welcome, `--selftest`), `tests/` (QTest, run offscreen), `packaging/macos/`, `docs/`.
- Build: `cmake --preset release && cmake --build --preset release && ctest --preset release`; `psi --selftest` must stay green.
  Build must be warning-free (`-Wall -Wextra`), `QT_NO_KEYWORDS` (use `Q_SIGNALS` / `Q_EMIT`).
- Model units are always millimetres; display/parse through `Units`. Document edits go through commands (undo/dirty).
- Any `.psi` schema change: bump `kSchemaVersion`, add a migration in `PsiFile::read`, add a test.
- **Design lives in Figma** (file «Пси — UI», team Lotus): `design/figma/01_ui_kit.js` + `02_screens.js`. When you add or
  change a screen, dialog or component, update the scripts in the same change and run them via the Figma MCP `use_figma`
  tool (Starter plan has a small monthly call limit — if hit, schedule a retry and tell the user).
- CI: `.github/workflows/psi-macos.yml` — build + tests on every push, release on tag `psi-v*` or `workflow_dispatch`
  with `release_version`. No secrets are needed.
- Never commit API keys or proprietary SDKs. OCCT is behind `PSI_WITH_OCCT` (off) for stages 02+.
- Commit messages in Russian; one logical step per commit.
