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
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>
#include <vector>

namespace spades {
	namespace gui {
		/**
		 * The tools a skin can be picked for, one at a time (the in-game Skins
		 * page; after an idea and first version by Kyrope). A slot owns its
		 * tool's models, sounds and view script: Models/Weapons/<Dir>/,
		 * Sounds/Weapons/<Dir>/ and Scripts/Skin/<Dir>/.
		 */
		enum class SkinSlot { Spade, Block, Grenade, Rifle, SMG, Shotgun };
		constexpr int kNumSkinSlots = 6;

		/** "Spade", "Block", ... as the resource directories are named */
		const char* SkinSlotDirectory(SkinSlot);
		/** The slot a resource path belongs to; -1 for none (any case, / or \) */
		int SkinSlotOfPath(const std::string& path);

		/**
		 * Which mod's files each slot is drawn from, in SkinChoices.txt beside
		 * EnabledMods.txt. A slot with no choice takes the enabled mods as they
		 * stack (the last enabled wins); one with a choice takes only that
		 * mod's files for it, which need not be enabled, or the stock ones
		 * (Default, an empty name). A mixed pack chosen for its rifle gives only
		 * its rifle.
		 */
		struct SkinChoices {
			bool chosen[kNumSkinSlots] = {};
			std::string mod[kNumSkinSlots]; // "" with `chosen`: Default

			static SkinChoices Load();
			/** False if it could not be written */
			bool Save() const;

			bool operator==(const SkinChoices& o) const;
			bool operator!=(const SkinChoices& o) const { return !(*this == o); }
		};

		/** Mods marked as favourites (FavoriteMods.txt): listed first everywhere */
		class FavoriteMods {
		public:
			static std::vector<std::string> Load();
			static bool Contains(const std::string& mod);
			static void Set(const std::string& mod, bool favorite);
		};
	} // namespace gui
} // namespace spades
