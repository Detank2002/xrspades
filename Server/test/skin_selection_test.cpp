// Skin choices are separate from the main menu's enabled collection.
// No game, filesystem, renderer or script runtime is needed for these checks.

#include <cstdio>
#include <string>
#include <vector>

#include <Gui/SkinSelection.h>

using namespace spades::gui;

namespace {
	int failures = 0;
	void Check(bool ok, const char* what) {
		if (!ok) {
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}
	std::size_t Index(SkinType type) {
		return static_cast<std::size_t>(type);
	}
	SkinMod Mod(const std::string& name, std::initializer_list<SkinType> types) {
		SkinMod mod;
		mod.name = name;
		for (auto type : types)
			mod.types[Index(type)] = true;
		return mod;
	}
} // namespace

int main() {
	static_assert(kSkinTypeCount == 7 && kWeaponSkinTypeCount == 5,
	              "Five weapons plus independent Font and SFX categories");
	Check(!IsWeaponSkin(SkinType::Font) && !IsWeaponSkin(SkinType::SFX) &&
	        SkinModelDirectory(SkinType::Font) == nullptr &&
	        SkinPreviewPath(SkinType::SFX) == nullptr,
	      "non-voxel categories cannot index weapon model tables");
	Check(SkinTypeForModName("SEMI-AWP-Author.pak") == SkinType::Rifle,
	      "SEMI is the rifle category");
	Check(SkinTypeForModName("rifle-Custom-.zip") == SkinType::Rifle,
	      "case-insensitive rifle alias");
	Check(SkinTypeForModName("SPADE-Crowbar-.pak") == SkinType::Spade, "spade tag");
	Check(SkinTypeForModName("GRENADE-Pineapple-.pak") == SkinType::Grenade, "grenade tag");
	Check(SkinTypeForModName("SMG-Vector-Nuceto.pak") == SkinType::SMG, "SMG tag");
	Check(SkinTypeForModName("SHOTGUN-Remington-.pak") == SkinType::Shotgun, "shotgun tag");
	Check(SkinTypeForModName("FONT-Noto-Google.pak") == SkinType::Font, "font tag");
	Check(SkinTypeForModName("sfx-Killsounds-.pak") == SkinType::SFX, "SFX tag");
	for (const char* name : {"PLAYER-Test-.pak", "INTEL-Test-.pak", "TENT-Test-.pak", "M24.pak"})
		Check(!SkinTypeForModName(name), "unsupported/unstructured name is not misclassified");

	Check(SkinTypeForPath("Models\\Weapons\\RIFLE\\Weapon.kv6.weak") == SkinType::Rifle,
	      "normalizes model paths");
	Check(SkinTypeForPath("Scripts/Skin/SMG/View.as") == SkinType::SMG,
	      "skin scripts follow their weapon");
	Check(SkinTypeForPath("Sounds/Weapons/Grenade/Throw.opus") == SkinType::Grenade,
	      "sounds follow their weapon");
	Check(SkinTypeForPath("Gfx/Hotbar/Spade.png") == SkinType::Spade,
	      "hotbar icons follow their weapon");
	Check(SkinTypeForPath("Gfx/Killfeed/a-Shotgun.png") == SkinType::Shotgun,
	      "killfeed icons follow their weapon");
	Check(SkinTypeForPath("Gfx/Killfeed/b-SMG.png") == SkinType::SMG &&
	        SkinTypeForPath("Gfx/Killfeed/c-Shotgun.png.weak") == SkinType::Shotgun &&
	        SkinTypeForPath("Gfx/Killfeed/e-Melee.png") == SkinType::Spade &&
	        SkinTypeForPath("Gfx/Killfeed/f-Grenade.png") == SkinType::Grenade,
	      "stock killfeed paths stay with their weapon choices");
	Check(SkinTypeForPath("Gfx/ScopeArc.png") == SkinType::Rifle,
	      "rifle scope does not leak to another skin");
	for (const char* path : {"Fonts/Gui.ttf", "Gfx/Fonts/OpenSans.ttf.weak",
	                         "Gfx/Fonts/100-unifont.ospfont", "Gfx/Fonts/100-unifont.png"})
		Check(SkinTypeForPath(path) == SkinType::Font, "faces and fallback atlases belong to Font");
	for (const char* path : {"Sounds/Feedback/Killstreak/Killsound01.opus",
	                         "Sounds/Player/Footstep.wav", "Sounds/Impacts/Block.opus.weak"})
		Check(SkinTypeForPath(path) == SkinType::SFX, "shared sound effects belong to SFX");
	for (const char* path : {"Models/Player/Arm.kv6", "Models/Weapons/Rifles/Weapon.kv6",
	                         "FontsExtra/Gui.ttf", "Scripts/Skin/Skin.as"})
		Check(!SkinTypeForPath(path), "unrelated paths stay unrelated");

	const std::vector<SkinMod> enabled = {
	  Mod("Font", {SkinType::Font}),     Mod("Rifle A", {SkinType::Rifle}),
	  Mod("Spade A", {SkinType::Spade}), Mod("Rifle B", {SkinType::Rifle}),
	  Mod("SMG A", {SkinType::SMG}),     Mod("Shotgun A", {SkinType::Shotgun})};
	SkinSelection selection;
	auto equipped = selection.Resolve(enabled);
	Check(equipped[Index(SkinType::Rifle)] == "Rifle B", "old installs retain last-enabled-wins");
	Check(equipped[Index(SkinType::Grenade)].empty(), "no enabled skin means Default");
	Check(selection.Resolve({}) == EquippedSkins{}, "an empty collection has seven defaults");
	selection.Set(SkinType::Rifle, "Rifle A");
	equipped = selection.Resolve(enabled);
	Check(equipped[Index(SkinType::Rifle)] == "Rifle A", "explicit choice overrides apply order");
	Check(equipped[Index(SkinType::SMG)] == "SMG A", "rifle choice leaves the SMG alone");
	Check(equipped[Index(SkinType::Spade)] == "Spade A", "rifle choice leaves the spade alone");
	Check(enabled.size() == 6 && enabled[3].name == "Rifle B",
	      "equipping does not remove/reorder the collection");
	Check(SkinFileIsActive(enabled[1], equipped, "Models/Weapons/Rifle/Weapon.kv6"),
	      "chosen model is exposed");
	Check(!SkinFileIsActive(enabled[3], equipped, "Scripts/Skin/Rifle/View.as"),
	      "other enabled skins cannot override it");
	Check(!SkinFileIsActive(enabled[3], equipped, "Gfx/CustomReticle.png"),
	      "unused skin dependencies stay hidden");
	Check(SkinFileIsActive(enabled[0], equipped, "Fonts/Gui.ttf"),
	      "font selection remains independent of the weapon");

	selection.Set(SkinType::Rifle, "");
	equipped = selection.Resolve(enabled);
	Check(equipped[Index(SkinType::Rifle)].empty(),
	      "explicit Default does not mean the last enabled mod");
	Check(!SkinFileIsActive(enabled[1], equipped, "Models/Weapons/Rifle/Weapon.kv6.weak"),
	      "Default suppresses weak overrides too");
	selection.Set(SkinType::Rifle, "removed skin");
	Check(selection.Resolve(enabled)[Index(SkinType::Rifle)] == "Rifle B",
	      "a stale/disabled choice safely falls back");
	Check(selection.Resolve({})[Index(SkinType::Rifle)].empty(),
	      "a stale choice cannot enable a missing skin");

	const auto combined =
	  Mod("Combined pack", {SkinType::Rifle, SkinType::SMG, SkinType::Font, SkinType::SFX});
	const std::vector<SkinMod> mixed = {combined, Mod("Other SMG", {SkinType::SMG})};
	SkinSelection mixedSelection;
	mixedSelection.Set(SkinType::Rifle, "Combined pack");
	mixedSelection.Set(SkinType::SMG, "Other SMG");
	equipped = mixedSelection.Resolve(mixed);
	Check(SkinFileIsActive(combined, equipped, "Models/Weapons/Rifle/Weapon.kv6"),
	      "combined pack contributes the chosen rifle");
	Check(!SkinFileIsActive(combined, equipped, "Models/Weapons/SMG/Weapon.kv6"),
	      "combined pack cannot replace another weapon's choice");
	Check(SkinFileIsActive(combined, equipped, "Scripts/CustomUtility.as"),
	      "selected pack keeps its common dependencies");
	Check(!SkinFileIsActive(combined, equipped, "Models/Weapons/"),
	      "zip folder entries are not reloadable files");

	const std::vector<SkinMod> global = {combined, Mod("Font B", {SkinType::Font}),
	                                     Mod("SFX B", {SkinType::SFX})};
	mixedSelection.Set(SkinType::Font, "Font B");
	mixedSelection.Set(SkinType::SFX, "SFX B");
	equipped = mixedSelection.Resolve(global);
	Check(SkinFileIsActive(combined, equipped, "Models/Weapons/Rifle/Weapon.kv6") &&
	        !SkinFileIsActive(combined, equipped, "Gfx/Fonts/OpenSans.ttf") &&
	        !SkinFileIsActive(combined, equipped, "Sounds/Feedback/HitFeedback.opus"),
	      "selecting a mixed pack's rifle cannot replace Font or SFX");
	mixedSelection.Set(SkinType::Font, "");
	mixedSelection.Set(SkinType::SFX, "");
	equipped = mixedSelection.Resolve(global);
	Check(equipped[Index(SkinType::Font)].empty() && equipped[Index(SkinType::SFX)].empty() &&
	        equipped[Index(SkinType::Rifle)] == "Combined pack",
	      "Font and SFX Defaults do not reset any weapon");
	Check(!SkinFileIsActive(global[1], equipped, "Gfx/Fonts/OpenSans.ttf.weak") &&
	        !SkinFileIsActive(global[2], equipped, "Sounds/Feedback/Killsound01.opus"),
	      "global Defaults suppress all candidate overrides, including weak files");

	SkinSelection saved;
	saved.Set(SkinType::Spade, "SPADE-Crowbar-Author.pak");
	saved.Set(SkinType::Grenade, "");
	saved.Set(SkinType::Rifle, "名前 with spaces.pak");
	saved.Set(SkinType::Font, "FONT-AlteDIN1451-PeterWiegel.pak");
	saved.Set(SkinType::SFX, "");
	const auto parsed = SkinSelection::Parse(saved.Serialize());
	Check(parsed.Serialize() == saved.Serialize(),
	      "preferences round-trip including UTF-8 and Default");
	Check(parsed.Get(SkinType::Grenade) && parsed.Get(SkinType::Grenade)->empty(),
	      "Default survives reload");
	Check(!parsed.Get(SkinType::SMG), "an absent choice remains distinct from Default");
	const auto messy = SkinSelection::Parse(
	  "unknown\tanything\r\ninvalid line\nRIFLE\tRifle A\r\nsmg\t\r\nrifle\tRifle B");
	Check(messy.Get(SkinType::Rifle) == std::optional<std::string>("Rifle B"),
	      "CRLF and duplicate preference lines");
	Check(messy.Get(SkinType::SMG) && messy.Get(SkinType::SMG)->empty(), "CRLF Default");
	Check(!messy.Get(SkinType::Spade), "malformed keys are ignored");

	std::printf("skin_selection_test: %s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
