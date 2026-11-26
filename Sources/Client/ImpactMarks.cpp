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
			// Damaged blocks: a block hit is drawn as a model of kCells voxels a
			// side in its own colour, the craters cut out of whichever faces were
			// hit, and the map leaves all of that block's faces out
			// (IRenderer::SetMapFaceHidden), so what is seen is the model: real
			// pits with walls and floors, from any side, on any face. A face with
			// a solid block against it is never cut into: the map draws no face
			// between two solid blocks, so a gap there was a slit through the
			// wall (testers saw through chipped edges). Nor is an edge or a
			// corner, open to the air or not: a pit is cut only inside a face, so
			// the block keeps its whole outline and no two pits, on two faces or
			// two blocks, ever meet at a corner to open a way through. No pit
			// goes deeper than kMaxDepth voxels, so a solid core is always left.
			constexpr int kCells = 8;
			constexpr int kMaxDepth = 3; // the spade's; a bullet's is 2
			constexpr std::size_t kMaxBlocks = 300;
			constexpr float kDamageLifetime = 90.0F;

			uint32_t Darken(uint32_t c, float k) {
				auto ch = [&](int shift) {
					return static_cast<uint32_t>(std::min(255.0F, ((c >> shift) & 0xFF) * k))
					       << shift;
				};
				return ch(0) | ch(8) | ch(16);
			}
		} // namespace

		constexpr int kFaces[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
		                              {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

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

			// The damaged blocks, by block
			struct Damaged {
				IntVector3 block;
				uint32_t blockColor; // its colour (health included) when last built
				bool removed[kCells][kCells][kCells] = {};
				Handle<IModel> model;
				Matrix4 matrix;
				float age = 0.0F;
				uint64_t order = 0;
				int openFaces = 0; // OpenFaces when last cut
			};

			/** Which of a block's faces (kFaces) have air against them, a bit
			 * each: only those may be cut into. */
			static int OpenFaces(GameMap& map, const IntVector3& b) {
				int mask = 0;
				for (int f = 0; f < 6; f++) {
					const int x = b.x + kFaces[f][0], y = b.y + kFaces[f][1], z = b.z + kFaces[f][2];
					if (z < 0 || !map.IsSolidWrapped(x, y, z))
						mask |= 1 << f;
				}
				return mask;
			}
			/** The faces (kFaces) a cell of the model lies on, a bit each. */
			static int CellFaces(const int cell[3]) {
				int mask = 0;
				for (int q = 0; q < 3; q++) {
					if (cell[q] == kCells - 1)
						mask |= 1 << (q * 2);
					if (cell[q] == 0)
						mask |= 1 << (q * 2 + 1);
				}
				return mask;
			}
			std::unordered_map<uint64_t, Damaged> damaged;
			// Models replaced or dropped, kept a few frames: a frame still in
			// flight may draw them (every hit rebuilds its block's)
			std::deque<std::pair<int, Handle<IModel>>> retired;
			void Retire(Handle<IModel>& m) {
				if (m)
					retired.emplace_back(0, std::move(m));
				m = nullptr;
			}
			uint64_t damageOrder = 0;
			static uint64_t Key(const IntVector3& b) {
				return (static_cast<uint64_t>(b.x & 0xFFFF) << 32) |
				       (static_cast<uint64_t>(b.y & 0xFFFF) << 16) |
				       static_cast<uint64_t>(b.z & 0xFF);
			}

			// +x, -x, +y, -y, +z, -z: face f is axis f / 2, positive if f is even
			/** All of a block's map faces left out (or put back): the model is
			 * the whole block, so nothing of it is drawn twice. */
			bool HideFaces(const IntVector3& block, bool hidden) {
				bool all = true;
				for (const auto& o : kFaces)
					all = client.GetRenderer().SetMapFaceHidden(
					        block, IntVector3::Make(o[0], o[1], o[2]), hidden) &&
					      all;
				return all;
			}
			void Restore(Damaged& d) {
				HideFaces(d.block, false);
				Retire(d.model);
			}
			void RestoreAll() {
				for (auto& kv : damaged)
					Restore(kv.second);
				damaged.clear();
			}

			/** The block's model from what is cut out of it, in its colour as the
			 * map draws it (halved while damaged); the pits' walls and floors
			 * darker the deeper they are, as the map's corners are. */
			void Build(Damaged& d, uint32_t mapColor) {
				d.blockColor = mapColor;
				uint32_t col = mapColor & 0xFFFFFF;
				if ((mapColor >> 24) < 100)
					col = (col & 0xFEFEFE) >> 1;
				auto m = Handle<VoxelModel>::New(kCells, kCells, kCells);
				auto removedAt = [&](int x, int y, int z) {
					return x >= 0 && y >= 0 && z >= 0 && x < kCells && y < kCells && z < kCells &&
					       d.removed[x][y][z];
				};
				for (int x = 0; x < kCells; x++)
					for (int y = 0; y < kCells; y++)
						for (int z = 0; z < kCells; z++) {
							if (d.removed[x][y][z])
								continue;
							bool wall = false; // open to a pit
							for (const auto& o : kFaces)
								wall = wall || removedAt(x + o[0], y + o[1], z + o[2]);
							uint32_t v = col;
							if (wall) {
								// How far in from the block's surface
								const int depth =
								  std::min({x, y, z, kCells - 1 - x, kCells - 1 - y, kCells - 1 - z});
								v = Darken(col, depth == 0 ? 0.78F
								                           : std::max(0.3F, 0.6F - 0.1F * depth));
							}
							// Never quite black: a black voxel takes the custom colour
							if ((v & 0xFFFFFF) == 0)
								v = 0x010101;
							m->SetSolid(x, y, z, v);
						}
				// Voxel (0,0,0) from 0 to 1, not centred on 0 as models are by default
				m->SetOrigin(MakeVector3(0.5F, 0.5F, 0.5F));
				Retire(d.model);
				d.model = client.GetRenderer().CreateModel(*m);
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
			impl->RestoreAll();
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
			// Cut into the block where the renderer can leave its faces out
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
			const uint64_t key = ImpactMarksImpl::Key(block);
			auto it = impl->damaged.find(key);
			if (it == impl->damaged.end()) {
				if (!impl->HideFaces(block, true)) {
					impl->HideFaces(block, false); // a renderer that cannot: flat marks
					return false;
				}
				// The oldest goes when there are too many
				if (impl->damaged.size() >= kMaxBlocks) {
					auto oldest = impl->damaged.begin();
					for (auto i = impl->damaged.begin(); i != impl->damaged.end(); ++i)
						if (i->second.order < oldest->second.order)
							oldest = i;
					impl->Restore(oldest->second);
					impl->damaged.erase(oldest);
				}
				ImpactMarksImpl::Damaged d;
				d.block = block;
				// A hair larger than the block (0.1% each side), never smaller: the
				// next blocks draw no faces against it, so a crack at its edges
				// would be a slit through the wall. Every face of it is the model's,
				// so there is nothing for it to fight but the next blocks' faces in
				// the same planes, by too thin a strip to see.
				const float side = 1.002F / kCells;
				d.matrix = Matrix4::FromAxis(MakeVector3(side, 0, 0), MakeVector3(0, side, 0),
				                             MakeVector3(0, 0, side),
				                             MakeVector3(block) - MakeVector3(0.001F, 0.001F, 0.001F));
				it = impl->damaged.emplace(key, std::move(d)).first;
			}
			ImpactMarksImpl::Damaged& d = it->second;
			d.age = 0.0F;
			d.order = ++impl->damageOrder;

			// The crater, on the face hit: in that face's frame, `a` across it two
			// ways and into the block from it
			const int axis = normal.x != 0 ? 0 : normal.y != 0 ? 1 : 2;
			const int a1 = axis == 0 ? 1 : 0, a2 = axis == 2 ? 1 : 2;
			const bool positive = normal.x + normal.y + normal.z > 0;
			impl->seed = Hash(impl->seed + 0x9e3779b9U);
			const float radius = spade ? 2.0F + 0.6F * Noise(impl->seed, 1, 2)
			                           : 1.15F + 0.45F * Noise(impl->seed, 3, 4);
			// Kept in from an edge whose other face has a block against it, so
			// no pit opens onto it; an edge open to the air may be reached, but
			// its own cells stay (below), so a pit there stops a cell short.
			const int open = ImpactMarksImpl::OpenFaces(*map, block);
			d.openFaces = open;
			auto margin = [&](int q, bool high) {
				return (open & (1 << (q * 2 + (high ? 0 : 1)))) ? 1.5F : radius + 0.4F;
			};
			const Vector3 local = (hitPos - MakeVector3(block)) * static_cast<float>(kCells);
			auto centre = [&](int q) {
				const float lo = margin(q, false), hi = kCells - margin(q, true);
				return lo <= hi ? std::max(lo, std::min(hi, local[q])) : kCells * 0.5F;
			};
			const float c1 = centre(a1);
			const float c2 = centre(a2);
			const int deepest = spade ? kMaxDepth - 1 : kMaxDepth - 2; // layers below the face's
			for (int i = 0; i < kCells; i++)
				for (int j = 0; j < kCells; j++) {
					const float di = i + 0.5F - c1, dj = j + 0.5F - c2;
					const float dist =
					  std::sqrt(di * di + dj * dj) + (Noise(impl->seed, i, j) - 0.5F) * 0.9F;
					for (int k = 0; k <= deepest; k++) {
						if (dist >= radius * (1.0F - 0.38F * static_cast<float>(k)))
							continue;
						int cell[3];
						cell[axis] = positive ? kCells - 1 - k : k;
						cell[a1] = i;
						cell[a2] = j;
						// Never an edge or a corner (a cell on two faces or three),
						// nor on a face with a block against it
						const int faces = ImpactMarksImpl::CellFaces(cell);
						int onSurface = 0;
						for (int q = 0; q < 3; q++)
							onSurface += (cell[q] == 0 || cell[q] == kCells - 1) ? 1 : 0;
						if (onSurface >= 2 || (faces & ~open) != 0)
							continue;
						d.removed[cell[0]][cell[1]][cell[2]] = true;
					}
				}
			impl->Build(d, map->GetColorWrapped(block.x, block.y, block.z));
			return true;
		}

		void ImpactMarks::Update(float dt) {
			// Switched off: the faces go back at once; left out with nothing
			// drawn in their place, they would be holes through the walls
			if (!(bool)cg_impactMarks && !impl->damaged.empty())
				impl->RestoreAll();
			auto map = impl->client.GetWorld() ? impl->client.GetWorld()->GetMap()
			                                   : Handle<GameMap>();
			if (!map) {
				impl->marks.clear();
				impl->damaged.clear(); // its map, and the hidden faces, are gone
				return;
			}
			// Damaged blocks: gone with the block, put back after a while, and
			// rebuilt in the block's new colour when it is damaged or healed
			for (auto it = impl->damaged.begin(); it != impl->damaged.end();) {
				auto& d = it->second;
				d.age += dt;
				const IntVector3& b = d.block;
				const bool solid = map->IsSolidWrapped(b.x, b.y, b.z);
				const uint32_t col = solid ? map->GetColorWrapped(b.x, b.y, b.z) : 0;
				if (!solid || d.age > kDamageLifetime ||
				    (col & 0xFFFFFF) != (d.blockColor & 0xFFFFFF)) {
					impl->Restore(d);
					it = impl->damaged.erase(it);
					continue;
				}
				// A block put against a face cut into: that face filled back in,
				// or it would be a slit through to nothing (the map draws no
				// face between the two)
				const int open = ImpactMarksImpl::OpenFaces(*map, b);
				const int closed = d.openFaces & ~open;
				d.openFaces = open;
				bool refilled = false;
				if (closed) {
					for (int x = 0; x < kCells; x++)
						for (int y = 0; y < kCells; y++)
							for (int z = 0; z < kCells; z++) {
								const int cell[3] = {x, y, z};
								if (d.removed[x][y][z] && (ImpactMarksImpl::CellFaces(cell) & closed)) {
									d.removed[x][y][z] = false;
									refilled = true;
								}
							}
				}
				if (col != d.blockColor || refilled)
					impl->Build(d, col); // its health changed, or a face was filled
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
				impl->RestoreAll();
				return;
			}
			for (auto& rm : impl->retired)
				rm.first++;
			while (!impl->retired.empty() && impl->retired.front().first > 4)
				impl->retired.pop_front();
			IRenderer& r = impl->client.GetRenderer();
			ModelRenderParam param;
			param.drawOutline = false;
			// The damaged blocks stand in for the map's own, in the shadows too:
			// the map's shadow mesh has left their faces out as well
			param.castShadow = true;
			for (const auto& kv : impl->damaged) {
				if (!kv.second.model)
					continue;
				param.matrix = kv.second.matrix;
				r.RenderModel(*kv.second.model, param);
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
