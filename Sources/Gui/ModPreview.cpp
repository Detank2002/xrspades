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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>

#include "ModOverlay.h"
#include "ModPreview.h"
#include "ModsScreenHelper.h"
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/VoxelModel.h>
#include <Core/ZipFileSystem.h>

namespace spades {
	namespace gui {
		namespace {
			// The stock model for each slot
			const char* const kStockModels[kNumSkinSlots] = {
			  "Models/Weapons/Spade/Spade.kv6",   "Models/Weapons/Block/Block.kv6",
			  "Models/Weapons/Grenade/Grenade.kv6", "Models/Weapons/Rifle/Weapon.kv6",
			  "Models/Weapons/SMG/Weapon.kv6",     "Models/Weapons/Shotgun/Weapon.kv6"};

			std::string Lower(std::string s) {
				for (char& c : s)
					c = c == '\\' ? '/'
					              : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return s;
			}
			bool EndsWith(const std::string& s, const char* tail) {
				const std::size_t n = std::char_traits<char>::length(tail);
				return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
			}

			// How well a .kv6 in a slot's folder stands for the mod: the stock
			// model's own name first, then any but the small parts
			int ModelRank(const std::string& lowerPath, int slot) {
				if (lowerPath == Lower(kStockModels[slot]))
					return 3;
				const std::string name = lowerPath.substr(lowerPath.rfind('/') + 1);
				if (name == "weapon.kv6" || name == "spade.kv6" || name == "grenade.kv6" ||
				    name == "block.kv6")
					return 2;
				for (const char* part : {"casing", "magazine", "sight", "pin", "pump", "charm",
				                         "nomagazine", "nopump", "pickaxe", "shell", "bullet"})
					if (name.find(part) != std::string::npos)
						return 0;
				return 1;
			}

			std::string SafeFileName(const std::string& s) {
				std::string out;
				for (char c : s)
					out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
					         ? c
					         : '_';
				return out.substr(0, 80);
			}
		} // namespace

		bool ModSkinInfo::HasAny() const {
			for (bool b : slots)
				if (b)
					return true;
			return false;
		}

		ModSkinInfo ScanModSkins(const std::string& mod) {
			ModSkinInfo info;
			if (mod.empty()) {
				for (int i = 0; i < kNumSkinSlots; i++) {
					info.slots[i] = true;
					info.model[i] = kStockModels[i];
				}
				return info;
			}
			int rank[kNumSkinSlots];
			std::fill(rank, rank + kNumSkinSlots, -1);
			for (const std::string& pak : ModsScreenHelper::GetModPakPaths({mod})) {
				try {
					auto stream = FileManager::OpenForReading(pak.c_str());
					ZipFileSystem zip(stream.get());
					for (const std::string& file : zip.GetAllFiles()) {
						const int slot = SkinSlotOfPath(file);
						if (slot < 0)
							continue;
						info.slots[slot] = true;
						const std::string lower = Lower(file);
						if (lower.compare(0, 15, "models/weapons/") != 0 ||
						    !EndsWith(lower, ".kv6"))
							continue;
						const int r = ModelRank(lower, slot);
						if (r > rank[slot]) {
							rank[slot] = r;
							info.model[slot] = file;
							info.modelPak[slot] = pak;
						}
					}
				} catch (const std::exception& ex) {
					SPLog("Mod preview: cannot read %s: %s", pak.c_str(), ex.what());
				}
			}
			return info;
		}

		Handle<Bitmap> RenderVoxelPreview(VoxelModel& m, int width, int height) {
			// Drawn at twice the size, then each 2x2 averaged
			constexpr int kSuper = 2;
			const int W = width * kSuper, H = height * kSuper;
			const int mw = m.GetWidth(), mh = m.GetHeight(), md = m.GetDepth();

			// The longer of x and y goes across; z is down in a model
			const bool alongX = mw >= mh;
			const float ya = 0.45F, pa = 0.38F; // turned and tilted, radians
			const float ca = std::cos(ya), sa = std::sin(ya), cb = std::cos(pa),
			            sb = std::sin(pa);
			const float cx = mw * 0.5F, cy = mh * 0.5F, cz = md * 0.5F;
			auto project = [&](float x, float y, float z, float& sx, float& sy, float& depth) {
				const float u = alongX ? x - cx : y - cy;
				const float v = alongX ? y - cy : cx - x;
				const float h = z - cz;
				sx = u * ca + v * sa;
				const float dz = -u * sa + v * ca; // + towards the eye
				sy = h * cb + dz * sb;
				depth = dz * cb - h * sb;
			};

			// The surface voxels, and how far they reach on the picture
			struct Splat {
				float x, y, depth;
				uint32_t color;
			};
			std::vector<Splat> splats;
			float x0 = 1e9F, x1 = -1e9F, y0 = 1e9F, y1 = -1e9F;
			for (int x = 0; x < mw; x++)
				for (int y = 0; y < mh; y++)
					for (int z = 0; z < md; z++) {
						if (!m.IsSolid(x, y, z))
							continue;
						const bool up = !m.IsSolid(x, y, z - 1);
						const bool down = !m.IsSolid(x, y, z + 1);
						const bool xp = !m.IsSolid(x + 1, y, z), xn = !m.IsSolid(x - 1, y, z);
						const bool yp = !m.IsSolid(x, y + 1, z), yn = !m.IsSolid(x, y - 1, z);
						if (!(up || down || xp || xn || yp || yn))
							continue;
						// Lit by the face it shows most: the top brightest, the
						// side towards the eye next, the far ones darkest
						const bool towards = alongX ? yp : xn;
						const bool across = alongX ? (xn || xp) : (yn || yp);
						const float light = up ? 1.0F : towards ? 0.82F : across ? 0.7F : 0.55F;
						uint32_t c = m.GetColor(x, y, z) & 0xFFFFFF;
						if (c == 0)
							c = 0x707070; // the team's colour in a game
						const auto ch = [&](int shift) {
							return static_cast<uint32_t>(
							  std::min(255.0F, ((c >> shift) & 0xFF) * light + 0.5F));
						};
						Splat s;
						project(x + 0.5F, y + 0.5F, z + 0.5F, s.x, s.y, s.depth);
						s.color = ch(0) | (ch(8) << 8) | (ch(16) << 16);
						splats.push_back(s);
						x0 = std::min(x0, s.x);
						x1 = std::max(x1, s.x);
						y0 = std::min(y0, s.y);
						y1 = std::max(y1, s.y);
					}

			Handle<Bitmap> bmp = Handle<Bitmap>::New(width, height);
			uint32_t* out = bmp->GetPixels();
			std::fill(out, out + width * height, 0u);
			if (splats.empty())
				return bmp;

			// Fit, a voxel's width round it
			const float scale = std::min((W * 0.94F) / (x1 - x0 + 1.6F), (H * 0.9F) / (y1 - y0 + 1.6F));
			const float ox = W * 0.5F - (x0 + x1) * 0.5F * scale;
			const float oy = H * 0.5F - (y0 + y1) * 0.5F * scale;
			const int half = std::max(1, static_cast<int>(std::ceil(scale * 0.62F)));
			std::vector<float> zbuf(static_cast<std::size_t>(W) * H,
			                        -std::numeric_limits<float>::infinity());
			std::vector<uint32_t> col(static_cast<std::size_t>(W) * H, 0u);
			for (const Splat& s : splats) {
				const int px = static_cast<int>(s.x * scale + ox);
				const int py = static_cast<int>(s.y * scale + oy);
				for (int y = std::max(0, py - half); y < std::min(H, py + half); y++)
					for (int x = std::max(0, px - half); x < std::min(W, px + half); x++) {
						const std::size_t i = static_cast<std::size_t>(y) * W + x;
						if (s.depth > zbuf[i]) {
							zbuf[i] = s.depth;
							col[i] = s.color | 0xFF000000u;
						}
					}
			}
			for (int y = 0; y < height; y++)
				for (int x = 0; x < width; x++) {
					uint32_t r = 0, g = 0, b = 0, n = 0;
					for (int j = 0; j < kSuper; j++)
						for (int i = 0; i < kSuper; i++) {
							const uint32_t c =
							  col[static_cast<std::size_t>(y * kSuper + j) * W + x * kSuper + i];
							if (c >> 24) {
								r += c & 0xFF;
								g += (c >> 8) & 0xFF;
								b += (c >> 16) & 0xFF;
								n++;
							}
						}
					if (n == 0)
						continue;
					const uint32_t a = n * 255 / (kSuper * kSuper);
					out[y * width + x] = (r / n) | ((g / n) << 8) | ((b / n) << 16) | (a << 24);
				}
			return bmp;
		}

		ModPreviewCache::ModPreviewCache() {}
		ModPreviewCache::~ModPreviewCache() {}

		const ModSkinInfo& ModPreviewCache::GetInfo(const std::string& mod) {
			auto it = infos.find(mod);
			if (it == infos.end())
				it = infos.emplace(mod, ScanModSkins(mod)).first;
			return it->second;
		}

		int ModPreviewCache::FirstModelSlot(const std::string& mod) {
			const ModSkinInfo& info = GetInfo(mod);
			for (int i = 0; i < kNumSkinSlots; i++)
				if (!info.model[i].empty())
					return i;
			return -1;
		}

		client::IImage* ModPreviewCache::Get(client::IRenderer& renderer, const std::string& mod,
		                                     SkinSlot slot) {
			const int si = static_cast<int>(slot);
			const std::string key = mod + "\n" + std::to_string(si);
			auto it = entries.find(key);
			if (it != entries.end())
				return it->second.image.GetPointerOrNull();
			const ModSkinInfo& info = GetInfo(mod);
			if (info.model[si].empty()) {
				entries[key].failed = true;
				return nullptr;
			}
			if (frameBudget <= 0)
				return nullptr;
			frameBudget--;

			Entry& e = entries[key];
			constexpr int kW = 192, kH = 96;
			// Kept by name and size: a mod changed on disk is drawn again
			std::string cacheFile;
			if (!mod.empty())
				cacheFile = "ModPreviews/" + SafeFileName(mod) + "-" + std::to_string(si) + "-v2-" +
				            std::to_string(Handle<ModsScreenHelper>::New()->GetModTotalSize(mod)) +
				            ".png";
			Handle<Bitmap> bmp;
			if (!cacheFile.empty() && FileManager::FileExists(cacheFile.c_str())) {
				try {
					bmp = Bitmap::Load(cacheFile);
				} catch (const std::exception&) {
				}
			}
			if (!bmp) {
				try {
					std::unique_ptr<IStream> stream;
					std::unique_ptr<IStream> pakStream;
					std::unique_ptr<ZipFileSystem> zip;
					if (mod.empty()) {
						stream = ModOverlay::OpenStock(info.model[si].c_str());
					} else {
						pakStream = FileManager::OpenForReading(info.modelPak[si].c_str());
						zip.reset(new ZipFileSystem(pakStream.get()));
						stream = zip->OpenForReading(info.model[si].c_str());
					}
					Handle<VoxelModel> model = VoxelModel::LoadKV6(*stream);
					bmp = RenderVoxelPreview(*model, kW, kH);
					if (!cacheFile.empty()) {
						try {
							// The PNG writer turns rows over (it is made for screenshots, read
							// bottom row first) and the reader doesn't, so a turned-over copy
							// is written, to load back the right way up
							Handle<Bitmap> upright = Handle<Bitmap>::New(kW, kH);
							for (int y = 0; y < kH; y++)
								std::copy_n(bmp->GetPixels() + (kH - 1 - y) * kW, kW,
								            upright->GetPixels() + y * kW);
							upright->Save(cacheFile);
						} catch (const std::exception& ex) {
							SPLog("Mod preview not kept: %s: %s", cacheFile.c_str(), ex.what());
						}
					}
				} catch (const std::exception& ex) {
					SPLog("Mod preview of %s (%s) failed: %s", mod.c_str(), info.model[si].c_str(),
					      ex.what());
					e.failed = true;
					return nullptr;
				}
			}
			e.image = renderer.CreateImage(*bmp);
			return e.image.GetPointerOrNull();
		}
	} // namespace gui
} // namespace spades
