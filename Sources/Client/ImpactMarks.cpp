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
#include <unordered_map>
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
			// Carved faces: the block's face, drawn as a slab kCarve voxels across
			// and kDepth deep in its own colour, the craters cut out of it. The
			// map leaves that face out (IRenderer::SetMapFaceHidden), so what is
			// seen is the slab: real pits, with walls and a floor, from any side.
			constexpr int kCarve = 8;
			constexpr int kDepth = 4;
			constexpr std::size_t kMaxFaces = 300;
			constexpr float kCarveLifetime = 90.0F;

			uint32_t Darken(uint32_t c, float k) {
				auto ch = [&](int shift) {
					return static_cast<uint32_t>(std::min(255.0F, ((c >> shift) & 0xFF) * k))
					       << shift;
				};
				return ch(0) | ch(8) | ch(16);
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

			// The carved faces, by block and face
			struct Carved {
				IntVector3 block, normal;
				uint32_t blockColor; // its colour (health included) when last built
				bool removed[kCarve][kCarve][kDepth] = {};
				Handle<IModel> model;
				// Behind it, the rest of the block as one plain box: a voxel
				// model's mesh can leave pinprick cracks where a pit's walls
				// meet the surface, and the block behind the slab is hollow, so
				// through one the sky beyond the wall showed. Six faces, no
				// cracks: through any crack only the box, dark like the pits.
				Handle<IModel> back;
				Matrix4 backMatrix;
				Matrix4 matrix;
				Vector3 u, v, face; // the slab's across axes, and the face's middle
				float age = 0.0F;
				uint64_t order = 0;
			};
			std::unordered_map<uint64_t, Carved> carved;
			uint64_t carveOrder = 0;
			static uint64_t Key(const IntVector3& b, const IntVector3& n) {
				const int face = n.x > 0 ? 0 : n.x < 0 ? 1 : n.y > 0 ? 2 : n.y < 0 ? 3 : n.z > 0 ? 4 : 5;
				return (static_cast<uint64_t>(b.x & 0xFFFF) << 32) |
				       (static_cast<uint64_t>(b.y & 0xFFFF) << 16) |
				       (static_cast<uint64_t>(b.z & 0xFF) << 3) | static_cast<uint64_t>(face);
			}

			void Restore(Carved& c) { client.GetRenderer().SetMapFaceHidden(c.block, c.normal, false); }

			/** The slab's model from what is cut out of it, in the block's colour
			 * as the map draws it (halved while damaged); the pits' walls and
			 * floors darker, as the map's corners are. */
			void Build(Carved& c, uint32_t mapColor) {
				c.blockColor = mapColor;
				uint32_t col = mapColor & 0xFFFFFF;
				if ((mapColor >> 24) < 100)
					col = (col & 0xFEFEFE) >> 1;
				auto m = Handle<VoxelModel>::New(kCarve, kCarve, kDepth);
				for (int x = 0; x < kCarve; x++)
					for (int y = 0; y < kCarve; y++)
						for (int z = 0; z < kDepth; z++) {
							if (c.removed[x][y][z])
								continue;
							// Open to a pit: on its wall or floor
							bool wall = false;
							const int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
							                     {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
							for (const auto& o : d) {
								const int xx = x + o[0], yy = y + o[1], zz = z + o[2];
								if (xx < 0 || yy < 0 || zz < 0 || xx >= kCarve || yy >= kCarve ||
								    zz >= kDepth)
									continue;
								if (c.removed[xx][yy][zz])
									wall = true;
							}
							uint32_t v = col;
							// The outer ring's sides lie on the block's own side faces
							// (just in front): darkened, the two fought over which is in
							// front and a dark square flickered at the block's edge (in a
							// headset most, testers). It stays the block's colour.
							const bool ring = x == 0 || y == 0 || x == kCarve - 1 || y == kCarve - 1;
							if (wall && !ring)
								v = Darken(col, z == 0 ? 0.78F : 0.55F - 0.1F * static_cast<float>(z));
							// Never quite black: a black voxel takes the custom colour
							if ((v & 0xFFFFFF) == 0)
								v = 0x010101;
							m->SetSolid(x, y, z, v);
						}
				// Voxel (0,0,0) from 0 to 1, not centred on 0 as models are by
				// default: centred, the slab stood half a voxel proud of the face
				// and short of two of its edges, a slit into the hollow block
				m->SetOrigin(MakeVector3(0.5F, 0.5F, 0.5F));
				c.model = client.GetRenderer().CreateModel(*m);
				auto b = Handle<VoxelModel>::New(1, 1, 1);
				b->SetOrigin(MakeVector3(0.5F, 0.5F, 0.5F));
				uint32_t bc = Darken(col, 0.35F);
				if ((bc & 0xFFFFFF) == 0)
					bc = 0x010101;
				b->SetSolid(0, 0, 0, bc);
				c.back = client.GetRenderer().CreateModel(*b);
			}

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

		void ImpactMarks::Clear() {
			impl->marks.clear();
			for (auto& kv : impl->carved)
				impl->Restore(kv.second);
			impl->carved.clear();
		}

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
			// Carved into the face where the renderer can leave the face out
			if (AddCarved(hitPos, block, normal, spade))
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

		bool ImpactMarks::AddCarved(const Vector3& hitPos, const IntVector3& block,
		                            const IntVector3& normal, bool spade) {
			auto map = impl->client.GetWorld()->GetMap();
			IRenderer& r = impl->client.GetRenderer();
			const uint64_t key = ImpactMarksImpl::Key(block, normal);
			auto it = impl->carved.find(key);
			if (it == impl->carved.end()) {
				if (!r.SetMapFaceHidden(block, normal, true))
					return false;
				// The oldest goes when there are too many
				if (impl->carved.size() >= kMaxFaces) {
					auto oldest = impl->carved.begin();
					for (auto i = impl->carved.begin(); i != impl->carved.end(); ++i)
						if (i->second.order < oldest->second.order)
							oldest = i;
					impl->Restore(oldest->second);
					impl->carved.erase(oldest);
				}
				ImpactMarksImpl::Carved c;
				c.block = block;
				c.normal = normal;
				// The slab: model x and y across the face (u, v), z into it from
				// the face, kCarve voxels to the block. A hair wider than the
				// block, never narrower: the block behind the slab is hollow (the
				// map draws no faces inside a wall), so any crack at its rim would
				// be a slit to see through the wall. Its sides stand that hair
				// outside the block's own, in front of them: half a percent of a
				// block, too little to see and enough that the depth buffer always
				// tells them apart (0.2% flickered in a headset, whose two eyes'
				// projection rounds more).
				const Vector3 n = MakeVector3(normal);
				const int axis = normal.x != 0 ? 0 : normal.y != 0 ? 1 : 2;
				// Right-handed (u x v = -n, the model's z): mirrored, the slab's
				// faces would turn inside out and show its inside
				const Vector3 u = axis == 0 ? MakeVector3(0, 1, 0) : MakeVector3(1, 0, 0);
				const Vector3 v = Vector3::Cross(-n, u);
				const float s = 1.0F / kCarve, side = s * 1.01F;
				Vector3 face = MakeVector3(block) + 0.5F + n * 0.5F;
				c.matrix = Matrix4::FromAxis(u * side, v * side, -n * s, face) *
				           Matrix4::Translate(-kCarve * 0.5F, -kCarve * 0.5F, 0.0F);
				// The box: from inside the slab's floor to the block's far side,
				// a hair inside the block all round (never seen but through a
				// crack), as far inside as the slab is out, for the same reason
				c.backMatrix = Matrix4::FromAxis(u * 0.99F, v * 0.99F, -n * 0.545F,
				                                 face - n * 0.45F) *
				               Matrix4::Translate(-0.5F, -0.5F, 0.0F);
				c.u = u;
				c.v = v;
				c.face = face;
				it = impl->carved.emplace(key, std::move(c)).first;
			}
			ImpactMarksImpl::Carved& c = it->second;
			c.age = 0.0F;
			c.order = ++impl->carveOrder;

			// The crater: where on the face it struck, kept in from the edges
			impl->seed = Hash(impl->seed + 0x9e3779b9U);
			const float radius = spade ? 2.0F + 0.6F * Noise(impl->seed, 1, 2)
			                           : 1.15F + 0.45F * Noise(impl->seed, 3, 4);
			const float lo = radius + 0.4F, hi = kCarve - radius - 0.4F;
			// Where it struck, in the slab's voxels
			const Vector3 local = hitPos - c.face;
			const float cu = std::max(lo, std::min(hi, Vector3::Dot(local, c.u) * kCarve + kCarve * 0.5F));
			const float cv = std::max(lo, std::min(hi, Vector3::Dot(local, c.v) * kCarve + kCarve * 0.5F));
			const int deepest = spade ? 2 : 1; // the last layer is always the floor
			// The outermost ring of voxels is never cut, whatever the noise: a
			// pit through it would open sideways into the hollow block next to
			// it (a slit through the wall), and would nick the block's outline
			for (int x = 1; x < kCarve - 1; x++)
				for (int y = 1; y < kCarve - 1; y++) {
					const float dx = x + 0.5F - cu, dy = y + 0.5F - cv;
					const float d = std::sqrt(dx * dx + dy * dy) +
					                (Noise(impl->seed, x, y) - 0.5F) * 0.9F;
					for (int z = 0; z <= deepest; z++)
						if (d < radius * (1.0F - 0.38F * static_cast<float>(z)))
							c.removed[x][y][z] = true;
				}
			impl->Build(c, map->GetColorWrapped(block.x, block.y, block.z));
			return true;
		}

		void ImpactMarks::Update(float dt) {
			// Switched off: the faces go back at once; left out with nothing
			// drawn in their place, they would be holes through the walls
			if (!(bool)cg_impactMarks && !impl->carved.empty()) {
				for (auto& kv : impl->carved)
					impl->Restore(kv.second);
				impl->carved.clear();
			}
			auto map = impl->client.GetWorld() ? impl->client.GetWorld()->GetMap()
			                                   : Handle<GameMap>();
			if (!map) {
				impl->marks.clear();
				impl->carved.clear(); // its map, and the hidden faces, are gone
				return;
			}
			// Carved faces: gone with the block, put back after a while, and
			// rebuilt in the block's new colour when it is damaged or healed
			for (auto it = impl->carved.begin(); it != impl->carved.end();) {
				auto& c = it->second;
				c.age += dt;
				const IntVector3& b = c.block;
				const bool solid = map->IsSolidWrapped(b.x, b.y, b.z);
				const uint32_t col = solid ? map->GetColorWrapped(b.x, b.y, b.z) : 0;
				if (!solid || c.age > kCarveLifetime ||
				    (col & 0xFFFFFF) != (c.blockColor & 0xFFFFFF)) {
					impl->Restore(c);
					it = impl->carved.erase(it);
					continue;
				}
				if (col != c.blockColor)
					impl->Build(c, col); // its health changed
				++it;
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
			if (!(bool)cg_impactMarks) {
				// As in Update, for a frame drawn without one (paused)
				for (auto& kv : impl->carved)
					impl->Restore(kv.second);
				impl->carved.clear();
				return;
			}
			IRenderer& r = impl->client.GetRenderer();
			ModelRenderParam param;
			param.castShadow = false;
			param.drawOutline = false;
			// The carved faces stand in for the map's own, in the shadows too: the
			// map's shadow mesh has left the face out as well, and without the slab
			// the sun came through the block onto what was behind it
			param.castShadow = true;
			for (const auto& kv : impl->carved) {
				param.matrix = kv.second.matrix;
				if (kv.second.model)
					r.RenderModel(*kv.second.model, param);
				if (kv.second.back) {
					param.matrix = kv.second.backMatrix;
					r.RenderModel(*kv.second.back, param);
				}
			}
			if (impl->marks.empty() || impl->models.empty())
				return;
			param.castShadow = false;
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
