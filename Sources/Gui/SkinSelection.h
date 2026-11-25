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

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace spades {
	namespace gui {
		// Player/Intel/Tent remain unsupported. The last two entries are global
		// mod categories, independent of each of the five weapon choices.
		enum class SkinType : std::size_t { Spade, Grenade, Rifle, SMG, Shotgun, Font, SFX, Count };
		constexpr std::size_t kWeaponSkinTypeCount = static_cast<std::size_t>(SkinType::Font);
		constexpr std::size_t kSkinTypeCount = static_cast<std::size_t>(SkinType::Count);
		constexpr bool IsWeaponSkin(SkinType type) {
			return static_cast<std::size_t>(type) < kWeaponSkinTypeCount;
		}
		using EquippedSkins = std::array<std::string, kSkinTypeCount>;

		struct SkinMod {
			std::string name;
			std::array<bool, kSkinTypeCount> types{};

			bool HasSkins() const;
			bool HasType(SkinType type) const { return types[static_cast<std::size_t>(type)]; }
		};

		const char* SkinTypeKey(SkinType type);
		const char* SkinModelDirectory(SkinType type);
		const char* SkinPreviewPath(SkinType type);
		std::optional<SkinType> SkinTypeForPath(const std::string& path);
		std::optional<SkinType> SkinTypeForModName(const std::string& name);

		/**
		 * The equipped skins, NOT the main menu's enabled collection. A missing
		 * preference preserves the old last-enabled-wins behaviour; an empty
		 * preference explicitly equips Default. A disabled/deleted choice falls
		 * back to the last available skin without enabling anything by itself.
		 *
		 * Kept independent of filesystems/renderers so resolution and filtering
		 * are identical at startup, during live switching and in tests.
		 */
		class SkinSelection {
			std::array<std::optional<std::string>, kSkinTypeCount> choices;

		public:
			void Set(SkinType type, const std::string& modName);
			const std::optional<std::string>& Get(SkinType type) const;
			EquippedSkins Resolve(const std::vector<SkinMod>& enabled) const;

			// SelectedSkins.txt: type<TAB>mod name per line. An empty name means
			// Default; an absent line means no preference (including old installs).
			static SkinSelection Parse(const std::string& text);
			std::string Serialize() const;
		};

		/** Whether a mod contributes this file with the given loadout. */
		bool SkinFileIsActive(const SkinMod& mod, const EquippedSkins& equipped,
		                      const std::string& path);
	} // namespace gui
} // namespace spades
