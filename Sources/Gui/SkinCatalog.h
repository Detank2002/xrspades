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

#include "SkinPreviewScript.h"
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
			SkinPreviewOptions previewOptions;

			std::optional<std::string> ReadModFile(const std::string& modName,
			                                       const std::string& path,
			                                       std::size_t maxBytes = 34 * 1024 * 1024) const;

		public:
			SkinCatalog(ModsScreenHelper& helper, const std::vector<std::string>& enabled,
			            SkinPreviewOptions previewOptions = {});
			const std::vector<SkinMod>& GetMods() const { return mods; }
			const std::vector<std::string>& GetPakPaths(const std::string& modName) const;

			/** Captures the assembled idle pose from this candidate, then stock.
			 * Never mounts a candidate or uses the live script/renderer caches.
			 * A failed view capture tries third person, then a bounded body fallback.
			 */
			SkinPreviewScene LoadPreviewScene(SkinType type, const std::string& modName) const;

			/** Model-only/unsupported-script fallback, also read independently. */
			Handle<VoxelModel> LoadPreview(SkinType type, const std::string& modName) const;

			// Optional TrueType/OpenType face for an independent Aa sample. An
			// atlas-only language pack has no scalable face; the UI uses its GUI
			// font for that sample instead. Empty name always reads the stock face.
			std::optional<std::string> LoadFontPreview(const std::string& modName) const;
		};
	} // namespace gui
} // namespace spades
