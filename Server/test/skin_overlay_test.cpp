// Real archives and the real overlay/catalog, with only the application,
// diagnostic, GPU/audio and script runtimes replaced. No download or game needed.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

#include <zlib.h>

#include "skin_test_devices.h"
#include <Core/DynamicMemoryStream.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/MemoryStream.h>
#include <Core/VoxelModel.h>
#include <Core/ZipFileSystem.h>
#include <Gui/Main.h>
#include <Gui/ModOverlay.h>
#include <Gui/ModsScreenHelper.h>
#include <Gui/SkinCatalog.h>
#include <ScriptBindings/ScriptManager.h>

using namespace spades;
using namespace spades::gui;
void MountSkinTestDisk(const std::string& path);
namespace spades {
	extern bool alwaysBad;
	extern int fontReloads;
} // namespace spades

namespace {
	constexpr const char* kRifle = "Models/Weapons/Rifle/Weapon.kv6";
	constexpr const char* kSMG = "Models/Weapons/SMG/Weapon.kv6";
	constexpr const char* kScript = "Scripts/Skin/Rifle/View.as";
	void Check(bool ok, const char* what) {
		if (!ok)
			throw std::runtime_error(what);
	}
	class FailingZipStream : public MemoryStream {
	public:
		using MemoryStream::MemoryStream;
		bool fail = false;
		int failures = 0;
		std::size_t Read(void* out, std::size_t size) override {
			// Fail on the first stored entry's payload, after ZIP/local headers
			// have succeeded and an internal streaming reader has been created.
			if (fail && GetPosition() == 30 + std::string(kRifle).size()) {
				++failures;
				throw std::runtime_error("injected ZIP backing-stream failure");
			}
			return MemoryStream::Read(out, size);
		}
	};
	void Write(const std::string& relative, const std::string& bytes) {
		const auto path = std::filesystem::path(g_userResourceDirectory) / relative;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		Check(file.good(), "write test fixture");
	}
	std::string Read(const char* path) {
		return FileManager::ReadAllBytes(path);
	}

	std::string Model(std::uint32_t color) {
		auto voxels = Handle<VoxelModel>::New(1, 1, 1);
		voxels->SetSolid(0, 0, 0, color);
		DynamicMemoryStream stream;
		voxels->SaveKV6(stream);
		stream.SetPosition(0);
		return stream.ReadAllBytes();
	}
	std::string SceneScript(const std::string& body, bool thirdPerson = false,
	                        const std::string& magazine = {}) {
		return "namespace spades { class TestSkin : IWeaponSkin { Renderer@ r; Model@ body, mag; "
		       "TestSkin(Renderer@ renderer, AudioDevice@ dev) { @r = renderer; "
		       "@body = r.RegisterModel(\"" +
		       body + "\"); " +
		       (magazine.empty() ? "" : "@mag = r.RegisterModel(\"" + magazine + "\"); ") +
		       "} void AddToScene() { ModelRenderParam p; p.matrix = CreateTranslateMatrix(1,2,3); "
		       "p.customColor = Vector3(.2F,.4F,.8F); r.AddModel(body, p); " +
		       (magazine.empty() ? ""
		                         : "p.matrix *= CreateScaleMatrix(.5F); r.AddModel(mag, p); ") +
		       "} } IWeaponSkin@ Create" + (thirdPerson ? "ThirdPerson" : "View") +
		       "RifleSkin(Renderer@ r, AudioDevice@ dev) { return TestSkin(r, dev); } }";
	}
	void U16(std::string& out, std::uint32_t value) {
		out.push_back(static_cast<char>(value));
		out.push_back(static_cast<char>(value >> 8));
	}
	void U32(std::string& out, std::uint32_t value) {
		U16(out, value);
		U16(out, value >> 16);
	}
	// Small stored ZIP writer for self-contained fixtures, not a production packer.
	void Pak(const std::string& name, const std::map<std::string, std::string>& files) {
		std::string local, central;
		for (const auto& file : files) {
			const auto offset = static_cast<std::uint32_t>(local.size());
			const auto size = static_cast<std::uint32_t>(file.second.size());
			const auto crc = static_cast<std::uint32_t>(
			  crc32(0, reinterpret_cast<const Bytef*>(file.second.data()), size));
			U32(local, 0x04034b50);
			U16(local, 20);
			for (int i = 0; i < 4; ++i)
				U16(local, 0); // flags, method, time, date
			U32(local, crc);
			U32(local, size);
			U32(local, size);
			U16(local, static_cast<std::uint32_t>(file.first.size()));
			U16(local, 0);
			local += file.first;
			local += file.second;
			U32(central, 0x02014b50);
			U16(central, 20);
			U16(central, 20);
			for (int i = 0; i < 4; ++i)
				U16(central, 0);
			U32(central, crc);
			U32(central, size);
			U32(central, size);
			U16(central, static_cast<std::uint32_t>(file.first.size()));
			for (int i = 0; i < 4; ++i)
				U16(central, 0); // extra, comment, disk, attributes
			U32(central, 0);
			U32(central, offset);
			central += file.first;
		}
		std::string end;
		U32(end, 0x06054b50);
		U16(end, 0);
		U16(end, 0);
		U16(end, static_cast<std::uint32_t>(files.size()));
		U16(end, static_cast<std::uint32_t>(files.size()));
		U32(end, static_cast<std::uint32_t>(central.size()));
		U32(end, static_cast<std::uint32_t>(local.size()));
		U16(end, 0);
		Write("Mods/" + name, local + central + end);
	}
} // namespace

int main() {
	const auto directory =
	  std::filesystem::temp_directory_path() /
	  ("xrspades-skins-" +
	   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	g_userResourceDirectory = directory.string();
	int result = 0;
	try {
		Write(std::string(kRifle) + ".weak", Model(0x777777));
		Write(std::string(kSMG) + ".weak", Model(0x888888));
		Write("Scripts/Base/Utils.as", "");
		Write("Scripts/Skin/BasicViewWeapon.as", "");
		Write("Scripts/Skin/BasicViewTool.as", "");
		Write("Models/Weapons/Rifle/FirstPerson.kv6", Model(0xAAAAAA));
		Write(kScript, SceneScript("Models/Weapons/Rifle/FirstPerson.kv6"));
		Write("Scripts/Skin/Rifle/ThirdPerson.as", SceneScript(kRifle, true));
		Write("Fonts/Gui.ttf", "Stock font");
		Write("Gfx/Fonts/OpenSans.ttf", "Stock Open Sans");
		Write("Sounds/Feedback/HitFeedback.opus", "Stock hit");
		Pak("SEMI-A-.pak", {{kRifle, Model(0x0000FF)}, {kScript, "A script"}});
		Pak("SEMI-B-.pak", {{kRifle, Model(0xFF0000)}, {kScript, "B script"}});
		Pak("SEMI-Broken-.pak", {{kRifle, Model(0x00FF00)}, {kScript, "BROKEN"}});
		Pak("SEMI-Restart-.pak", {{kRifle, Model(0xFFFF00)},
		                          {kScript, "restart script"},
		                          {"Shaders/custom.vs", "restart"}});
		Pak("SMG-C-.pak", {{kSMG, Model(0x00FFFF)}, {"Sounds/Weapons/SMG/Fire.opus", "SMG sound"}});
		Pak("FONT-Gui-.pak", {{"Fonts/Gui.ttf", "Font unchanged"}});
		Pak("FONT-Other-.pak", {{"Gfx/Fonts/OpenSans.ttf", "Other font"}});
		Pak("FONT-Broken-.pak", {{"Gfx/Fonts/OpenSans.ttf", "BROKEN FONT"}});
		Pak("SFX-A-.pak", {{"Sounds/Feedback/HitFeedback.opus", "A hit"}});
		Pak("SFX-B-.pak", {{"Sounds/Feedback/HitFeedback.opus", "B hit"},
		                   {"Sounds/Feedback/Killstreak/Killsound01.opus", "B kill"}});
		Pak("Combined.pak",
		    {{kRifle, Model(0xFF00FF)}, {kSMG, Model(0x00FF00)}, {kScript, "Combined"}});
		Pak("Folder/01.pak", {{kRifle, Model(0xAAAAAA)}});
		Pak("Folder/02.pak", {{kRifle, Model(0xBBBBBB)}});
		Pak("SEMI-Corrupt-.pak", {{kRifle, "not a KV6"}});
		Write("Mods/SEMI-Unreadable-.pak", "not a ZIP");
		Pak("Partial/01.pak", {{kRifle, Model(0xFFFFFF)}});
		Write("Mods/Partial/02.pak", "not a ZIP");
		Pak("Renamed.pak", {{"Models/Weapons/Rifle/Custom.kv6.weak", Model(0x123456)}});
		Pak("Scripted.pak",
		    {{kRifle, Model(0x333333)},
		     {"Models/Weapons/Rifle/Display.kv6", Model(0x654321)},
		     {"Scripts/Skin/Rifle/ThirdPerson.as",
		      "// RegisterModel(\"Models/Weapons/Rifle/Weapon.kv6\");\n"
		      "@model = renderer.RegisterModel\n(\"Models/Weapons/Rifle/Display.kv6\");"}});
		Pak("Assembled.pak", {{kRifle, Model(0x333333)},
		                      {"Models/Weapons/Rifle/Body.kv6", Model(0x003355)},
		                      {"Models/Weapons/Rifle/Magazine.kv6.weak", Model(0x557700)},
		                      {kScript, SceneScript("Models/Weapons/Rifle/Body.kv6", false,
		                                            "Models/Weapons/Rifle/Magazine.kv6")}});
		MountSkinTestDisk(g_userResourceDirectory);

		{
			auto stream = ModOverlay::OpenBaseResource("Mods/SEMI-A-.pak");
			ZipFileSystem zip(stream.get());
			bool oversizedRefused = false;
			try {
				zip.OpenForReading(kRifle, 32);
			} catch (const std::exception&) {
				oversizedRefused = true;
			}
			Check(oversizedRefused, "preview size limit checked before decompression");
			Check(zip.OpenForReading(kRifle)->ReadAllBytes() == Model(0x0000FF),
			      "refusing an oversized entry leaves the archive usable");
		}

		{
			const auto bytes = ModOverlay::OpenBaseResource("Mods/SEMI-A-.pak")->ReadAllBytes();
			FailingZipStream stream(bytes.data(), bytes.size());
			bool refused = false;
			{
				ZipFileSystem zip(&stream);
				stream.fail = true;
				try {
					zip.OpenForReading(kRifle);
				} catch (const std::exception&) {
					refused = true;
				}
				// The internal reader is still present after the I/O exception.
				// Destruction must not terminate on a second close error.
			}
			Check(refused && stream.failures == 1,
			      "failed ZIP reads unwind and destroy the archive without terminating");
		}

		TestRenderer renderer;
		TestAudio audio;
		std::string error;
		auto helper = Handle<ModsScreenHelper>::New();
		const std::vector<std::string> enabled = {
		  "FONT-Broken-.pak", "FONT-Other-.pak",   "FONT-Gui-.pak", "SFX-B-.pak",  "SFX-A-.pak",
		  "SEMI-Broken-.pak", "SEMI-Restart-.pak", "SEMI-A-.pak",   "SEMI-B-.pak", "SMG-C-.pak"};
		helper->SetEnabledMods(enabled);
		ModOverlay::Mount();
		Check(ModOverlay::GetEquippedSkin(SkinType::Rifle) == "SEMI-B-.pak",
		      "last enabled rifle at startup");
		const auto initial = Read(kRifle), smg = Read(kSMG);
		auto catalog = Handle<SkinCatalog>::New(*helper, enabled);
		Check(catalog->GetMods().size() == enabled.size(),
		      "catalog contains enabled installed mods");
		Check(catalog->LoadPreview(SkinType::Rifle, "")->GetColor(0, 0, 0) == 0x777777,
		      "Default preview bypasses the equipped skin, also with .weak");
		Check((catalog->LoadPreview(SkinType::Rifle, "SEMI-A-.pak")->GetColor(0, 0, 0) &
		       0xFFFFFF) == 0x0000FF,
		      "A preview reads A, not equipped B");
		Check(Read(kRifle) == initial, "browsing never changes the live overlay");

		Check(ModOverlay::ApplySkin(SkinType::Rifle, "SEMI-A-.pak", renderer, audio, error) ==
		          ModApplyResult::Applied &&
		        error.empty(),
		      "equip A immediately");
		Check(ModOverlay::GetEquippedSkin(SkinType::Rifle) == "SEMI-A-.pak" &&
		        Read(kRifle) != initial,
		      "selection and live resource update together");
		Check(Read(kSMG) == smg && Read("Fonts/Gui.ttf") == "Font unchanged",
		      "other weapons and fonts are unchanged");
		Check(renderer.lastChanges.Contains(kRifle) && !renderer.lastChanges.Contains(kSMG),
		      "targeted resource reload");
		Check(helper->GetEnabledMods() == enabled, "equipping never edits the collection");
		const auto a = Read(kRifle), preference = Read("SelectedSkins.txt");
		Check(ModOverlay::ApplySkin(SkinType::SMG, "SEMI-A-.pak", renderer, audio, error) ==
		        ModApplyResult::Refused,
		      "wrong-category selection refused");
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "Combined.pak", renderer, audio, error) ==
		        ModApplyResult::Refused,
		      "installed but disabled skin refused");
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "SEMI-Broken-.pak", renderer, audio, error) ==
		          ModApplyResult::Refused &&
		        !error.empty(),
		      "broken scripts reported");
		Check(ModOverlay::GetEquippedSkin(SkinType::Rifle) == "SEMI-A-.pak" && Read(kRifle) == a,
		      "failed switch rolls resources and highlight back");
		Check(Read("SelectedSkins.txt") == preference && helper->GetEnabledMods() == enabled,
		      "failed click changes neither preference nor collection");
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "SEMI-Restart-.pak", renderer, audio, error) ==
		        ModApplyResult::NeedsRestart,
		      "restart-only change refused during a game");
		Check(ModOverlay::GetEquippedSkin(SkinType::Rifle) == "SEMI-A-.pak" &&
		        Read("SelectedSkins.txt") == preference,
		      "restart-only choice not highlighted or saved");

		// The old in-memory pak must survive even if its installed copy is
		// replaced or deleted before a failing reload.
		std::filesystem::rename(directory / "Mods/SEMI-A-.pak", directory / "A.backup");
		const auto generation = ScriptManager::GetInstance()->GetGeneration();
		renderer.failNextReload = true;
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "SEMI-B-.pak", renderer, audio, error) ==
		        ModApplyResult::Refused,
		      "backend exception reported");
		Check(ModOverlay::GetEquippedSkin(SkinType::Rifle) == "SEMI-A-.pak" && Read(kRifle) == a &&
		        Read("SelectedSkins.txt") == preference,
		      "backend exception restores previous overlay without persisting or disk rereads");
		Check(Read(kScript) == "A script" &&
		        ScriptManager::GetInstance()->GetGeneration() > generation,
		      "rollback rebuilds the previous script module from retained archives");
		std::filesystem::rename(directory / "A.backup", directory / "Mods/SEMI-A-.pak");

		Check(ModOverlay::ApplySkin(SkinType::Rifle, "", renderer, audio, error) ==
		        ModApplyResult::Applied,
		      "equip Default");
		Check(Read(kRifle) == ModOverlay::OpenBaseResource(kRifle)->ReadAllBytes(),
		      "Default restores stock resources");
		ModOverlay::Mount();
		Check(ModOverlay::GetEquippedSkin(SkinType::Rifle).empty(),
		      "Default persists across startup");
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "SEMI-A-.pak", renderer, audio, error) ==
		        ModApplyResult::Applied,
		      "re-equip A");

		auto extended = enabled;
		extended.push_back("Combined.pak");
		helper->SetEnabledMods(extended);
		Check(ModOverlay::ApplySkin(SkinType::SMG, "SMG-C-.pak", renderer, audio, error) ==
		        ModApplyResult::Applied,
		      "keep the other weapon explicit");
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "Combined.pak", renderer, audio, error) ==
		        ModApplyResult::Applied,
		      "equip rifle from multi-weapon pack");
		Check(Read(kSMG) == smg && helper->GetEnabledMods() == extended,
		      "combined pack cannot replace another equipped weapon or edit the collection");
		ModOverlay::Mount();
		Check(ModOverlay::GetEquippedSkin(SkinType::Rifle) == "Combined.pak",
		      "named selection persists across startup");
		const auto combined = Read(kRifle), saved = Read("SelectedSkins.txt");

		alwaysBad = true;
		Check(ModOverlay::Apply(renderer, audio, error) == ModApplyResult::Applied,
		      "unchanged main-menu apply requires no script rebuild");
		auto reduced = extended;
		reduced.erase(std::remove(reduced.begin(), reduced.end(), "Combined.pak"), reduced.end());
		helper->SetEnabledMods(reduced);
		Check(ModOverlay::Apply(renderer, audio, error) == ModApplyResult::Refused,
		      "unrepairable main-menu apply refused");
		Check(helper->GetEnabledMods() == extended &&
		        ModOverlay::GetEquippedSkin(SkinType::Rifle) == "Combined.pak",
		      "main-menu rollback restores the entire collection, not just active skins");
		alwaysBad = false;
		g_tryMod = true;
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "", renderer, audio, error) ==
		        ModApplyResult::Refused,
		      "try-mod remains isolated");
		g_tryMod = false;

		extended.insert(extended.end(),
		                {"Folder", "SEMI-Corrupt-.pak", "SEMI-Unreadable-.pak", "Missing.pak",
		                 "Partial", "Renamed.pak", "Scripted.pak", "Assembled.pak"});
		catalog = Handle<SkinCatalog>::New(*helper, extended);
		Check((catalog->LoadPreview(SkinType::Rifle, "Folder")->GetColor(0, 0, 0) & 0xFFFFFF) ==
		        0xBBBBBB,
		      "folder-mod preview uses its last pak");
		Check((catalog->LoadPreview(SkinType::Rifle, "Renamed.pak")->GetColor(0, 0, 0) &
		       0xFFFFFF) == 0x123456,
		      "renamed weak model is previewed");
		Check((catalog->LoadPreview(SkinType::Rifle, "Scripted.pak")->GetColor(0, 0, 0) &
		       0xFFFFFF) == 0x654321,
		      "third-person display body is preferred over a placeholder canonical model");
		const auto assembled = catalog->LoadPreviewScene(SkinType::Rifle, "Assembled.pak");
		Check(assembled.scripted && assembled.diagnostic.empty() && assembled.parts.size() == 2,
		      "catalog captures the complete private view scene, not a placeholder body");
		Check((assembled.parts[0].model->GetColor(0, 0, 0) & 0xFFFFFF) == 0x003355 &&
		        (assembled.parts[1].model->GetColor(0, 0, 0) & 0xFFFFFF) == 0x557700 &&
		        assembled.parts[0].matrix.m[12] == 1 && assembled.parts[1].matrix.m[0] == 0.5F &&
		        assembled.parts[0].customColor.z == 0.8F,
		      "scene reads this candidate's normal/weak parts with per-draw matrices/colors");
		const auto bodyOnly = catalog->LoadPreviewScene(SkinType::Rifle, "Folder");
		Check(bodyOnly.scripted && bodyOnly.parts.size() == 1 &&
		        (bodyOnly.parts[0].model->GetColor(0, 0, 0) & 0xFFFFFF) == 0xBBBBBB,
		      "model-only body uses the third-person pose instead of an unrelated stock view");
		const auto renamed = catalog->LoadPreviewScene(SkinType::Rifle, "Renamed.pak");
		Check(!renamed.scripted && !renamed.diagnostic.empty() &&
		        (renamed.parts[0].model->GetColor(0, 0, 0) & 0xFFFFFF) == 0x123456,
		      "unsupported renamed model-only scenes use the bounded body fallback");
		const auto stock = catalog->LoadPreviewScene(SkinType::Rifle, "");
		Check(stock.scripted && (stock.parts[0].model->GetColor(0, 0, 0) & 0xFFFFFF) == 0xAAAAAA,
		      "Default capture reads base scripts/models below the equipped overlay");
		Check(catalog->GetMods().size() == extended.size() - 3,
		      "missing, unreadable and partially unreadable archives are excluded");
		bool corruptRefused = false;
		try {
			catalog->LoadPreview(SkinType::Rifle, "SEMI-Corrupt-.pak");
		} catch (const std::exception&) {
			corruptRefused = true;
		}
		Check(corruptRefused, "corrupt preview fails safely");
		Check(Read(kRifle) == combined && Read("SelectedSkins.txt") == saved,
		      "folder/corrupt previews do not affect the game");

		// An unwritable preference is not a false failure/highlight: the live
		// choice is usable, and the caller gets a session-only warning.
		std::filesystem::create_directory(directory / "SelectedSkins.txt.tmp");
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "SEMI-A-.pak", renderer, audio, error) ==
		          ModApplyResult::Applied &&
		        !error.empty(),
		      "save failure reports session-only selection");
		Check(ModOverlay::GetEquippedSkin(SkinType::Rifle) == "SEMI-A-.pak" &&
		        Read("SelectedSkins.txt") == saved,
		      "save failure keeps live choice but not a corrupt preference");
		std::filesystem::remove(directory / "SelectedSkins.txt.tmp");
		Check(ModOverlay::ApplySkin(SkinType::Rifle, "SEMI-A-.pak", renderer, audio, error) ==
		          ModApplyResult::Applied &&
		        error.empty(),
		      "same skin can be saved on retry");
		// Global choices use the same immediate apply/persist/rollback path,
		// without touching the weapon choices or the enabled collection.
		const auto collection = helper->GetEnabledMods();
		const auto rifleBeforeGlobals = Read(kRifle);
		const auto fontsBefore = fontReloads;
		Check(ModOverlay::ApplySkin(SkinType::Font, "FONT-Other-.pak", renderer, audio, error) ==
		          ModApplyResult::Applied &&
		        error.empty(),
		      "font selection applies without restart");
		Check(fontReloads == fontsBefore + 1 && Read("Gfx/Fonts/OpenSans.ttf") == "Other font" &&
		        Read("Fonts/Gui.ttf") == "Stock font",
		      "font refresh is requested on the new overlay");
		Check(catalog->LoadFontPreview("FONT-Other-.pak") ==
		          std::optional<std::string>("Other font") &&
		        catalog->LoadFontPreview("") == std::optional<std::string>("Stock Open Sans"),
		      "font previews read their own pack or stock bytes, never the equipped winner");
		const auto fontPreference = Read("SelectedSkins.txt");
		Check(ModOverlay::ApplySkin(SkinType::Font, "FONT-Broken-.pak", renderer, audio, error) ==
		          ModApplyResult::Refused &&
		        !error.empty(),
		      "font refresh failure is reported");
		Check(ModOverlay::GetEquippedSkin(SkinType::Font) == "FONT-Other-.pak" &&
		        Read("Gfx/Fonts/OpenSans.ttf") == "Other font" &&
		        Read("SelectedSkins.txt") == fontPreference,
		      "failed font restores resources, preference and selection");
		Check(ModOverlay::ApplySkin(SkinType::Font, "", renderer, audio, error) ==
		          ModApplyResult::Applied &&
		        Read("Gfx/Fonts/OpenSans.ttf") == "Stock Open Sans",
		      "font Default restores stock");
		Check(ModOverlay::ApplySkin(SkinType::Font, "FONT-Gui-.pak", renderer, audio, error) ==
		        ModApplyResult::Applied,
		      "custom Fonts directory is selectable too");
		ModOverlay::Mount();
		Check(ModOverlay::GetEquippedSkin(SkinType::Font) == "FONT-Gui-.pak",
		      "font preference persists across startup");

		constexpr const char* hit = "Sounds/Feedback/HitFeedback.opus";
		constexpr const char* kill = "Sounds/Feedback/Killstreak/Killsound01.opus";
		Check(ModOverlay::ApplySkin(SkinType::SFX, "SFX-B-.pak", renderer, audio, error) ==
		          ModApplyResult::Applied &&
		        Read(hit) == "B hit" && Read(kill) == "B kill",
		      "SFX changes immediately, including mod-only sounds");
		Check(audio.lastChanges.Contains(hit) && audio.lastChanges.Contains(kill) &&
		        !audio.lastChanges.Contains("Sounds/Weapons/SMG/Fire.opus"),
		      "audio reload targets shared SFX, not the selected weapon's sounds");
		Check(ModOverlay::ApplySkin(SkinType::SFX, "", renderer, audio, error) ==
		          ModApplyResult::Applied &&
		        Read(hit) == "Stock hit" && !FileManager::FileExists(kill) &&
		        audio.lastChanges.Contains(kill),
		      "SFX Default restores stock and notifies audio to silence removed optional chunks");
		ModOverlay::Mount();
		Check(ModOverlay::GetEquippedSkin(SkinType::SFX).empty() &&
		        ModOverlay::GetEquippedSkin(SkinType::Font) == "FONT-Gui-.pak" &&
		        Read(kRifle) == rifleBeforeGlobals && Read(kSMG) == smg &&
		        helper->GetEnabledMods() == collection,
		      "all seven categories persist independently");
		Check(ModOverlay::ApplySkin(SkinType::Font, "SFX-A-.pak", renderer, audio, error) ==
		        ModApplyResult::Refused,
		      "global wrong-category selection refused");

		ResourceChanges fontChanges;
		fontChanges.Add("Gfx/Fonts/OpenSans.ttf.weak");
		fontChanges.Add("Gfx/Fonts/100-unifont.ospfont");
		fontChanges.Add("Gfx/Fonts/100-unifont.png");
		Check(
		  fontChanges.GetReloadTime() == ResourceChanges::ReloadTime::Restart &&
		    fontChanges.GetReloadTime(ResourceChanges::FontReload::Live) ==
		      ResourceChanges::ReloadTime::Live,
		  "only the selector opts into font reload; home-screen restart behavior stays unchanged");
		fontChanges.Add("Shaders/custom.vs");
		Check(fontChanges.GetReloadTime(ResourceChanges::FontReload::Live) ==
		        ResourceChanges::ReloadTime::Restart,
		      "font support does not hide other restart-only changes");
		std::printf("skin_overlay_test: all passed\n");
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "FAIL: %s\n", ex.what());
		result = 1;
	}
	FileManager::Close();
	std::filesystem::remove_all(directory);
	return result;
}
