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

#include <map>
#include <string>
#include <vector>

#include "SkinChoice.h"
#include <Core/RefCountedObject.h>

namespace spades {
	class Bitmap;
	class VoxelModel;
	namespace client {
		class IImage;
		class IRenderer;
	} // namespace client
	namespace gui {
		/** What a mod has for each skin slot, read from its paks */
		struct ModSkinInfo {
			bool slots[kNumSkinSlots] = {};
			// The model that stands for it in a slot (its weapon, spade...):
			// a path in the mod's paks, "" if it has none (sounds or a script only)
			std::string model[kNumSkinSlots];
			std::string modelPak[kNumSkinSlots]; // the pak it is in; "" for a stock one
			bool HasAny() const;
		};

		/** Reads a mod's paks for what it has (no mod: the stock files) */
		ModSkinInfo ScanModSkins(const std::string& mod);

		/**
		 * A still picture of a voxel model, a three-quarter view from above,
		 * its longest side across, lit, transparent round it. Drawn on the CPU
		 * once; nothing is drawn again while it is shown.
		 */
		Handle<Bitmap> RenderVoxelPreview(VoxelModel&, int width, int height);

		/**
		 * The pictures of mods' models, for the mod pages. Each is drawn the
		 * first time it is asked for and kept, on disk too (ModPreviews/, by
		 * the mod's name and size), so a page opens without drawing them all
		 * again. At most one is drawn a frame (`Get` returns null until then).
		 */
		class ModPreviewCache : public RefCountedObject {
			struct Entry {
				Handle<client::IImage> image;
				bool failed = false;
			};
			std::map<std::string, Entry> entries;
			std::map<std::string, ModSkinInfo> infos;
			int frameBudget = 1;

		protected:
			~ModPreviewCache();

		public:
			ModPreviewCache();
			/** Call once a frame: lets one more picture be drawn */
			void NewFrame() { frameBudget = 1; }
			const ModSkinInfo& GetInfo(const std::string& mod);
			/** Null while not drawn yet, or if the mod has no model there */
			client::IImage* Get(client::IRenderer&, const std::string& mod, SkinSlot slot);
			/** The first slot the mod has a model for, -1 if none */
			int FirstModelSlot(const std::string& mod);
		};
	} // namespace gui
} // namespace spades
