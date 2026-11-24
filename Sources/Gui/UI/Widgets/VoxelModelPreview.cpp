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
#include <array>
#include <cmath>
#include <limits>

#include <Core/Exception.h>
#include <Core/VoxelModel.h>

namespace spades {
	namespace gui {
		namespace ui {
			namespace {
				const Vector3 kNormals[] = {{-1, 0, 0}, {1, 0, 0},  {0, -1, 0},
				                            {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
				// A pathological checkerboard must not stall the game or allocate an
				// unbounded mesh. Real weapons are far below this, with no loss of detail.
				constexpr std::size_t kMaxFaces = 100000;
			} // namespace

			VoxelModelPreview::VoxelModelPreview(const VoxelModel& model) {
				std::array<int, 3> lo{model.GetWidth(), model.GetHeight(), model.GetDepth()};
				std::array<int, 3> hi{0, 0, 0};
				for (int x = 0; x < model.GetWidth(); ++x)
					for (int y = 0; y < model.GetHeight(); ++y) {
						const auto bits = model.GetSolidBitsAtUnchecked(x, y);
						for (int z = 0; z < model.GetDepth(); ++z) {
							if (!((bits >> z) & 1))
								continue;
							const int p[] = {x, y, z};
							for (int a = 0; a < 3; ++a) {
								lo[a] = std::min(lo[a], p[a]);
								hi[a] = std::max(hi[a], p[a] + 1);
							}
						}
					}
				if (hi[0] == 0)
					return;
				halfSize =
				  MakeVector3(float(hi[0] - lo[0]), float(hi[1] - lo[1]), float(hi[2] - lo[2])) *
				  0.5F;
				const auto center =
				  MakeVector3(float(lo[0]), float(lo[1]), float(lo[2])) + halfSize;

				// Greedy rectangles on each surface plane preserve every silhouette,
				// gap and surface color, including on high-resolution custom models.
				for (int dir = 0; dir < 6; ++dir) {
					const int axis = dir / 2, u = (axis + 1) % 3, v = (axis + 2) % 3;
					const int w = hi[u] - lo[u], h = hi[v] - lo[v];
					std::vector<std::uint32_t> mask(w * h);
					for (int slice = lo[axis]; slice < hi[axis]; ++slice) {
						for (int y = 0; y < h; ++y)
							for (int x = 0; x < w; ++x) {
								int p[3];
								p[axis] = slice;
								p[u] = x + lo[u];
								p[v] = y + lo[v];
								auto& color = mask[x + y * w];
								color = 0;
								if (!model.IsSolid(p[0], p[1], p[2]))
									continue;
								const auto rgb = model.GetColorUnchecked(p[0], p[1], p[2]);
								p[axis] += (dir & 1) ? 1 : -1;
								if (!model.IsSolid(p[0], p[1], p[2]))
									color = (rgb & 0xFFFFFF) | 0x1000000;
							}
						for (int y = 0; y < h; ++y)
							for (int x = 0; x < w;) {
								const auto color = mask[x + y * w];
								if (color == 0) {
									++x;
									continue;
								}
								int rw = 1, rh = 1;
								while (x + rw < w && mask[x + rw + y * w] == color)
									++rw;
								for (; y + rh < h; ++rh) {
									int n = 0;
									while (n < rw && mask[x + n + (y + rh) * w] == color)
										++n;
									if (n != rw)
										break;
								}
								for (int dy = 0; dy < rh; ++dy)
									std::fill_n(mask.begin() + x + (y + dy) * w, rw, 0);
								float p[3], du[3]{}, dv[3]{};
								p[axis] = float(slice + (dir & 1));
								p[u] = float(lo[u] + x);
								p[v] = float(lo[v] + y);
								du[u] = float(rw);
								dv[v] = float(rh);
								if (faces.size() == kMaxFaces)
									SPRaise("Preview surface is too complex");
								faces.push_back({MakeVector3(p[0], p[1], p[2]) - center,
								                 MakeVector3(du[0], du[1], du[2]),
								                 MakeVector3(dv[0], dv[1], dv[2]), color & 0xFFFFFF,
								                 dir});
								x += rw;
							}
					}
				}
			}

			std::vector<VoxelModelPreview::Quad> VoxelModelPreview::Project(const AABB2& bounds,
			                                                                float yaw) const {
				std::vector<Quad> result;
				if (faces.empty() || bounds.GetWidth() <= 0 || bounds.GetHeight() <= 0 ||
				    !std::isfinite(yaw))
					return result;
				const float s = std::sin(yaw), c = std::cos(yaw);
				constexpr float tilt = 0.16F;
				const float st = std::sin(tilt), ct = std::cos(tilt);
				const Vector3 right = MakeVector3(c, s, 0);
				const Vector3 down = MakeVector3(s * st, -c * st, ct);
				const Vector3 toward = MakeVector3(s * ct, -c * ct, -st);
				const float radius = std::hypot(halfSize.x, halfSize.y);
				const float height = halfSize.z * ct + radius * st;
				// Stable full-turn framing, using occupied bounds rather than the
				// model pivot/padding. A shallow studio view gives rifles more room.
				const float scale = 0.94F * std::min(bounds.GetWidth() / (2 * radius),
				                                     bounds.GetHeight() / (2 * height));
				const Vector2 center = (bounds.min + bounds.max) * 0.5F;
				auto project = [&](Vector3 p) {
					return MakeVector2(Vector3::Dot(p, right), Vector3::Dot(p, down)) * scale;
				};
				result.reserve(faces.size() / 2);
				for (const Face& face : faces) {
					const auto normal = kNormals[face.direction];
					const float facing = Vector3::Dot(normal, toward);
					if (facing <= 0.0001F)
						continue;
					const float light =
					  std::max(0.0F, -0.4F * Vector3::Dot(normal, right) -
					                   0.6F * Vector3::Dot(normal, down) + 0.69F * facing);
					const auto channel = [&](int shift) {
						// UI images already contain display/sRGB bytes. A per-channel
						// gamma lift here made dark metals pale and desaturated paint.
						// Neutral scalar lighting keeps the KV6 palette's hue/contrast.
						// Black KV6 team-color voxels use neutral charcoal in the preview.
						const float value =
						  face.color == 0 ? 0.16F : float((face.color >> shift) & 255) / 255.0F;
						return value * (0.76F + 0.24F * light);
					};
					result.push_back(
					  {center + project(face.origin), project(face.axisU), project(face.axisV),
					   MakeVector4(channel(0), channel(8), channel(16), 1),
					   Vector3::Dot(face.origin, toward), Vector3::Dot(face.axisU, toward),
					   Vector3::Dot(face.axisV, toward)});
				}
				return result;
			}

			std::vector<std::uint32_t> VoxelModelPreview::Rasterize(int width, int height,
			                                                        float yaw) const {
				if (width < 1 || height < 1 || width > 512 || height > 512)
					SPRaise("Invalid preview image size");
				// Four coverage/depth samples per output pixel, including on SW.
				const int w = width * 2, h = height * 2;
				std::vector<std::uint32_t> pixels(w * h, 0);
				std::vector<float> depth(w * h, -std::numeric_limits<float>::infinity());
				for (const Quad& q : Project(AABB2(0, 0, float(w), float(h)), yaw)) {
					const float det = q.axisU.x * q.axisV.y - q.axisU.y * q.axisV.x;
					if (std::fabs(det) < 0.00001F)
						continue;
					const Vector2 corners[] = {q.origin, q.origin + q.axisU, q.origin + q.axisV,
					                           q.origin + q.axisU + q.axisV};
					Vector2 lo = corners[0], hi = corners[0];
					for (const auto& p : corners) {
						lo.x = std::min(lo.x, p.x);
						lo.y = std::min(lo.y, p.y);
						hi.x = std::max(hi.x, p.x);
						hi.y = std::max(hi.y, p.y);
					}
					const int x0 = std::max(0, int(std::floor(lo.x)));
					const int y0 = std::max(0, int(std::floor(lo.y)));
					const int x1 = std::min(w, int(std::ceil(hi.x)));
					const int y1 = std::min(h, int(std::ceil(hi.y)));
					const auto rgba = 0xFF000000U | std::uint32_t(q.color.x * 255 + 0.5F) |
					                  (std::uint32_t(q.color.y * 255 + 0.5F) << 8) |
					                  (std::uint32_t(q.color.z * 255 + 0.5F) << 16);
					for (int y = y0; y < y1; ++y)
						for (int x = x0; x < x1; ++x) {
							const auto p = MakeVector2(x + 0.5F, y + 0.5F) - q.origin;
							const float u = (p.x * q.axisV.y - p.y * q.axisV.x) / det;
							const float v = (q.axisU.x * p.y - q.axisU.y * p.x) / det;
							if (u < 0 || v < 0 || u > 1 || v > 1)
								continue;
							const float z = q.depth + u * q.depthU + v * q.depthV;
							const int i = x + y * w;
							if (z > depth[i]) {
								depth[i] = z;
								pixels[i] = rgba;
							}
						}
				}
				std::vector<std::uint32_t> result(width * height, 0);
				for (int y = 0; y < height; ++y)
					for (int x = 0; x < width; ++x) {
						unsigned count = 0, r = 0, g = 0, b = 0;
						for (int dy = 0; dy < 2; ++dy)
							for (int dx = 0; dx < 2; ++dx) {
								const auto p = pixels[x * 2 + dx + (y * 2 + dy) * w];
								if (p >> 24) {
									++count;
									r += p & 255;
									g += (p >> 8) & 255;
									b += (p >> 16) & 255;
								}
							}
						if (count)
							result[x + y * width] = (count * 255 / 4 << 24) | (r / count) |
							                        (g / count << 8) | (b / count << 16);
					}
				return result;
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
