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

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "VulkanMapChunk.h"
#include "VulkanSettings.h"
#include <cstring>
#include "VulkanMapRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanBuffer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Client/GameMap.h>
#include <Core/Debug.h>
#include <Core/Settings.h>

// 2: the map's vertex positions in a stream of their own (VulkanMapChunk::
// SeparatePositions); 1: each vertex whole. Read at start.
DEFINE_SPADES_SETTING(r_mapVertexStreams, "2");


namespace spades {
	namespace draw {
		bool VulkanMapChunk::SeparatePositions() {
			static const bool separate = (int)r_mapVertexStreams != 1;
			return separate;
		}

		VulkanMapChunk::VulkanMapChunk(VulkanMapRenderer& r, client::GameMap* mp, int cx, int cy, int cz)
		    : renderer(r),
		      device(r.renderer.GetDevice()) {
			SPADES_MARK_FUNCTION();

			map = mp;
			chunkX = cx;
			chunkY = cy;
			chunkZ = cz;
			needsUpdate = true;
			realized = false;

			centerPos = MakeVector3(
			    cx * (float)Size + (float)Size / 2,
			    cy * (float)Size + (float)Size / 2,
			    cz * (float)Size + (float)Size / 2
			);

			radius = (float)Size * 0.5F * sqrtf(3.0F);
			aabb = AABB3(
			    cx * (float)Size,
			    cy * (float)Size,
			    cz * (float)Size,
			    (float)Size,
			    (float)Size,
			    (float)Size
			);
		}

		VulkanMapChunk::~VulkanMapChunk() {
			SetRealized(false);
		}

		void VulkanMapChunk::SetRealized(bool b) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (realized == b)
				return;

			if (!b) {
				// Release buffers
				vertexBuffer.Set(nullptr, false);
				indexBuffer.Set(nullptr, false);

				// Clear CPU-side data
				std::vector<VertexPosition> p;
				p.swap(positions);
				std::vector<VertexSurface> sf;
				sf.swap(surfaces);

				std::vector<uint16_t> i;
				i.swap(indices);
			} else {
				needsUpdate = true;
			}

			realized = b;
		}

		uint8_t VulkanMapChunk::calcAOID(int x, int y, int z,
		                                 int ux, int uy, int uz, int vx, int vy, int vz) {
			int v = 0;
			if (IsSolid(x - ux, y - uy, z - uz))
				v |= 1;
			if (IsSolid(x + ux, y + uy, z + uz))
				v |= 1 << 1;
			if (IsSolid(x - vx, y - vy, z - vz))
				v |= 1 << 2;
			if (IsSolid(x + vx, y + vy, z + vz))
				v |= 1 << 3;
			if (IsSolid(x - ux + vx, y - uy + vy, z - uz + vz))
				v |= 1 << 4;
			if (IsSolid(x - ux - vx, y - uy - vy, z - uz - vz))
				v |= 1 << 5;
			if (IsSolid(x + ux + vx, y + uy + vy, z + uz + vz))
				v |= 1 << 6;
			if (IsSolid(x + ux - vx, y + uy - vy, z + uz - vz))
				v |= 1 << 7;
			return (uint8_t)v;
		}

		void VulkanMapChunk::EmitVertex(int aoX, int aoY, int aoZ, int x, int y, int z,
		                                int ux, int uy, int vx, int vy, uint32_t color,
		                                int nx, int ny, int nz) {
			SPADES_MARK_FUNCTION_DEBUG();

			int uz = (ux == 0 && uy == 0) ? 1 : 0;
			int vz = (vx == 0 && vy == 0) ? 1 : 0;

			// Evaluate ambient occlusion
			unsigned int aoID = calcAOID(aoX, aoY, aoZ, ux, uy, uz, vx, vy, vz);

			// The face's direction, in the order Update emits them; the shader
			// has each one's u and v (Shaders/Vulkan/MapVertex.glsl), from which
			// it works out the normal, the sun shading, the face's centre and
			// each corner's place in the AO atlas
			const uint8_t face = nz == 1 ? 0 : nz == -1 ? 1 : nx == -1 ? 2 : nx == 1 ? 3 : ny == -1 ? 4 : 5;
			VertexSurface surface;
			surface.r = (uint8_t)(color);
			surface.g = (uint8_t)(color >> 8);
			surface.b = (uint8_t)(color >> 16);
			surface.ao = (uint8_t)aoID;

			uint16_t idx = (uint16_t)positions.size();
			auto corner = [&](int px, int py, int pz, int c) {
				positions.push_back(
				  {(uint8_t)px, (uint8_t)py, (uint8_t)pz, (uint8_t)(face | (c << 3))});
				surfaces.push_back(surface);
			};
			corner(x, y, z, 0);
			corner(x + ux, y + uy, z + uz, 1);
			corner(x + vx, y + vy, z + vz, 2);
			corner(x + ux + vx, y + uy + vy, z + uz + vz, 3);

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx + 1);
			indices.push_back(idx + 3);
			indices.push_back(idx + 2);
		}

		bool VulkanMapChunk::IsSolid(int x, int y, int z) {
			if (!map)
				return false;
			if (z < 0)
				return false;
			if (z >= 64)
				return true;

			x = ((x % map->Width()) + map->Width()) % map->Width();
			y = ((y % map->Height()) + map->Height()) % map->Height();

			// Mirror GL's GLMapChunk hack: z=63 queries look up z=62 instead
			// when water is on, so coastal "water-surface" voxels get rendered
			// (their above-water neighbor counts as solid) and pure-water
			// columns drop out.
			return map->IsSolid(x, y, (z == 63 && (int)renderer.renderer.GetSettings().r_water > 0) ? 62 : z);
		}

		void VulkanMapChunk::Update() {
			SPADES_MARK_FUNCTION();

			if (!map)
				return;


			positions.clear();
			surfaces.clear();
			indices.clear();

			int originX = chunkX << SizeBits;
			int originY = chunkY << SizeBits;
			int originZ = chunkZ << SizeBits;

			// Build mesh for this chunk
			for (int x = 0; x < Size; x++) {
				for (int y = 0; y < Size; y++) {
					for (int z = 0; z < Size; z++) {
						int wx = originX + x;
						int wy = originY + y;
						int wz = originZ + z;

						if (!IsSolid(wx, wy, wz))
							continue;

						// A z=63 block is drawn only under a solid z=62 one (IsSolid's
						// water rule): it is that block's ground going on under the
						// water, so it takes its colour. Its own is the sea floor's,
						// often a light blue, which the water's clear edge at the shore
						// let show as a bright line along the waterline.
						const bool underShore =
						  wz == 63 && (int)renderer.renderer.GetSettings().r_water > 0;
						uint32_t color = map->GetColor(wx, wy, underShore ? 62 : wz);

						// Apply damage darkening - match OpenGL exactly
						// Health is stored in upper byte (0xHHBBGGRR format)
						int health = color >> 24;
						if (health < 100) {
							// Damaged block: darken by 50%
							color &= 0xFFFFFF;   // Mask to RGB only
							color &= 0xFEFEFE;   // Clear low bits
							color >>= 1;         // Divide RGB by 2
						}

						// Emit faces for this voxel
						// +Z face (up)
						// A face drawn by something else in its place (a chipped
						// one, ImpactMarks) is left out
						auto shown = [&](int face) {
							return !renderer.IsFaceHidden(wx, wy, wz, face);
						};

						if (!IsSolid(wx, wy, wz + 1) && shown(4)) {
							EmitVertex(wx, wy, wz + 1, x + 1, y, z + 1, -1, 0, 0, 1, color, 0, 0, 1);
						}

						// -Z face (down)
						if (!IsSolid(wx, wy, wz - 1) && shown(5)) {
							EmitVertex(wx, wy, wz - 1, x, y, z, 1, 0, 0, 1, color, 0, 0, -1);
						}

						// -X face
						if (!IsSolid(wx - 1, wy, wz) && shown(1)) {
							EmitVertex(wx - 1, wy, wz, x, y + 1, z, 0, 0, 0, -1, color, -1, 0, 0);
						}

						// +X face
						if (!IsSolid(wx + 1, wy, wz) && shown(0)) {
							EmitVertex(wx + 1, wy, wz, x + 1, y, z, 0, 0, 0, 1, color, 1, 0, 0);
						}

						// -Y face
						if (!IsSolid(wx, wy - 1, wz) && shown(3)) {
							EmitVertex(wx, wy - 1, wz, x, y, z, 0, 0, 1, 0, color, 0, -1, 0);
						}

						// +Y face
						if (!IsSolid(wx, wy + 1, wz) && shown(2)) {
							EmitVertex(wx, wy + 1, wz, x + 1, y + 1, z, 0, 0, -1, 0, color, 0, 1, 0);
						}
					}
				}
			}

			// Upload into buffers of their own. The previous ones may still be
			// drawn from by a frame in flight, so they are never written again:
			// they are retired, and freed once no frame can use them.
			auto upload = [&](Handle<VulkanBuffer>& buffer, const void* data, std::size_t size,
			                  VkBufferUsageFlags usage) {
				if (buffer)
					renderer.GetRenderer().QueueBufferForDeletion(buffer);
				buffer.Set(nullptr, false);
				if (size == 0)
					return;
				buffer = Handle<VulkanBuffer>::New(
				  device, size, usage,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				std::memcpy(buffer->Map(), data, size);
				buffer->Unmap();
			};
			// One buffer: the positions, then the surfaces (r_mapVertexStreams 2),
			// or each vertex's two halves together (1)
			{
				const std::size_t n = positions.size();
				std::vector<uint8_t> data(n * 8);
				if (SeparatePositions()) {
					std::memcpy(data.data(), positions.data(), n * 4);
					std::memcpy(data.data() + n * 4, surfaces.data(), n * 4);
					surfaceOffset = n * 4;
				} else {
					for (std::size_t i = 0; i < n; i++) {
						std::memcpy(&data[i * 8], &positions[i], 4);
						std::memcpy(&data[i * 8 + 4], &surfaces[i], 4);
					}
					surfaceOffset = 0;
				}
				upload(vertexBuffer, data.data(), data.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
			}
			upload(indexBuffer, indices.data(), indices.size() * sizeof(uint16_t),
			       VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

			needsUpdate = false;
		}

		float VulkanMapChunk::DistanceFromEye(const Vector3& eye) {
			Vector3 diff = eye - centerPos;

			{
				const float mw = (float)map->Width();
				const float mh = (float)map->Height();
				if (diff.x < -(mw * 0.5F)) diff.x += mw;
				if (diff.y < -(mh * 0.5F)) diff.y += mh;
				if (diff.x >  (mw * 0.5F)) diff.x -= mw;
				if (diff.y >  (mh * 0.5F)) diff.y -= mh;
			}

			float dist = std::max(fabsf(diff.x), fabsf(diff.y));
			return std::max(dist - ((float)Size * 0.5F), 0.0F);
		}

		void VulkanMapChunk::UpdateIfNeeded() {
			if (needsUpdate && realized) {
				Update();
			}
		}

		void VulkanMapChunk::RenderSunlightPass(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (indices.empty() || !vertexBuffer || !indexBuffer)
				return;

			const auto& eye = renderer.renderer.GetSceneDef().viewOrigin;
			Vector3 diff = eye - centerPos;
			float sx = 0.0F, sy = 0.0F;

			// (Not when the whole map is drawn once, seen from outside.)
			if (!renderer.renderer.GetSceneDef().wholeMap) {
				const float mw = (float)map->Width();
				const float mh = (float)map->Height();
				if (diff.x >  (mw * 0.5F)) sx += mw;
				if (diff.y >  (mh * 0.5F)) sy += mh;
				if (diff.x < -(mw * 0.5F)) sx -= mw;
				if (diff.y < -(mh * 0.5F)) sy -= mh;
			}

			// Frustum cull
			{
				AABB3 bx = aabb;
				bx.min.x += sx; bx.max.x += sx;
				bx.min.y += sy; bx.max.y += sy;
				if (!renderer.renderer.BoxFrustrumCull(bx))
					return;
			}

			// What changes from chunk to chunk: its origin, wrapped around the map to
			// lie nearest the eye. The pass's view is bound once for all of them.
			MapDrawConstants drawConstants{};
			drawConstants.modelOrigin = MakeVector3((float)(chunkX << SizeBits) + sx,
			                                        (float)(chunkY << SizeBits) + sy,
			                                        (float)(chunkZ << SizeBits));
			vkCmdPushConstants(commandBuffer, renderer.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
			                   sizeof(drawConstants), &drawConstants);

			// (The textures' set, 0, is the same for every chunk: bound once for
			// the pass, VulkanMapRenderer::DrawVisibleChunks)

			// The vertices: the positions, and the surfaces after them
			VkBuffer vb[2] = {vertexBuffer->GetBuffer(), vertexBuffer->GetBuffer()};
			VkDeviceSize offsets[2] = {0, surfaceOffset};
			vkCmdBindVertexBuffers(commandBuffer, 0, SeparatePositions() ? 2 : 1, vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			// Draw
			vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);
		}

		void VulkanMapChunk::RenderShadowMapPass(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (indices.empty() || !vertexBuffer || !indexBuffer)
				return;

			const auto& eye = renderer.renderer.GetSceneDef().viewOrigin;
			Vector3 diff = eye - centerPos;
			float sx = 0.0F, sy = 0.0F;

			// (Not when the whole map is drawn once, seen from outside.)
			if (!renderer.renderer.GetSceneDef().wholeMap) {
				const float mw = (float)map->Width();
				const float mh = (float)map->Height();
				if (diff.x >  (mw * 0.5F)) sx += mw;
				if (diff.y >  (mh * 0.5F)) sy += mh;
				if (diff.x < -(mw * 0.5F)) sx -= mw;
				if (diff.y < -(mh * 0.5F)) sy -= mh;
			}

			// Push model origin for this chunk
			Vector3 modelOrigin = MakeVector3(
				(float)(chunkX << SizeBits) + sx,
				(float)(chunkY << SizeBits) + sy,
				(float)(chunkZ << SizeBits)
			);
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
			                   0, sizeof(Vector3), &modelOrigin);

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			// Draw
			vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);
		}

	} // namespace draw
} // namespace spades
