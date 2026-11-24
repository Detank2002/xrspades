# In-game skin selector

The home screen / server list **Mods** tab is still the enable/disable list,
with the same downloads, apply order and Apply changes button. Enable every
skin you want available there; equipping a skin does **not** untick its
alternatives or reorder that list.

In a game, open **Esc → Mods**:

- Choose **Spade**, **Grenade**, **Rifle**, **SMG** or **Shotgun** on the left.
- The grid on the right contains **Default** and that weapon's enabled,
  installed skins. Scroll to reach additional rows.
- Cards contain independently loaded, lit, rotating KV6 weapon models. Click
  a card once to equip it immediately. The equipped card has a gold border
  and an Equipped label; there is no Apply step.
- Each weapon has its own choice. Default restores the stock resources for
  that weapon, even while other skins for it remain enabled.
- **Back** or **Esc** returns to the pause menu.

Player, Intel and Tent are deliberately not categories in this selector.

## Storage and compatibility

`EnabledMods.txt` remains the ordered collection managed by the home screen.
`SelectedSkins.txt`, alongside it in the user resource directory, stores the
per-weapon choices separately. An empty name explicitly selects Default; an
absent preference retains the previous last-enabled-wins behaviour. A saved
skin that has been disabled or removed falls back to the last available skin
for that weapon, or Default if none remain.

The catalog recognises the standard `SPADE`, `GRENADE`, `SEMI` (Rifle), `SMG`
and `SHOTGUN` filename tags (also `RIFLE`), and discovers custom/folder packs
from their weapon resource paths. Multi-weapon packs are filtered per weapon,
so choosing their Rifle cannot override a separately chosen SMG. Fonts and
other non-weapon mods retain their normal overlay order.

Previews read the pack's KV6 weapon body directly from its own archives, not
from the renderer's shared model cache. Default reads below the mod overlay.
Previewing never mounts a skin or runs its scripts; script-only/sound-only
skins use the stock body. Custom names are supported with a body-model
fallback. An invalid/oversized preview shows “Preview unavailable” instead
of interrupting the menu. Model dimensions, decompressed candidate payloads
and surface complexity are bounded, and only visible rows render.

Normal model/image/sound/skin-script changes use the existing live-reload
path. If a skin's scripts fail, or the change requires a restart, the click
reports the problem and keeps the previous equipped skin and preference.
Rollback retains the exact previously mounted archives in memory; it does
not depend on their installed copies still being on disk. Unlike the home
screen's Apply, the selector never drops mods from the collection to repair
a script failure. A disk-write failure is reported as
an equipped-for-this-session-only choice. `--try-mod` stays isolated.

## Verification

In a normally configured build:

```sh
cmake -S . -B build
cmake --build build --target zs_tests
ctest --test-dir build --output-on-failure
```

New headless tests:

- `skin_selection`: category/path classification, independent choices,
  Default vs unset, stale preferences, persistence format and pack filtering.
- `voxel_model_preview`: actual voxel meshing, hidden-face removal, depth
  ordering, rotation, bounds throughout a full turn, complexity limits, and
  all five shipped weapon models.
- `skin_overlay`: real zip/pak fixtures with headless renderer/audio devices
  and a script-build failure marker. Checks isolated previews, hot-reload
  change detection, collection preservation, default/named persistence,
  combined/folder packs and rollback. Only the application/collection I/O,
  diagnostics and script runtime are replaced; mounting/catalog/model code
  is the production implementation.

Manual in-game checks:

1. Enable two Rifle skins, a Spade skin and a non-weapon mod on the home
   screen and Apply. Verify the home screen list has not changed.
2. Join a game, open Esc → Mods, switch between all five categories and
   confirm disabled skins and unsupported categories are absent.
3. Select each Rifle skin, then Default. Verify the gold highlight and held
   weapon change without another button, and the Spade/non-weapon mod remain
   unchanged. Close/reopen the menu and restart to check persistence.
4. Enable enough skins for more than two grid rows. Check scrolling,
   category changes, and window resizing; models stay inside their cards.
5. Try a skin with broken scripts. The previous skin must stay equipped and
   all of the home screen's enabled entries must remain enabled.
6. Check both monitor and VR menu rendering on available native backends.
