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

#include <vector>

#include <Core/Math.h>

namespace spades {
	class VoxelModel;
	namespace client {
		class IRenderer;
	}
	namespace gui {
		namespace ui {
			/**
			 * A lit, rotating voxel mesh in a 2D UI rectangle. Orthographic face
			 * projection lets the regular 2D batch draw it on every backend (also
			 * on the VR menu surface), without starting another world scene or
			 * touching the game's camera, fog, depth buffer or model cache.
			 */
			class VoxelModelPreview {
				struct Face {
					Vector3 center;
					Vector3 color;
					int direction;
				};
				std::vector<Face> faces;
				Vector3 halfSize{0, 0, 0};

			public:
				struct Quad {
					Vector2 origin, axisU, axisV;
					Vector4 color;
					float depth;
				};

				explicit VoxelModelPreview(const VoxelModel& model);
				bool IsEmpty() const { return faces.empty(); }
				std::size_t GetFaceCount() const { return faces.size(); }
				std::vector<Quad> Project(const AABB2& bounds, float yaw) const;
				void Render(client::IRenderer& renderer, const AABB2& bounds, float yaw) const;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
