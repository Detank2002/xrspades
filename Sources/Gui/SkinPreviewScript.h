/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.
 Distributed under the GNU General Public License, version 3 or later.
 See <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>

#include "SkinPreviewScene.h"
#include "SkinSelection.h"

namespace spades {
	namespace gui {
		struct SkinPreviewOptions {
			Vector3 teamColor = MakeVector3(0.2F, 0.2F, 0.2F);
			std::map<std::string, std::string> settings;
		};

		// The reader must bypass the live overlay: candidate pak, then stock. It
		// must enforce maxBytes before ZIP decompression, not after allocation.
		using SkinPreviewReader =
		  std::function<std::optional<std::string>(const std::string& path, std::size_t maxBytes)>;

		/**
		 * Runs only the skin's view/third-person module against a private recorder.
		 * No real renderer, audio device, config writer, file/network API or Client
		 * module is registered. Includes, source bytes, execution and draw counts
		 * are bounded. The caller handles failure/fallback; partial scenes never
		 * escape after an exception.
		 */
		SkinPreviewScene CaptureSkinPreview(SkinType type, const SkinPreviewReader& reader,
		                                    const SkinPreviewOptions& options = {},
		                                    bool thirdPerson = false);

		/** Bounded KV6 parser shared by scene loading and model-only fallback. */
		Handle<VoxelModel> LoadSkinPreviewModel(const std::string& bytes);
	} // namespace gui
} // namespace spades
