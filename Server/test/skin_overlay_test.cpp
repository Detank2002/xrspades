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
}

namespace {
	constexpr const char* kRifle = "Models/Weapons/Rifle/Weapon.kv6";
	constexpr const char* kSMG = "Models/Weapons/SMG/Weapon.kv6";
	constexpr const char* kScript = "Scripts/Skin/Rifle/View.as";
	void Check(bool ok, const char* what) {
		if (!ok)
			throw std::runtime_error(what);
	}
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
		Write(kScript, "stock script");
		Pak("SEMI-A-.pak", {{kRifle, Model(0x0000FF)}, {kScript, "A script"}});
		Pak("SEMI-B-.pak", {{kRifle, Model(0xFF0000)}, {kScript, "B script"}});
		Pak("SEMI-Broken-.pak", {{kRifle, Model(0x00FF00)}, {kScript, "BROKEN"}});
		Pak("SEMI-Restart-.pak", {{kRifle, Model(0xFFFF00)},
		                          {kScript, "restart script"},
		                          {"Shaders/custom.vs", "restart"}});
		Pak("SMG-C-.pak", {{kSMG, Model(0x00FFFF)}, {"Sounds/Weapons/SMG/Fire.opus", "SMG sound"}});
		Pak("FONT-Gui-.pak", {{"Fonts/Gui.ttf", "Font unchanged"}});
		Pak("Combined.pak",
		    {{kRifle, Model(0xFF00FF)}, {kSMG, Model(0x00FF00)}, {kScript, "Combined"}});
		Pak("Folder/01.pak", {{kRifle, Model(0xAAAAAA)}});
		Pak("Folder/02.pak", {{kRifle, Model(0xBBBBBB)}});
		Pak("SEMI-Corrupt-.pak", {{kRifle, "not a KV6"}});
		Write("Mods/SEMI-Unreadable-.pak", "not a ZIP");
		Pak("Partial/01.pak", {{kRifle, Model(0xFFFFFF)}});
		Write("Mods/Partial/02.pak", "not a ZIP");
		Pak("Renamed.pak", {{"Models/Weapons/Rifle/Custom.kv6.weak", Model(0x123456)}});
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

		TestRenderer renderer;
		TestAudio audio;
		std::string error;
		auto helper = Handle<ModsScreenHelper>::New();
		const std::vector<std::string> enabled = {"FONT-Gui-.pak",     "SEMI-Broken-.pak",
		                                          "SEMI-Restart-.pak", "SEMI-A-.pak",
		                                          "SEMI-B-.pak",       "SMG-C-.pak"};
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

		extended.insert(extended.end(), {"Folder", "SEMI-Corrupt-.pak", "SEMI-Unreadable-.pak",
		                                 "Missing.pak", "Partial", "Renamed.pak"});
		catalog = Handle<SkinCatalog>::New(*helper, extended);
		Check((catalog->LoadPreview(SkinType::Rifle, "Folder")->GetColor(0, 0, 0) & 0xFFFFFF) ==
		        0xBBBBBB,
		      "folder-mod preview uses its last pak");
		Check((catalog->LoadPreview(SkinType::Rifle, "Renamed.pak")->GetColor(0, 0, 0) &
		       0xFFFFFF) == 0x123456,
		      "renamed weak model is previewed");
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
		std::printf("skin_overlay_test: all passed\n");
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "FAIL: %s\n", ex.what());
		result = 1;
	}
	FileManager::Close();
	std::filesystem::remove_all(directory);
	return result;
}
