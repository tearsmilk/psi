# Repo notes for Claude

- Пси / PSI (Ψ) — универсальная CAD-система, этапы 00–01 (Draft MVP) + 02 «Эскиз» и «Модель» (3D на OCCT) + 05 «Интерьер» (2D): C++20, Qt 6 Widgets, CMake + Ninja, macOS 11+ Intel x64.
  Russian UI, Claude-like style (ivory/charcoal, terracotta #D97757 accent, serif headings, minimal animation) — `ui/theme.cpp`.
- Layout: `core/` (geometry incl. `ops` = join / split / cut, units, attributes, document, command-system, project), `modules/drawing/` (sheets),
  `modules/draft/` (tools, `boolean` = QPainterPath set ops, `edit` = value editing for the double-click editor, snap + grid magnet, input parser, demo scenario),
  `modules/sketcher/` (`sketch.*` model + constraint solver in psi_core, `sketch_view.*` canvas in psi_ui), `modules/interior/`
  (`building.*` model / rooms / schedules in psi_core, `interior_view.*` plan canvas in psi_ui), `modules/model/` (lib `psi_model`, only with
  `PSI_WITH_OCCT`: `kernel.*` = pimpl facade over OCCT — the only files including OCCT headers are `kernel.cpp`, `io/step/step.cpp`,
  `io/stl/stl.cpp` via `kernel_occt.h`; `part.*` = feature tree, rebuild from the changed feature, bodies, mass props, JSON param keys documented
  in `part.h`), `io/native` (`.psi` = ZIP + JSON, schema `kSchemaVersion`), `io/dxf`, `io/pdf`, `io/step`, `io/stl` (reports like DXF),
  `rendering/` (renderer + `Viewport`; `view3d.*` = software z-buffer 3D view with id-buffer picking, offscreen-safe), `ui/` (theme, icons,
  `inline_editor`, `canvas` = shared 2D canvas of the sketch / interior workspaces), `apps/psi-desktop/` (main window, docks, sheets / sketch /
  interior / model pages (`model_page.*`, compiled only with OCCT, foreign parts `models/<id>.json` + cache `assets/model_<id>.brep`),
  `workspace_common.*` snapshot undo + side sections, dialogs, welcome, `--selftest` incl. `selftest_modules.cpp`), `tests/` (QTest, run offscreen),
  `packaging/macos/`, `docs/`, `vcpkg.json` + `packaging/vcpkg-triplets/x64-osx-11.cmake` (OCCT for CI).
- Build: `cmake --preset release && cmake --build --preset release && ctest --preset release`; `psi --selftest` must stay green.
  `PSI_WITH_OCCT` defaults to ON when `find_package(OpenCASCADE)` succeeds (Debian/Ubuntu: `libocct-*-dev`; the package links `libtbb.so`,
  so `libtbb-dev` or a symlink may be needed), otherwise the «Модель» button is disabled («Собрано без OCCT»). Keep both configurations building.
  Build must be warning-free (`-Wall -Wextra`), `QT_NO_KEYWORDS` (use `Q_SIGNALS` / `Q_EMIT`).
- Model units are always millimetres; display/parse through `Units`. Document edits go through commands (undo/dirty).
- Any `.psi` schema change: bump `kSchemaVersion`, add a migration in `PsiFile::read`, add a test. Sketches and buildings
  live as foreign parts `sketches/<id>.json` / `interior/<id>.json` (`Project::foreignParts()`) — new part types need no bump.
- **Design lives in Figma** (file «Пси — UI», team Lotus): `design/figma/01_ui_kit.js` + `02_screens.js`. When you add or
  change a screen, dialog or component, update the scripts in the same change and run them via the Figma MCP `use_figma`
  tool (Starter plan has a small monthly call limit — if hit, schedule a retry and tell the user).
- CI: `.github/workflows/psi-macos.yml` — build + tests on every push, release on tag `psi-v*` or `workflow_dispatch`
  with `release_version`. No secrets are needed.
- Never commit API keys or proprietary SDKs. OCCT stays behind `PSI_WITH_OCCT` and the `psi::md::Kernel` facade — no OCCT headers outside `psi_model`.
- Adding a 3D feature: keys in `part.h`, a branch in `Part::applyFeature`, a `schemaFor` row + toolbar entry in `model_page.cpp`, an icon, a `test_model` case.
- Adding a tool: `toolIds()` / `createTool()` in `modules/draft/tools.cpp`, an icon in `ui/icons.cpp`, a group in `ToolsPanel`
  (`apps/psi-desktop/panels.cpp`), hotkey + alias + menu in `main_window.cpp`, the count in `selftest.cpp`, README tool list.
- Commit messages in Russian; one logical step per commit.
