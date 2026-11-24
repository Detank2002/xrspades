/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades. If not, see <http://www.gnu.org/licenses/>.
 */

#include "SkinSelection.h"

#include <algorithm>
#include <cctype>

namespace spades {
	namespace gui {
		namespace {
			const char* const kKeys[] = {"spade", "grenade", "rifle", "smg", "shotgun"};
			const char* const kDirectories[] = {"Models/Weapons/Spade/", "Models/Weapons/Grenade/",
			                                    "Models/Weapons/Rifle/", "Models/Weapons/SMG/",
			                                    "Models/Weapons/Shotgun/"};
			const char* const kModels[] = {
			  "Models/Weapons/Spade/Spade.kv6", "Models/Weapons/Grenade/Grenade.kv6",
			  "Models/Weapons/Rifle/Weapon.kv6", "Models/Weapons/SMG/Weapon.kv6",
			  "Models/Weapons/Shotgun/Weapon.kv6"};

			std::string Normalize(std::string path) {
				for (char& c : path)
					c = c == '\\' ? '/'
					              : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return path;
			}
		} // namespace

		bool SkinMod::HasSkins() const {
			return std::find(types.begin(), types.end(), true) != types.end();
		}

		const char* SkinTypeKey(SkinType type) {
			return kKeys[static_cast<std::size_t>(type)];
		}
		const char* SkinModelDirectory(SkinType type) {
			return kDirectories[static_cast<std::size_t>(type)];
		}
		const char* SkinPreviewPath(SkinType type) {
			return kModels[static_cast<std::size_t>(type)];
		}

		std::optional<SkinType> SkinTypeForPath(const std::string& path) {
			const std::string p = Normalize(path);
			for (std::size_t i = 0; i < kSkinTypeCount; ++i) {
				const std::string key = kKeys[i];
				for (const char* root : {"models/weapons/", "scripts/skin/", "sounds/weapons/"}) {
					const std::string prefix = std::string(root) + key + "/";
					if (p.compare(0, prefix.size(), prefix) == 0)
						return static_cast<SkinType>(i);
				}
				for (const std::string& icon :
				     {"gfx/hotbar/" + key + ".png", "gfx/killfeed/a-" + key + ".png"}) {
					if (p == icon || p == icon + ".weak")
						return static_cast<SkinType>(i);
				}
			}
			// These shared-looking files actually belong to the rifle scope.
			if (p == "gfx/scopearc.png" || p == "gfx/scopering.png" ||
			    p == "gfx/scopearc.png.weak" || p == "gfx/scopering.png.weak")
				return SkinType::Rifle;
			return std::nullopt;
		}

		std::optional<SkinType> SkinTypeForModName(const std::string& name) {
			const auto dash = name.find('-');
			if (dash == std::string::npos)
				return std::nullopt;
			const std::string tag = Normalize(name.substr(0, dash));
			if (tag == "semi")
				return SkinType::Rifle;
			for (std::size_t i = 0; i < kSkinTypeCount; ++i)
				if (tag == kKeys[i])
					return static_cast<SkinType>(i);
			return std::nullopt;
		}

		void SkinSelection::Set(SkinType type, const std::string& modName) {
			choices[static_cast<std::size_t>(type)] = modName;
		}

		const std::optional<std::string>& SkinSelection::Get(SkinType type) const {
			return choices[static_cast<std::size_t>(type)];
		}

		EquippedSkins SkinSelection::Resolve(const std::vector<SkinMod>& enabled) const {
			EquippedSkins result{};
			for (std::size_t i = 0; i < kSkinTypeCount; ++i) {
				const auto& choice = choices[i];
				if (choice && choice->empty())
					continue; // explicit Default, not the last mod
				for (const SkinMod& mod : enabled) {
					if (!mod.types[i])
						continue;
					result[i] = mod.name;
					if (choice && *choice == mod.name)
						break;
				}
			}
			return result;
		}

		SkinSelection SkinSelection::Parse(const std::string& text) {
			SkinSelection result;
			std::size_t start = 0;
			while (start < text.size()) {
				const auto end = text.find('\n', start);
				std::string line = text.substr(start, end == std::string::npos ? end : end - start);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				const auto tab = line.find('\t');
				if (tab != std::string::npos && line.find('\0') == std::string::npos) {
					const std::string key = Normalize(line.substr(0, tab));
					for (std::size_t i = 0; i < kSkinTypeCount; ++i)
						if (key == kKeys[i])
							result.choices[i] = line.substr(tab + 1);
				}
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
			return result;
		}

		std::string SkinSelection::Serialize() const {
			std::string result;
			for (std::size_t i = 0; i < kSkinTypeCount; ++i)
				if (choices[i])
					result += std::string(kKeys[i]) + "\t" + *choices[i] + "\n";
			return result;
		}

		bool SkinFileIsActive(const SkinMod& mod, const EquippedSkins& equipped,
		                      const std::string& path) {
			if (path.empty() || path.back() == '/' || path.back() == '\\')
				return false;
			if (auto type = SkinTypeForPath(path))
				return equipped[static_cast<std::size_t>(*type)] == mod.name;
			// Dependencies outside a weapon directory are kept only for mods
			// actually in use. Non-weapon mods retain their normal apply order.
			return !mod.HasSkins() ||
			       std::find(equipped.begin(), equipped.end(), mod.name) != equipped.end();
		}
	} // namespace gui
} // namespace spades
