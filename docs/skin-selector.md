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

Weapon previews read a KV6 body directly from that mod's archives, never the
renderer’s shared model cache. Simple literal `RegisterModel` references in
`ThirdPerson.as` are preferred over placeholder canonical bodies; renamed
model-only packs have a body-model fallback. This is a body preview, **not**
a full script-assembled scene. Browsing never mounts a candidate or executes
its scripts. Default reads below the mod overlay; sound/script-only weapon
mods use the stock body.

The preview mesh preserves original surface colors and thin parts. It only
merges equal-color coplanar faces, never downsampling filled interior voxels
into the surface. A shallow, well-lit orthographic view is framed from the
occupied bounds throughout a full turn. Neutral scalar lighting preserves the
KV6 palette's hue and contrast; there is no per-channel gamma/brightness lift
or translucent tint. Black team-color voxels use neutral charcoal, not arbitrary
blue. Per-pixel depth and four-sample antialiasing produce an ordinary sRGB RGBA
image, compatible with the regular UI and VR menu surface.
Only visible cards rasterize/upload, at 24 Hz, with one image draw per card.

Candidate KV6/face reads are capped at 34 MiB before ZIP decompression and
preview script reads at 512 KiB. KV6 dimensions/cell counts and mesh complexity
are bounded (512 × 512 × 64, at most four million cells and 100,000 merged
faces); thumbnail images are at most 512 × 512. A corrupt or excessively
complex preview is logged and omitted; its named card stays usable.

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
  rotation/bounds throughout a turn, safe limits and all five stock bodies.
- `skin_overlay`: production ZIP/catalog/mount/filter/change-detection code,
  independent previews (including script-named bodies), immediate choices,
  persistence, mixed/folder packs and failed script/backend/font rollback.
  Application/collection I/O, diagnostics, GPU/audio devices and script/font
  runtimes are replaced here. Font refresh itself is tested separately below.
- `font_reload`: real FreeType, FontManager, fallback descriptions and glyph
  caches, using bundled fonts and headless image devices/atlas codec. Checks
  stable existing holders across renderers, new/default faces and fallback
  glyphs, failed face/descriptor/atlas uploads, and image lifetimes.

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
