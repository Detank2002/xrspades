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

#include <map>
#include <string>
#include <vector>

#include "SkinSelection.h"
#include <Core/RefCountedObject.h>

namespace spades {
	class VoxelModel;

	namespace gui {
		class ModsScreenHelper;

		/** A snapshot of the enabled, installed collection, in apply order. */
		class SkinCatalog : public RefCountedObject {
			std::vector<SkinMod> mods;
			std::map<std::string, std::vector<std::string>> paks;
			std::map<std::string, std::vector<std::string>> files;

			std::optional<std::string> ReadModFile(const std::string& modName,
			                                       const std::string& path) const;

		public:
			SkinCatalog(ModsScreenHelper& helper, const std::vector<std::string>& enabled);
			const std::vector<SkinMod>& GetMods() const { return mods; }
			const std::vector<std::string>& GetPakPaths(const std::string& modName) const;

			/**
			 * Reads the display model from THIS mod's archives, never the currently
			 * mounted winner or the renderer's model cache. Empty modName reads the
			 * unmodified base model. Does not mount or execute any preview scripts.
			 */
			Handle<VoxelModel> LoadPreview(SkinType type, const std::string& modName) const;
		};
	} // namespace gui
} // namespace spades
