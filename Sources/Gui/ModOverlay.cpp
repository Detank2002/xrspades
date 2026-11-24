/*
 Copyright (c) 2026 ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <zlib.h>

#include "Main.h"
#include "ModOverlay.h"
#include "ModsScreenHelper.h"
#include "SkinCatalog.h"
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/DynamicMemoryStream.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/ResourceChanges.h>
#include <Core/TMPUtils.h>
#include <Core/ZipFileSystem.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	namespace gui {
		namespace {
			// A selected pack can contain more than one weapon. Filter by resource
			// path as well as by mod, so equipping its rifle cannot change the SMG.
			class SkinPakFileSystem : public ZipFileSystem {
				std::unordered_set<std::string> visible;
				bool active = true;

			public:
				SkinPakFileSystem(IStream* stream, const SkinMod& mod,
				                  const EquippedSkins& equipped)
				    : ZipFileSystem(stream) {
					for (const std::string& file : GetAllFiles())
						if (SkinFileIsActive(mod, equipped, file))
							visible.insert(file);
				}

				// A transaction keeps the previous in-memory archives mounted but
				// invisible until the new resources have successfully reloaded.
				void SetActive(bool value) { active = value; }

				bool FileExists(const char* path) override {
					if (!active)
						return false;
					std::string key = path;
					for (char& c : key)
						c = c == '\\'
						      ? '/'
						      : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					return visible.count(key) != 0;
				}

				std::vector<std::string> EnumFiles(const char* path) override {
					if (!active)
						return {};
					auto result = ZipFileSystem::EnumFiles(path);
					result.erase(std::remove_if(result.begin(), result.end(),
					                            [this, path](const std::string& file) {
						                            return !FileExists(
						                              (std::string(path) + "/" + file).c_str());
					                            }),
					             result.end());
					return result;
				}

				std::vector<std::string> GetVisibleFiles() const {
					std::vector<std::string> result;
					for (const auto& file : visible)
						result.push_back(file);
					return result;
				}
			};

			struct MountedPak {
				std::string id;                  // path and checksum: equal ids, equal files
				std::unique_ptr<IStream> stream; // the ZipFileSystem doesn't own it
				SkinPakFileSystem* fs;           // owned by FileManager
			};

			std::vector<MountedPak> g_mountedPaks;
			// This remains the WHOLE enabled collection, not just the equipped
			// subset. In particular, rollback must not untick the other skins.
			std::vector<std::string> g_mountedMods;
			SkinSelection g_mountedSelection;
			EquippedSkins g_equipped{};

			void Unmount(std::vector<MountedPak>& paks) {
				for (MountedPak& pak : paks)
					FileManager::RemoveFileSystem(pak.fs);
				paks.clear(); // release the streams only after their filesystems
			}

			// No disk I/O is needed to restore a failed switch, including when a
			// download replaced/deleted one of the previous archives. Hidden paks
			// cannot win either the strong or weak FileManager search.
			class MountTransaction {
				std::vector<MountedPak> paks;
				std::vector<std::string> mods;
				SkinSelection selection;
				EquippedSkins equipped;
				bool finished = false;

			public:
				MountTransaction()
				    : mods(g_mountedMods), selection(g_mountedSelection), equipped(g_equipped) {
					paks = std::move(g_mountedPaks);
					for (MountedPak& pak : paks)
						pak.fs->SetActive(false);
				}
				MountTransaction(const MountTransaction&) = delete;
				MountTransaction& operator=(const MountTransaction&) = delete;
				~MountTransaction() { Restore(); }

				void Restore() {
					if (finished)
						return;
					Unmount(g_mountedPaks);
					g_mountedPaks = std::move(paks);
					g_mountedMods = std::move(mods);
					g_mountedSelection = std::move(selection);
					g_equipped = std::move(equipped);
					for (MountedPak& pak : g_mountedPaks)
						pak.fs->SetActive(true);
					finished = true;
				}

				void Commit() {
					Unmount(paks);
					finished = true;
				}
			};

			SkinSelection ReadSelection() {
				if (g_userResourceDirectory.empty())
					return {};
				const std::string path = g_userResourceDirectory + "/SelectedSkins.txt";
				if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
					std::string text;
					char buf[4096];
					std::size_t n;
					while ((n = std::fread(buf, 1, sizeof(buf), f)) != 0)
						text.append(buf, n);
					std::fclose(f);
					return SkinSelection::Parse(text);
				}
				return {};
			}

			bool SaveSelection(const SkinSelection& selection) {
				if (g_userResourceDirectory.empty())
					return false;
				const std::string path = g_userResourceDirectory + "/SelectedSkins.txt";
				const std::string temp = path + ".tmp";
				std::FILE* f = std::fopen(temp.c_str(), "wb");
				if (!f)
					return false;
				const std::string text = selection.Serialize();
				bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
				if (std::fclose(f) != 0)
					ok = false;
				if (ok) {
#ifdef _WIN32
					ok = MoveFileExA(temp.c_str(), path.c_str(),
					                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
					ok = std::rename(temp.c_str(), path.c_str()) == 0;
#endif
				}
				if (!ok)
					std::remove(temp.c_str());
				return ok;
			}

			// The id of the pak each mounted file is read from: the topmost one.
			std::unordered_map<std::string, std::string> GetWinners() {
				std::unordered_map<std::string, std::string> winners;
				for (const MountedPak& pak : g_mountedPaks) {
					for (const std::string& file : pak.fs->GetVisibleFiles())
						winners[file] = pak.id;
				}
				return winners;
			}

			// Mounts the collection with its equipped skins, the last mod on top.
			bool MountSet(const std::vector<std::string>& mods, const SkinSelection& selection) {
				auto helper = Handle<ModsScreenHelper>::New();
				auto catalog = Handle<SkinCatalog>::New(*helper, mods);
				const auto equipped = selection.Resolve(catalog->GetMods());

				// Stage the paks before removing the live set. Reading a preview or
				// archive never requires exposing an unequipped mod to the game.
				struct StagedPak {
					std::string id;
					std::unique_ptr<IStream> stream;
					std::unique_ptr<SkinPakFileSystem> fs;
				};
				std::vector<StagedPak> staged;
				bool complete = true;
				for (const SkinMod& mod : catalog->GetMods()) {
					if (mod.HasSkins() &&
					    std::find(equipped.begin(), equipped.end(), mod.name) == equipped.end())
						continue;
					for (const std::string& path : catalog->GetPakPaths(mod.name)) {
						try {
							// Do not hold files on disk open: the main menu can replace
							// installed paks while the game is running.
							std::string bytes =
							  ModOverlay::OpenBaseResource(path.c_str())->ReadAllBytes();
							auto stream = stmp::make_unique<DynamicMemoryStream>();
							stream->Write(bytes.data(), bytes.size());
							stream->SetPosition(0);
							auto crc = crc32(0, reinterpret_cast<const Bytef*>(bytes.data()),
							                 static_cast<uInt>(bytes.size()));
							std::string id = path + "#" + std::to_string(crc);
							auto fs =
							  stmp::make_unique<SkinPakFileSystem>(stream.get(), mod, equipped);
							staged.push_back({std::move(id), std::move(stream), std::move(fs)});
							SPLog("Mod pak staged (in memory): %s", path.c_str());
						} catch (const std::exception& ex) {
							complete = false;
							SPLog("Mod pak failed to mount: %s: %s", path.c_str(), ex.what());
						}
					}
				}

				// Keep staging ownership until each filesystem is actually published.
				// On an exception, neither a raw filesystem nor its stream can leak.
				std::vector<MountedPak> mounted;
				mounted.reserve(staged.size());
				try {
					for (StagedPak& pak : staged) {
						FileManager::PrependFileSystem(pak.fs.get());
						mounted.push_back(
						  {std::move(pak.id), std::move(pak.stream), pak.fs.release()});
					}
				} catch (...) {
					Unmount(mounted);
					throw;
				}
				Unmount(g_mountedPaks);
				g_mountedPaks = std::move(mounted);
				g_mountedMods = mods;
				g_mountedSelection = selection;
				g_equipped = equipped;
				return complete;
			}

			ResourceChanges
			ChangesSince(const std::unordered_map<std::string, std::string>& before) {
				auto after = GetWinners();
				ResourceChanges changes;
				for (const auto& file : before) {
					auto it = after.find(file.first);
					if (it == after.end() || it->second != file.second)
						changes.Add(file.first);
				}
				for (const auto& file : after)
					if (before.find(file.first) == before.end())
						changes.Add(file.first);
				return changes;
			}

		} // namespace

		void ModOverlay::Mount() {
			SPADES_MARK_FUNCTION();

			// A --try-mod run applies the mod under test on top of the base paks only.
			if (g_tryMod)
				return;

			MountSet(Handle<ModsScreenHelper>::New()->GetEnabledMods(), ReadSelection());
		}

		ModApplyResult ModOverlay::Apply(client::IRenderer& renderer, client::IAudioDevice& audio,
		                                 std::string& error) {
			SPADES_MARK_FUNCTION();

			if (g_tryMod) {
				error.clear();
				return ModApplyResult::Applied;
			}

			// A file changed if it is now read from another pak, or from none.
			std::vector<std::string> previous = g_mountedMods;
			const SkinSelection selection = ReadSelection();
			auto before = GetWinners();
			MountTransaction previousMount;
			MountSet(Handle<ModsScreenHelper>::New()->GetEnabledMods(), selection);

			ResourceChanges changes = ChangesSince(before);

			// Build the new scripts first: ones that don't build would also stop
			// the next start. A mod whose scripts break the build is turned off,
			// and the rest applied, so a broken mod can always be unticked and
			// never holds the others back (a tester could not untick one). Each
			// mod is tried out of the set, the last enabled first; with none that
			// mends it alone, the last is dropped and the search goes on.
			std::string notice;
			if (changes.ContainsUnder("Scripts")) {
				ScriptManager& scripts = *ScriptManager::GetInstance();
				error = scripts.CheckScripts();
				if (!error.empty()) {
					const std::string firstError = error;
					std::vector<std::string> set = g_mountedMods, off;
					while (!error.empty() && !set.empty()) {
						bool found = false;
						for (std::size_t i = set.size(); i-- > 0;) {
							std::vector<std::string> without = set;
							without.erase(without.begin() + static_cast<long>(i));
							MountSet(without, selection);
							if (scripts.CheckScripts().empty()) {
								off.push_back(set[i]);
								set = without;
								error.clear();
								found = true;
								break;
							}
						}
						if (!found) {
							off.push_back(set.back());
							set.pop_back();
							MountSet(set, selection);
							error = scripts.CheckScripts();
						}
					}
					if (!error.empty()) {
						// Not even without any mod: nothing a mod can mend.
						SPLog("Mods not applied: %s", error.c_str());
						Handle<ModsScreenHelper>::New()->SetEnabledMods(previous);
						previousMount.Restore();
						return ModApplyResult::Refused;
					}
					std::string names;
					for (const std::string& m : off)
						names += (names.empty() ? "" : ", ") + m;
					SPLog("Mods turned off, their scripts don't build: %s: %s", names.c_str(),
					      firstError.c_str());
					Handle<ModsScreenHelper>::New()->SetEnabledMods(set);
					MountSet(set, selection);
					changes = ChangesSince(before);
					notice = names + "\n\n" + firstError;
				}
			}

			bool live = changes.GetReloadTime() == ResourceChanges::ReloadTime::Live;
			if (live && changes.ContainsUnder("Scripts")) {
				error = ScriptManager::GetInstance()->ReloadScripts();
				if (!error.empty()) {
					SPLog("Mods not applied: %s", error.c_str());
					Handle<ModsScreenHelper>::New()->SetEnabledMods(previous);
					previousMount.Restore();
					return ModApplyResult::Refused;
				}
			}
			// What was turned off, for the menu to say (any result but Refused).
			error = notice;

			// What is loaded was built from the previous set, so keep that mounted
			// until the restart; the enabled set on disk is the new one.
			if (!live) {
				previousMount.Restore();
				return ModApplyResult::NeedsRestart;
			}

			// The players' skins are built again from the new scripts by the game.
			renderer.ReloadResources(changes);
			audio.ReloadResources(changes);
			previousMount.Commit();
			SPLog("Mods applied in place");
			return ModApplyResult::Applied;
		}

		std::unique_ptr<IStream> ModOverlay::OpenBaseResource(const char* path) {
			std::vector<IFileSystem*> excluded;
			for (const MountedPak& pak : g_mountedPaks)
				excluded.push_back(pak.fs);
			return FileManager::OpenForReading(path, excluded);
		}

		std::string ModOverlay::GetEquippedSkin(SkinType type) {
			return g_equipped[static_cast<std::size_t>(type)];
		}

		ModApplyResult ModOverlay::ApplySkin(SkinType type, const std::string& modName,
		                                     client::IRenderer& renderer,
		                                     client::IAudioDevice& audio, std::string& error) {
			SPADES_MARK_FUNCTION();
			error.clear();
			if (g_tryMod) {
				error = "Skin selection is unavailable while testing a mod with --try-mod.";
				return ModApplyResult::Refused;
			}

			auto helper = Handle<ModsScreenHelper>::New();
			const auto enabled = helper->GetEnabledMods();
			auto catalog = Handle<SkinCatalog>::New(*helper, enabled);
			if (!modName.empty()) {
				bool available = false;
				for (const SkinMod& mod : catalog->GetMods())
					available = available || (mod.name == modName && mod.HasType(type));
				if (!available) {
					error = "This skin is no longer installed and enabled in the main menu.";
					return ModApplyResult::Refused;
				}
			}

			const auto previousSelection = g_mountedSelection;
			const auto before = GetWinners();
			auto selection = previousSelection;
			selection.Set(type, modName);
			MountTransaction previousMount;
			ResourceChanges changes;
			bool scriptsReloaded = false;
			bool resourcesTouched = false;
			auto rollback = [&] {
				previousMount.Restore();
				if (scriptsReloaded)
					ScriptManager::GetInstance()->ReloadScripts();
				if (resourcesTouched) {
					renderer.ReloadResources(changes);
					audio.ReloadResources(changes);
				}
			};
			try {
				const bool complete = MountSet(enabled, selection);
				changes = ChangesSince(before);
				if (!complete || GetEquippedSkin(type) != modName) {
					rollback();
					error = "Could not read the selected skin's pak files.";
					return ModApplyResult::Refused;
				}
				if (changes.GetReloadTime() != ResourceChanges::ReloadTime::Live) {
					rollback();
					error = "These resources need a restart. Apply pending mod changes from the "
					        "main menu first.";
					return ModApplyResult::NeedsRestart;
				}
				// Unlike Apply on the home screen, a click must not silently drop
				// other skins from the collection to make a broken skin build.
				if (changes.ContainsUnder("Scripts")) {
					error = ScriptManager::GetInstance()->ReloadScripts();
					if (!error.empty()) {
						rollback();
						return ModApplyResult::Refused;
					}
					scriptsReloaded = true;
				}
				resourcesTouched = true;
				renderer.ReloadResources(changes);
				audio.ReloadResources(changes);
			} catch (const std::exception& ex) {
				error = ex.what();
				try {
					rollback();
				} catch (const std::exception& restoreError) {
					SPLog("Skin resource restore failed: %s", restoreError.what());
					error += "\n";
					error += restoreError.what();
				}
				return ModApplyResult::Refused;
			}
			previousMount.Commit();
			if (!SaveSelection(selection))
				error =
				  "Could not save SelectedSkins.txt. This skin is equipped for this session only.";
			SPLog("Skin equipped: %s: %s", SkinTypeKey(type),
			      modName.empty() ? "Default" : modName.c_str());
			return ModApplyResult::Applied;
		}

	} // namespace gui
} // namespace spades
