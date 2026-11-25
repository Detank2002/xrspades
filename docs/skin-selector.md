# In-game Mods selector

The home screen / server list **Mods** tab remains the enable/disable manager,
with its existing downloads, apply order and Apply changes button. Enable the
mods you want available there; selecting one in-game does **not** untick its
alternatives or reorder that collection.

In a game, open **Esc → Mods**:

- Choose **Spade**, **Grenade**, **Rifle**, **SMG**, **Shotgun**, **Font** or
  **SFX** on the left.
- The grid contains **Default** and that category's enabled, installed mods.
  Scroll to reach additional rows. Opening a category brings its current
  selection into view.
- Click a card once to select and apply it immediately. The selected card
  uses the pause menu's light-gray button state; there is no Apply step.
- The page uses the pause menu's black translucent overlay/panel and standard
  grayscale buttons. Its only heading is **Mods**: no explanatory labels,
  equipped badges or equipped footer.
- Weapon cards show independently loaded, lit, rotating voxel models. Font
  cards show an **Aa** sample, using the pack's scalable face when available
  (the GUI face for atlas-only language packs). SFX cards use a speaker icon.
- All seven choices are independent. Default restores the stock resources
  for that category, even while its alternatives remain enabled.
- **Back** or **Esc** returns to the pause menu.

Player, Intel and Tent are deliberately not categories in this selector.

## Storage and filtering

`EnabledMods.txt` remains the ordered collection managed by the home screen.
`SelectedSkins.txt`, alongside it in the user resource directory, stores the
choices separately, with keys `spade`, `grenade`, `rifle`, `smg`, `shotgun`,
`font` and `sfx`. Existing five-category preferences remain valid. An empty
name explicitly selects Default; an absent preference retains the previous
last-enabled-wins behavior. A saved mod that has been disabled or removed
falls back to the last available mod for its category, or Default.

The catalog recognizes the standard weapon filename tags (`SEMI` and `RIFLE`
both mean Rifle), `FONT` and `SFX`, and discovers custom/folder packs from
their resource paths. Font owns `Fonts/` and `Gfx/Fonts/`, including scalable
faces and fallback atlas descriptions/images. SFX owns shared `Sounds/`
resources (feedback, killstreak cues, footsteps, impacts, etc.). Weapon-specific
sounds under `Sounds/Weapons/<weapon>/` remain with their weapon selection.
Mixed-category packs are filtered by resource path, so selecting their Rifle
cannot replace a separately chosen SMG, Font or SFX.

## Previews and live changes

Weapon previews capture the **assembled idle weapon**, not one guessed main
model and not every KV6 in the archive. A private AngelScript engine runs the
candidate's view skin against a recording renderer. Only actual `AddModel`
draws are kept: alternate sights, empty magazines, casings and reload states
that are not drawn in that pose stay out. The result preserves each part's
matrix, scale, pivot, custom color and ghost opacity. Long sprites such as a
lightsaber blade become depth-tested additive glow, rather than a solid box.
No pack-name-specific assembly rules are used.

The recorder reads from the candidate's archives, then stock resources, never
the equipped overlay or renderer's shared model cache. Default uses stock
resources exclusively. If a stock view does not draw any model supplied by a
model-only candidate, or capture fails, the catalog tries its third-person
skin, then a bounded body-model fallback. Fallback diagnostics go to the log;
a bad preview cannot prevent selecting its card.

Browsing never mounts a candidate or runs it in the live game's script module.
The recording engine exposes local config snapshots, inert audio/image
objects and a small procedural-voxel API, with no file/network/world API or
settings writer. It uses its own deterministic random generator, not gameplay's
RNG. The snapshot includes the current sight/charm settings and the local
player color used by the held weapon. The pose is raised, ready, not aiming,
not reloading, with a full magazine and settled idle updates.

The preview mesh preserves original surface colors and thin parts. It only
merges equal-color coplanar faces, never downsampling filled interior voxels
into the surface. Parts are transformed before computing shared occupied
bounds, so attachments retain their relative positions throughout rotation.
A shallow orthographic view and neutral scalar lighting preserve the KV6
palette's hue and contrast, without a per-channel gamma/brightness lift.
Only exactly-black voxels take that draw's actual `customColor`; other colors
are not tinted. Per-pixel depth, translucent-face ordering and four-sample
antialiasing produce an ordinary straight-alpha sRGB RGBA image, compatible
with the regular UI and VR menu surface. Only visible cards rasterize/upload,
at 24 Hz, with one image draw per card.

Candidate KV6 reads are capped at 34 MiB before ZIP decompression, script
sections at 512 KiB (2 MiB and 32 sections total), and capture at 128 model
draws / 64 beams. Script execution has a line-callback/deadline budget and
stack/call-depth limits. KV6 dimensions/cell counts and mesh complexity are
bounded (512 × 512 × 64, at most four million cells per model, eight million
loaded cells total and 100,000 merged faces); thumbnail images are at most
512 × 512. This is a restricted in-process compatibility VM, **not** an OS
sandbox: script compilation and general string/array allocations do not have
hard process-level CPU/memory quotas. A corrupt or excessively complex preview
is logged and omitted; its named card stays usable.

Models/images/sounds/scripts use the existing live-reload path. Font changes
also refresh existing font holders in place: new FreeType sets and fallback
atlases are staged before publication, and existing glyph/measurement/raster
caches invalidate before their next use. IFont pointers held by the UI, chat
and other FontManagers remain stable. Returning Font to Default removes
mod-only fallback glyphs as well as replacing scalable faces. The home-screen
manager's existing restart behavior for font/layout changes is unchanged;
only the in-game selector opts into the staged font reload.

SFX reloads existing cached audio chunks. Returning to Default restores stock
sounds; optional mod-only sounds with no stock replacement become silence,
rather than continuing to play the unequipped pack.

A script/font failure or a restart-only resource reports an error and keeps
the previous selection and preference. Rollback retains the exact previously
mounted archives in memory, without rereading their installed copies. Unlike
the home screen's Apply, the selector never drops mods from the collection to
repair a failure. A preference-write failure is reported as session-only.
`--try-mod` stays isolated.

## Verification

The [audit record](testing/skin-selector-audit.md) distinguishes executed
headless/native checks from remaining device checks and static-analysis limits.

In a normally configured build:

```sh
cmake -S . -B build
cmake --build build --target zs_tests
ctest --test-dir build --output-on-failure
```

Headless selector regressions:

- `skin_selection`: all seven categories, resource ownership, independent
  choices, Default vs unset, stale preferences and persistence compatibility.
- `voxel_model_preview`: exact/merged surfaces, no pink interior leakage on
  high-resolution bodies, per-pixel occlusion, antialiased RGBA output,
  rotation/bounds, multipart assembly against a reference, pivots, mirrored /
  nonuniform transforms, black-only custom color, opacity and invalid inputs.
- `skin_overlay`: production ZIP/catalog/mount/filter/change-detection code,
  candidate-specific assembled previews, stock-view → third-person → body
  fallbacks, Default below the equipped overlay, immediate choices, persistence,
  mixed/folder packs and failed script/backend/font rollback. Preview capture
  uses the real private VM; live script reload uses a failure-marker stand-in.
  Application/collection I/O, diagnostics and GPU/audio devices are headless
  substitutes. Font refresh itself is tested separately below.
- `skin_preview_python`: stdlib Python tests invoke the production preview
  recorder/mesher/rasterizer fixture. Tests all five stock assemblies across
  four sight settings × charms off/on, plus synthetic active-vs-registered
  parts, transforms/pivots/colors, procedural-model ownership, local config
  writes, inert audio, bad scripts/paths/models and execution/draw/read limits.
  Each successful scene is checked at 24 angles, with RGBA/antialiasing checks.
- `font_reload`: real FreeType, FontManager, fallback descriptions and glyph
  caches, using bundled fonts and headless image devices/atlas codec. Checks
  stable existing holders across renderers, new/default faces and fallback
  glyphs, failed face/descriptor/atlas uploads, and image lifetimes.

### All official packs (optional network fixture)

`Server/test/fixtures/skin_preview_packs.json` pins all 18 packs from
`zerospades/zerospades-paks` at revision
`367d002311a3e0f29c65e064d0e2c83e6598b51b`, with sizes and SHA-256 hashes.
Downloads are explicit, never part of a normal configure/CTest run:

```sh
python3 Server/test/test_skin_previews.py build/Server/test/skin_preview_fixture Resources \
  --fetch --packs-dir build/official-skin-paks --artifacts build/preview-audit
```

Use the actual `skin_preview_fixture` executable path for your build layout.
Subsequent runs omit `--fetch`. To include the local corpus in CTest, configure
with `-DZS_TEST_SKIN_PACKS="$PWD/build/official-skin-paks"` and run
`ctest --test-dir build -R skin_preview --output-on-failure`.

The corpus run verifies all 18 archive hashes/ZIPs. All **13 weapon packs** and
**five stock weapons** exercise four sight settings × charms off/on: **144
scene configurations**, each checked through 24 angles, with explicit active
attachment expectations. The four Font packs and one SFX pack have no voxel
assembly, so this corpus test checks their archives, not visual/audio behavior.
Rendered JSON and raw RGBA frames stay under the ignored `build/` directory.

Manual native checks (not replaced by headless tests):

1. Enable multiple weapon skins, two Fonts and an SFX pack on the home screen.
   Verify its manager UI is unchanged.
2. Join a game. Compare Esc → Mods with the pause menu palette; verify only
   the Mods title, seven category names, card names/samples and Back remain.
3. Switch each weapon between two skins and Default. Check the highlight and
   held weapon change immediately; other choices and enabled checkboxes stay
   unchanged. Inspect stock and detailed custom previews throughout a turn.
4. Change Font repeatedly, then Default. Check existing HUD, pause-menu, chat
   and VR keyboard text, including non-Latin fallback glyphs where available.
5. Change SFX and then Default. Check both a replaced stock sound and mod-only
   killstreak cues; the latter must stop playing with Default selected. Weapon
   skin sounds must remain unchanged.
6. Scroll more than three rows; change categories; resize; close/reopen and
   restart to check layout, selection visibility and persistence.
7. Try broken scripts/fonts: the old choice must remain active and every
   collection entry must stay enabled. Check available native GL, Vulkan,
   software and VR backends, including frame timing while thumbnails rotate.
