# Skin-selector / assembled-thumbnail audit

Date: 2026-11-24. See [the selector guide](../skin-selector.md) for behavior,
limits, corpus commands and the remaining manual/device checklist. Downloaded
paks, native binaries, logs and rendered frames stay in ignored `build/`.

## Code-path review

The review followed these blocks rather than treating a successful compile as
UI validation:

- `ModsWindow`: category/filter construction, row/card ownership, deferred
  preview loading, per-card image lifetime, frame/size cache, click callback,
  highlight from the committed selection, failure alert, close/Escape owner
  lifetime and resize/disconnection integration with `ClientUI`.
- `UIManager`, `UIElement`, `ButtonBase`, `ListView`, `MessageBox`: event
  dispatch/capture, detachment during callbacks, visible-row creation,
  model/row teardown and modal return. The home-screen Mods manager is not
  replaced or restyled by this change.
- `SkinCatalog`: only the enabled collection, archive precedence and `.weak`,
  independent candidate → base reads, active-model contribution checks,
  view → third-person → bounded body fallback, Default below the live overlay,
  diagnostics and font-face reads.
- `SkinPreviewScript` / private compatibility API / shared math registration:
  separate engine and module, actual draws rather than registrations, idle
  inputs and settings/player-color snapshot, independent deterministic RNG,
  procedural-model reference ownership, include/path/read/draw/runtime budgets
  and all-or-nothing errors. No live renderer/audio/config/filesystem binding
  is registered in that engine. It is not an OS sandbox.
- `VoxelModelPreview`: pivot before draw matrix, shared occupied bounds,
  inverse-transpose normals and mirrors/nonuniform scale, equal-color surface
  merging, black-only custom color, opacity ordering/depth, additive beams,
  full-turn projection, supersampling and straight-alpha output.
- `SkinSelection`, `ModOverlay`, resource-change and Font/SFX reload paths:
  category ownership, immediate apply, staged publication, rollback and saved
  preferences versus the independently enabled collection.

## Automated checks executed

- Normal client-test harness: **13/13 CTest tests passed**, including the real
  preview VM, catalog/overlay, font reload, Python fixture tests, Vulkan shader
  reflection and the optional complete official-pak corpus.
- AddressSanitizer with leak detection and halt-on-error: **12/12 passed** in
  the non-Vulkan headless harness, including the official corpus. The shader
  reflection target is absent from that configuration, not a skipped failure.
- All **18** pinned official archives were hash/ZIP checked. Their **13 weapon
  packs** plus **five stock weapons** were rendered with four sight settings ×
  charms off/on: **144 scene configurations**, each checked at **24 angles**.
  Expected active attachment lists, transforms/pivots/colors, nonempty framed
  output, rotation and antialiasing are asserted. Fonts/SFX are not voxel
  scenes; this corpus test checks their archives, not their visual/audio output.
- The C++ mesher tests compare separately transformed multipart/pivot/mirror
  geometry with an assembled reference, and cover custom color, translucency,
  malformed transforms, surfaces/depth/limits. Catalog regressions exercise
  assembled scripts, `.weak` parts, Default isolation and fallback ordering.
- Native client target rebuilt successfully with Vulkan enabled (OpenGL and
  OpenXR disabled in this sandbox build). The actual **software-rendered**
  startup check passed: real main module, Unifont atlas/PNG decoding, rendered
  main menu, screenshot and clean exit. Profile setup is bypassed with an
  isolated test name, not with changes to the user's preferences.
- Refreshed 18-weapon contact sheet inspected, including the additive
  lightsaber blade, under `build/skin-inspect/full-audit/assembled-audit.jpg`.

## Static analysis and fixes

Cppcheck 2.22.0 ran with `warning,style,performance,portability`, exhaustive and
inconclusive checks, the actual native compile database and 21 relevant
translation units (including the UI framework, selector, reload paths and
private recorder). Reports are `build/ui-audit/cppcheck-final.{xml,log}`.
The scan is **not a clean whole-project static-analysis pass**: inherited
header/style findings remain and the overloaded native AngelScript method
macros in `MathScript.cpp` cause a Cppcheck parser error. That translation unit
was compiled by the real compiler and exercised by both preview and native
startup tests; the parser error is not counted as a successful analysis.

The high-risk findings in the changed path were checked against their actual
uses. Fixes include initializing preview aggregate members, the internal ZIP
stream cursors and fallback-font result size; making ZIP ownership explicitly
non-copyable; bounding the stock body fallback read as well as candidate reads;
and preventing an internal ZIP close exception from escaping its destructor
while a failed selection/preview read is unwinding. The injected backing-stream
failure regression **aborted with SIGABRT against the preceding ZIP source**
(`Zip file close error: 0xffffff9a`) and passes with the destructor fix. This
is a distinct error path, not a diagnosis of the reported Vulkan water crash.

## Limits of this validation

- Strict ASan+UBSan experiments **did not pass**: the bundled AngelScript VM
  reports DWORD-aligned pointer bytecode, zero-length null `memcpy`, and
  misaligned `std::string` values on its script stack. These pre-existing VM
  conventions were not patched or suppressed in production. The passing run
  above is AddressSanitizer, not a claim of UBSan-clean VM/skin execution.
- A native Vulkan launch was attempted with both available software ICDs.
  They lack X11 surface/presentation support (Lavapipe lacks `VK_KHR_surface`;
  SwiftShader lacks XCB/XLIB surfaces). Startup stops before Vulkan's water
  renderer, so the previously reported water `Bus error` is **not reproduced
  or cleared** here. Reflection/unit tests do not establish GPU startup.
- No GL/VR/device validation or interactive in-game event/screenshot suite is
  claimed by the headless fixture tests. Complete the manual checklist on the
  target Debian 13 installation and actual graphics/headset backends.
