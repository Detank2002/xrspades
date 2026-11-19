/*
 Copyright (c) 2026 XRSpades contributors

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
#include <deque>
#include <vector>

#include <Core/Settings.h>
#include <Core/VoxelModel.h>

#include "Client.h"
#include "GameMap.h"
#include "World.h"
#include "IModel.h"
#include "IRenderer.h"
#include "ImpactMarks.h"

DEFINE_SPADES_SETTING(cg_impactMarks, "1");

namespace spades {
	namespace client {
		namespace {
			// The model's grid: kSize voxels across, two deep (the pit's floor
			// and the rim, and the chips standing proud of the rim).
			constexpr int kSize = 12;
			constexpr int kVariants = 6;
			// Voxels a block: a bullet's mark is ~0.35 blocks across, the spade's
			// ~0.6.
			constexpr float kBulletScale = 1.0F / 20.0F;
			constexpr float kSpadeScale = 1.0F / 13.0F;
			constexpr std::size_t kMaxMarks = 300;
			constexpr float kLifetime = 45.0F, kFade = 5.0F;
			// The pit: dark, but not black (black voxels take the custom colour)
			constexpr uint32_t kPit = 0x1C1A18;

			uint32_t Hash(uint32_t x) {
				x ^= x >> 16;
				x *= 0x7feb352dU;
				x ^= x >> 15;
				x *= 0x846ca68bU;
				x ^= x >> 16;
				return x;
			}
			float Noise(uint32_t seed, int x, int y) {
				return static_cast<float>(Hash(seed * 7919U + static_cast<uint32_t>(x) * 131U +
				                               static_cast<uint32_t>(y)) &
				                          0xFFFF) /
				       65535.0F;
			}
		} // namespace

		struct ImpactMarksImpl {
			Client& client;
			std::vector<Handle<IModel>> models; // kVariants
			struct Mark {
				Matrix4 matrix;
				IntVector3 block;
				uint32_t blockColor; // its RGB when hit: a change is a rebuild
				Vector3 colour;      // the rim's, from the block
				int variant;
				float age = 0.0F;
			};
			std::deque<Mark> marks;
			uint32_t seed = 1;

			explicit ImpactMarksImpl(Client& c) : client(c) {}

			void MakeModels() {
				if (!models.empty())
					return;
				IRenderer& r = client.GetRenderer();
				for (int v = 0; v < kVariants; v++) {
					auto m = Handle<VoxelModel>::New(kSize, kSize, 2);
					const float c = (kSize - 1) * 0.5F;
					const float pit = 1.6F + 0.5F * Noise(v, 99, 1);
					const float rim = pit + 1.4F + 0.6F * Noise(v, 98, 2);
					for (int x = 0; x < kSize; x++)
						for (int y = 0; y < kSize; y++) {
							// A ragged circle: the distance, roughened
							const float dx = x - c, dy = y - c;
							const float d = std::sqrt(dx * dx + dy * dy) +
							                (Noise(v, x, y) - 0.5F) * 1.3F;
							if (d < pit) {
								m->SetSolid(x, y, 0, kPit);
							} else if (d < rim) {
								m->SetSolid(x, y, 0, 0); // the block's colour
								// Chips standing proud, more of them near the pit
								if (Noise(v + 17, x, y) > 0.45F + (d - pit) / (rim - pit) * 0.4F)
									m->SetSolid(x, y, 1, 0);
							}
						}
					m->SetOrigin(MakeVector3(-c - 0.5F, -c - 0.5F, 0.0F));
					models.push_back(r.CreateModel(*m));
				}
			}
		};

		ImpactMarks::ImpactMarks(Client& c) : impl(new ImpactMarksImpl(c)) {}
		ImpactMarks::~ImpactMarks() = default;

		void ImpactMarks::Clear() { impl->marks.clear(); }

		void ImpactMarks::Add(const Vector3& hitPos, const IntVector3& block,
		                      const IntVector3& normal, bool spade) {
			if (!(bool)cg_impactMarks)
				return;
			auto map = impl->client.GetWorld() ? impl->client.GetWorld()->GetMap()
			                                   : Handle<GameMap>();
			if (!map || block.z >= 63 || !map->IsSolidWrapped(block.x, block.y, block.z))
				return;
			const int axis = normal.x != 0 ? 0 : normal.y != 0 ? 1 : 2;
			if (normal.x == 0 && normal.y == 0 && normal.z == 0)
				return;
			impl->MakeModels();

			// The face's frame: across it (u, v) and out of it (n)
			const Vector3 n = MakeVector3(normal);
			const Vector3 u = axis == 0 ? MakeVector3(0, 1, 0) : MakeVector3(1, 0, 0);
			const Vector3 v = Vector3::Cross(n, u);
			// Where on the face: the hit, kept far enough in that the mark fits
			const float scale = spade ? kSpadeScale : kBulletScale;
			const float half = (kSize * 0.5F) * scale;
			const Vector3 blockMin = MakeVector3(block);
			Vector3 at = hitPos;
			for (int i = 0; i < 3; i++) {
				float lo = blockMin[i] + std::min(half, 0.5F), hi = blockMin[i] + 1.0F - std::min(half, 0.5F);
				if (i == axis) {
					// On the face itself, just off it
					at[i] = blockMin[i] + (n[i] > 0.0F ? 1.0F : 0.0F) + n[i] * 0.004F;
				} else {
					at[i] = std::max(lo, std::min(hi, at[i]));
				}
			}
			ImpactMarksImpl::Mark mk;
			impl->seed = Hash(impl->seed + 0x9e3779b9U);
			mk.variant = static_cast<int>(impl->seed % kVariants);
			// A turn about the normal, so marks side by side differ
			const float turn = static_cast<float>((impl->seed >> 8) & 0xFF) / 255.0F * 6.2831853F;
			const Vector3 u2 = u * std::cos(turn) + v * std::sin(turn);
			const Vector3 v2 = Vector3::Cross(n, u2);
			mk.matrix = Matrix4::FromAxis(u2 * scale, v2 * scale, n * scale, at);
			mk.block = block;
			const uint32_t col = map->GetColorWrapped(block.x, block.y, block.z);
			mk.blockColor = col & 0xFFFFFF;
			// The rim a little darker than the block: broken, shadowed stone
			const float k = 0.7F / 255.0F;
			mk.colour = MakeVector3(static_cast<float>(col & 0xFF) * k,
			                        static_cast<float>((col >> 8) & 0xFF) * k,
			                        static_cast<float>((col >> 16) & 0xFF) * k);
			impl->marks.push_back(mk);
			while (impl->marks.size() > kMaxMarks)
				impl->marks.pop_front();
		}

		void ImpactMarks::Update(float dt) {
			auto map = impl->client.GetWorld() ? impl->client.GetWorld()->GetMap()
			                                   : Handle<GameMap>();
			if (!map) {
				impl->marks.clear();
				return;
			}
			auto& marks = impl->marks;
			marks.erase(std::remove_if(marks.begin(), marks.end(),
			                           [&](ImpactMarksImpl::Mark& m) {
				                           m.age += dt;
				                           const IntVector3& b = m.block;
				                           if (m.age > kLifetime)
					                           return true;
				                           if (!map->IsSolidWrapped(b.x, b.y, b.z))
					                           return true;
				                           const uint32_t c =
				                             map->GetColorWrapped(b.x, b.y, b.z) & 0xFFFFFF;
				                           // Rebuilt, or healed back to its jitter-free
				                           // colour by the server: the hole is gone
				                           return c != m.blockColor;
			                           }),
			            marks.end());
		}

		void ImpactMarks::Draw() {
			if (!(bool)cg_impactMarks || impl->marks.empty() || impl->models.empty())
				return;
			IRenderer& r = impl->client.GetRenderer();
			ModelRenderParam param;
			param.castShadow = false;
			param.drawOutline = false;
			for (const auto& m : impl->marks) {
				param.customColor = m.colour;
				param.matrix = m.matrix;
				// The last seconds: sinks back into the block
				const float left = kLifetime - m.age;
				if (left < kFade)
					param.matrix = param.matrix *
					               Matrix4::Translate(0.0F, 0.0F, -2.2F * (1.0F - left / kFade));
				r.RenderModel(*impl->models[static_cast<std::size_t>(m.variant)], param);
			}
		}
	} // namespace client
} // namespace spades
