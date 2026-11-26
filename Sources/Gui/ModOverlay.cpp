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
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <zlib.h>

#include "Main.h"
#include "ModOverlay.h"
#include "ModsScreenHelper.h"
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/DynamicMemoryStream.h>
#include <Core/FileManager.h>
#include <Core/IFileSystem.h>
#include <Core/IStream.h>
#include <Core/ResourceChanges.h>
#include <Core/TMPUtils.h>
#include <Core/ZipFileSystem.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	namespace gui {
		namespace {
			/**
			 * A mod pak as mounted: its files, less those of the slots that
			 * SkinChoices gives to another mod (or to the stock files).
			 */
			class ModPakFileSystem : public IFileSystem {
				ZipFileSystem zip;
				std::unordered_set<std::string> hidden; // lower case, '/'
				std::vector<std::string> files;          // the rest, as named in the pak

				static std::string Key(std::string s) {
					for (char& c : s)
						c = c == '\\' ? '/'
						              : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					return s;
				}
				bool Visible(const std::string& path) const {
					return hidden.empty() || hidden.count(Key(path)) == 0;
				}

			public:
				ModPakFileSystem(IStream* stream, const std::function<bool(int)>& slotVisible)
				    : zip(stream) {
					for (const std::string& file : zip.GetAllFiles()) {
						const int slot = SkinSlotOfPath(file);
						if (slot >= 0 && !slotVisible(slot))
							hidden.insert(Key(file));
						else
							files.push_back(file);
					}
				}
				const std::vector<std::string>& GetFiles() const { return files; }

				std::vector<std::string> EnumFiles(const char* path) override {
					std::vector<std::string> out = zip.EnumFiles(path);
					if (!hidden.empty()) {
						const std::string dir = std::string(path) + "/";
						out.erase(std::remove_if(out.begin(), out.end(),
						                         [&](const std::string& f) {
							                         return !Visible(dir + f);
						                         }),
						          out.end());
					}
					return out;
				}
				std::unique_ptr<IStream> OpenForReading(const char* path) override {
					if (!Visible(path))
						SPFileNotFound(path);
					return zip.OpenForReading(path);
				}
				std::unique_ptr<IStream> OpenForWriting(const char* path) override {
					return zip.OpenForWriting(path);
				}
				bool FileExists(const char* path) override {
					return Visible(path) && zip.FileExists(path);
				}
			};

			struct MountedPak {
				std::string id;                  // path and checksum: equal ids, equal files
				std::unique_ptr<IStream> stream; // the file system doesn't own it
				ModPakFileSystem* fs;            // owned by FileManager
			};

			std::vector<MountedPak> g_mountedPaks;
			std::vector<std::string> g_mountedMods;
			SkinChoices g_choices;        // what MountSet mounts
			SkinChoices g_mountedChoices; // what it mounted last

			// The id of the pak each mounted file is read from: the topmost one.
			std::unordered_map<std::string, std::string> GetWinners() {
				std::unordered_map<std::string, std::string> winners;
				for (const MountedPak& pak : g_mountedPaks) {
					for (const std::string& file : pak.fs->GetFiles())
						winners[file] = pak.id;
				}
				return winners;
			}

			// Mounts `mods` in place of what is mounted, the last one on top, and
			// over them the mods g_choices picks for a slot that are not enabled.
			// A slot with a choice is drawn only from the mod chosen (or, for
			// Default, from none).
			void MountSet(const std::vector<std::string>& mods) {
				for (MountedPak& pak : g_mountedPaks)
					FileManager::RemoveFileSystem(pak.fs);
				g_mountedPaks.clear();
				g_mountedMods = mods;

				// A choice of a mod that is gone is no choice
				SkinChoices choices = g_choices;
				{
					const auto installed = Handle<ModsScreenHelper>::New()->GetModNames();
					for (int i = 0; i < kNumSkinSlots; i++)
						if (choices.chosen[i] && !choices.mod[i].empty() &&
						    std::find(installed.begin(), installed.end(), choices.mod[i]) ==
						      installed.end())
							choices.chosen[i] = false;
				}
				g_mountedChoices = choices;
				std::vector<std::string> set = mods;
				for (int i = 0; i < kNumSkinSlots; i++)
					if (choices.chosen[i] && !choices.mod[i].empty() &&
					    std::find(set.begin(), set.end(), choices.mod[i]) == set.end())
						set.push_back(choices.mod[i]);

				std::vector<std::string> owners;
				const auto paths = ModsScreenHelper::GetModPakPaths(set, &owners);
				for (std::size_t p = 0; p < paths.size(); p++) {
					const std::string& path = paths[p];
					const std::string owner = owners[p];
					const bool enabled = std::find(mods.begin(), mods.end(), owner) != mods.end();
					// An enabled mod: every slot not chosen for another. One that is
					// only chosen: the slots chosen for it.
					auto slotVisible = [&](int slot) {
						if (choices.chosen[slot])
							return choices.mod[slot] == owner;
						return enabled;
					};
					try {
						// Read the whole pak into memory and mount from there, so the
						// file on disk is never held open. That lets the mod manager
						// overwrite or delete an enabled pak (for example re-downloading
						// it) while the game is running.
						std::string bytes = FileManager::ReadAllBytes(path.c_str());
						auto stream = stmp::make_unique<DynamicMemoryStream>();
						stream->Write(bytes.data(), bytes.size());
						stream->SetPosition(0);

						auto crc = crc32(0, reinterpret_cast<const Bytef*>(bytes.data()),
						                 static_cast<uInt>(bytes.size()));
						std::string id = path + "#" + std::to_string(crc);

						auto* fs = new ModPakFileSystem(stream.get(), slotVisible);
						FileManager::PrependFileSystem(fs);
						g_mountedPaks.push_back({std::move(id), std::move(stream), fs});
						SPLog("Mod pak mounted (in memory): %s", path.c_str());
					} catch (const std::exception& ex) {
						SPLog("Mod pak failed to mount: %s: %s", path.c_str(), ex.what());
					}
				}
			}
		} // namespace

		void ModOverlay::Mount() {
			SPADES_MARK_FUNCTION();

			// A --try-mod run applies the mod under test on top of the base paks only.
			if (g_tryMod)
				return;

			g_choices = SkinChoices::Load();
			MountSet(Handle<ModsScreenHelper>::New()->GetEnabledMods());
		}

		SkinChoices ModOverlay::GetMountedChoices() { return g_mountedChoices; }

		std::unique_ptr<IStream> ModOverlay::OpenStock(const char* path) {
			return FileManager::OpenForReadingSkipping(path, [](IFileSystem* fs) {
				for (const MountedPak& pak : g_mountedPaks)
					if (pak.fs == fs)
						return true;
				return false;
			});
		}

		ModApplyResult ModOverlay::ApplySkinChoices(const SkinChoices& next,
		                                            client::IRenderer& renderer,
		                                            client::IAudioDevice& audio,
		                                            std::string& error) {
			SPADES_MARK_FUNCTION();
			if (g_tryMod) {
				error = "Skins can't be picked while a mod is tried out (--try-mod).";
				return ModApplyResult::Refused;
			}
			const SkinChoices previous = SkinChoices::Load();
			if (!next.Save()) {
				error = "SkinChoices.txt could not be written.";
				return ModApplyResult::Refused;
			}
			const ModApplyResult result = Apply(renderer, audio, error);
			if (result == ModApplyResult::Refused)
				previous.Save();
			return result;
		}

		ModApplyResult ModOverlay::Apply(client::IRenderer& renderer, client::IAudioDevice& audio,
		                                 std::string& error) {
			SPADES_MARK_FUNCTION();

			// A file changed if it is now read from another pak, or from none.
			std::vector<std::string> previous = g_mountedMods;
			const SkinChoices previousChoices = g_mountedChoices;
			// Back to what was mounted before, the choices too
			auto mountPrevious = [&]() {
				g_choices = previousChoices;
				MountSet(previous);
			};
			auto before = GetWinners();
			Mount();

			auto changesSince = [&before]() {
				auto after = GetWinners();
				ResourceChanges changes;
				for (const auto& file : before) {
					auto it = after.find(file.first);
					if (it == after.end() || it->second != file.second)
						changes.Add(file.first);
				}
				for (const auto& file : after) {
					if (before.find(file.first) == before.end())
						changes.Add(file.first);
				}
				return changes;
			};
			ResourceChanges changes = changesSince();

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
							MountSet(without);
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
							MountSet(set);
							error = scripts.CheckScripts();
						}
					}
					if (!error.empty()) {
						// Not even without any mod: nothing a mod can mend.
						SPLog("Mods not applied: %s", error.c_str());
						Handle<ModsScreenHelper>::New()->SetEnabledMods(previous);
						mountPrevious();
						return ModApplyResult::Refused;
					}
					std::string names;
					for (const std::string& m : off)
						names += (names.empty() ? "" : ", ") + m;
					SPLog("Mods turned off, their scripts don't build: %s: %s", names.c_str(),
					      firstError.c_str());
					Handle<ModsScreenHelper>::New()->SetEnabledMods(set);
					MountSet(set);
					changes = changesSince();
					notice = names + "\n\n" + firstError;
				}
			}

			bool live = changes.GetReloadTime() == ResourceChanges::ReloadTime::Live;
			if (live && changes.ContainsUnder("Scripts")) {
				error = ScriptManager::GetInstance()->ReloadScripts();
				if (!error.empty()) {
					SPLog("Mods not applied: %s", error.c_str());
					Handle<ModsScreenHelper>::New()->SetEnabledMods(previous);
					mountPrevious();
					return ModApplyResult::Refused;
				}
			}
			// What was turned off, for the menu to say (any result but Refused).
			error = notice;

			// What is loaded was built from the previous set, so keep that mounted
			// until the restart; the enabled set on disk is the new one.
			if (!live) {
				mountPrevious();
				return ModApplyResult::NeedsRestart;
			}

			// The players' skins are built again from the new scripts by the game.
			renderer.ReloadResources(changes);
			audio.ReloadResources(changes);
			SPLog("Mods applied in place");
			return ModApplyResult::Applied;
		}
	} // namespace gui
} // namespace spades
