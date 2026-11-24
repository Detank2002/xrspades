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

#include "VoxelModelPreview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <Client/IRenderer.h>
#include <Core/VoxelModel.h>

namespace spades {
	namespace gui {
		namespace ui {
			namespace {
				const IntVector3 kNormals[] = {{-1, 0, 0}, {1, 0, 0},  {0, -1, 0},
				                               {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
				constexpr std::size_t kMaxFaces = 20000;
			} // namespace

			VoxelModelPreview::VoxelModelPreview(const VoxelModel& model) {
				IntVector3 lo(model.GetWidth(), model.GetHeight(), model.GetDepth());
				IntVector3 hi(0, 0, 0);
				bool any = false;
				for (int x = 0; x < model.GetWidth(); ++x)
					for (int y = 0; y < model.GetHeight(); ++y) {
						const auto bits = model.GetSolidBitsAtUnchecked(x, y);
						for (int z = 0; z < model.GetDepth(); ++z) {
							if (!((bits >> z) & 1))
								continue;
							any = true;
							lo.x = std::min(lo.x, x);
							lo.y = std::min(lo.y, y);
							lo.z = std::min(lo.z, z);
							hi.x = std::max(hi.x, x + 1);
							hi.y = std::max(hi.y, y + 1);
							hi.z = std::max(hi.z, z + 1);
						}
					}
				if (!any)
					return;

				const auto extent = hi - lo;
				// A thumbnail does not need sub-pixel voxels. Bound both the mesh
				// and the per-frame sort/draw work for unusually detailed mods.
				int stride = std::max(1, (std::max({extent.x, extent.y, extent.z}) + 95) / 96);
				for (;;) {
					const IntVector3 dim = (extent + (stride - 1)) / stride;
					halfSize = MakeVector3(float(dim.x), float(dim.y), float(dim.z)) * 0.5F;
					std::vector<std::uint32_t> colors(dim.x * dim.y * dim.z, 0);
					std::vector<bool> solid(colors.size(), false);
					auto index = [dim](int x, int y, int z) { return (x * dim.y + y) * dim.z + z; };
					for (int x = lo.x; x < hi.x; ++x)
						for (int y = lo.y; y < hi.y; ++y)
							for (int z = lo.z; z < hi.z; ++z) {
								if (!model.IsSolid(x, y, z))
									continue;
								const int i = index((x - lo.x) / stride, (y - lo.y) / stride,
								                    (z - lo.z) / stride);
								if (!solid[i])
									colors[i] = model.GetColorUnchecked(x, y, z);
								solid[i] = true;
							}
					auto occupied = [&](IntVector3 p) {
						return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < dim.x && p.y < dim.y &&
						       p.z < dim.z && solid[index(p.x, p.y, p.z)];
					};
					faces.clear();
					for (int x = 0; x < dim.x && faces.size() <= kMaxFaces; ++x)
						for (int y = 0; y < dim.y && faces.size() <= kMaxFaces; ++y)
							for (int z = 0; z < dim.z && faces.size() <= kMaxFaces; ++z) {
								if (!solid[index(x, y, z)])
									continue;
								const auto c = colors[index(x, y, z)] & 0xFFFFFF;
								const Vector3 color =
								  c == 0 ? MakeVector3(0.35F, 0.6F, 0.95F)
								         : MakeVector3(float(c & 255), float((c >> 8) & 255),
								                       float((c >> 16) & 255)) /
								             255.0F;
								for (int dir = 0; dir < 6; ++dir)
									if (!occupied(IntVector3(x, y, z) + kNormals[dir]))
										faces.push_back(
										  {MakeVector3(x + 0.5F, y + 0.5F, z + 0.5F) - halfSize,
										   color, dir});
							}
					if (faces.size() <= kMaxFaces)
						break;
					stride *= 2;
				}
			}

			std::vector<VoxelModelPreview::Quad> VoxelModelPreview::Project(const AABB2& bounds,
			                                                                float yaw) const {
				std::vector<Quad> result;
				if (faces.empty() || bounds.GetWidth() <= 0 || bounds.GetHeight() <= 0)
					return result;
				const float s = std::sin(yaw), c = std::cos(yaw);
				constexpr float tilt = 0.28F;
				const float st = std::sin(tilt), ct = std::cos(tilt);
				const Vector3 right = MakeVector3(c, s, 0);
				const Vector3 down = MakeVector3(s * st, -c * st, ct);
				const Vector3 toward = MakeVector3(s * ct, -c * ct, -st);
				const float radius = std::sqrt(halfSize.x * halfSize.x + halfSize.y * halfSize.y);
				const float height = halfSize.z * ct + radius * st;
				// Fixed scale over a complete turn, so long guns do not pulse as
				// they rotate, and no pivot/empty padding can push a model off-card.
				const float scale = 0.9F * std::min(bounds.GetWidth() / (2 * radius),
				                                    bounds.GetHeight() / (2 * height));
				const Vector2 center = (bounds.min + bounds.max) * 0.5F;
				auto project = [&](Vector3 p) {
					return MakeVector2(Vector3::Dot(p, right), Vector3::Dot(p, down)) * scale;
				};
				result.reserve(faces.size() / 2);
				for (const Face& face : faces) {
					const auto n = kNormals[face.direction];
					const Vector3 normal = MakeVector3(float(n.x), float(n.y), float(n.z));
					const float facing = Vector3::Dot(normal, toward);
					if (facing <= 0.001F)
						continue;
					Vector3 u, v;
					if (n.x != 0) {
						u = MakeVector3(0, 1, 0);
						v = MakeVector3(0, 0, 1);
					} else if (n.y != 0) {
						u = MakeVector3(1, 0, 0);
						v = MakeVector3(0, 0, 1);
					} else {
						u = MakeVector3(1, 0, 0);
						v = MakeVector3(0, 1, 0);
					}
					const Vector3 p = face.center + normal * 0.5F - (u + v) * 0.5F;
					const float lit =
					  std::max(0.0F, -0.35F * Vector3::Dot(normal, right) -
					                   0.6F * Vector3::Dot(normal, down) + 0.72F * facing);
					const Vector3 rgb = face.color * (0.5F + 0.5F * lit);
					result.push_back({center + project(p), project(u), project(v),
					                  MakeVector4(rgb.x, rgb.y, rgb.z, 1),
					                  Vector3::Dot(face.center + normal * 0.5F, toward)});
				}
				std::sort(result.begin(), result.end(),
				          [](const Quad& a, const Quad& b) { return a.depth < b.depth; });
				return result;
			}

			void VoxelModelPreview::Render(client::IRenderer& renderer, const AABB2& bounds,
			                               float yaw) const {
				for (const Quad& quad : Project(bounds, yaw)) {
					renderer.SetColorAlphaPremultiplied(quad.color);
					renderer.DrawImage(nullptr, quad.origin, quad.origin + quad.axisU,
					                   quad.origin + quad.axisV, AABB2(0, 0, 1, 1));
				}
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
