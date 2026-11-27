/*
 Copyright (c) 2013 Fran6nd

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <Client/GameMap.h>
#include <Client/IRenderer.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanMapRenderer;
		class VulkanBuffer;

		class VulkanMapChunk {
		public:
			// A vertex, 8 bytes (it was 20), the rest worked out by the shader from
			// the face it is a corner of (Shaders/Vulkan/MapVertex.glsl). A tiled
			// GPU reads each vertex to bin it and again in every tile it is drawn
			// in: the less, the cheaper every triangle. Where it is...
			struct VertexPosition {
				uint8_t x, y, z;
				uint8_t face; // direction 0..5 | corner (0..3) << 3
			};
			// ...and what it looks like
			struct VertexSurface {
				uint8_t r, g, b;
				uint8_t ao; // the face's tile in the AO atlas
			};
			/**
			 * r_mapVertexStreams: 2 (the default), the positions in a stream of
			 * their own, ahead of the rest in the buffer, so binning reads only
			 * them; 1, each vertex's 8 bytes together. Read once: the pipelines
			 * are made for it.
			 */
			static bool SeparatePositions();

		private:

			VulkanMapRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			client::GameMap* map;
			int chunkX, chunkY, chunkZ;
			AABB3 aabb;

			Vector3 centerPos;
			float radius;

			std::vector<VertexPosition> positions;
			std::vector<VertexSurface> surfaces;
			VkDeviceSize surfaceOffset = 0; // where the surfaces start in vertexBuffer
			std::vector<uint16_t> indices;
			Handle<VulkanBuffer> vertexBuffer;
			Handle<VulkanBuffer> indexBuffer;

			bool needsUpdate;
			bool realized;

			uint8_t calcAOID(int x, int y, int z, int ux, int uy, int uz, int vx, int vy, int vz);

			void EmitVertex(int aoX, int aoY, int aoZ, int x, int y, int z, int ux, int uy, int vx,
			                int vy, uint32_t color, int nx, int ny, int nz);

			bool IsSolid(int x, int y, int z);

			void Update();

		public:
			enum { Size = 16, SizeBits = 4 };
			VulkanMapChunk(VulkanMapRenderer&, client::GameMap* mp, int cx, int cy, int cz);
			~VulkanMapChunk();

			void SetNeedsUpdate() { needsUpdate = true; }
			bool NeedsUpdate() const { return needsUpdate && realized; }
			void UpdateIfNeeded();

			void SetRealized(bool);

			float DistanceFromEye(const Vector3& eye);

			void RenderSunlightPass(VkCommandBuffer commandBuffer);
			void RenderShadowMapPass(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout);

			uint32_t GetIndexCount() const { return (uint32_t)indices.size(); }
			bool IsRealized() const { return realized; }
		};
	} // namespace draw
} // namespace spades
