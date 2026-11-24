/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <cctype>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "IFont.h"
#include "IRenderer.h"
#include <Core/Bitmap.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>

namespace spades {
	namespace client {

		class IImage;

		struct FallbackFontState;

		// Font descriptions are shared globally, but atlas images belong to
		// a particular renderer. Its fonts share one refreshable image set.
		class FallbackFontRenderer : public RefCountedObject {
			IRenderer* renderer;
			std::shared_ptr<FallbackFontState> state;
			bool roundSize;

			struct FindResult {
				IImage* img;
				float size;
				float sizeInverse;
				float advance;
				int x, y, w, h;
				int offX, offY;
				FindResult()
					: img(nullptr),
					  size(0.0F),
					  sizeInverse(0.0F),
					  advance(0.0F),
					  x(0),
					  y(0),
					  w(0),
					  h(0),
					  offX(0),
					  offY(0) {}
			};
			FindResult FindGlyph(uint32_t);

		protected:
			~FallbackFontRenderer();

		public:
			FallbackFontRenderer(IRenderer* renderer, std::shared_ptr<FallbackFontState> state);

			void Draw(uint32_t unicode, Vector2 offset, float size, Vector4 color);
			float Measure(uint32_t unicode, float size);
		};

		struct FallbackFont {
			struct GlyphInfo {
				uint32_t unicode;
				uint16_t x, y;
				uint8_t w, h;
				uint16_t advance;
				int16_t offX, offY;
			};
			std::string imagePath;
			std::unordered_map<uint32_t, GlyphInfo> glyphs;
			float fontSize;
			float fontSizeInverse;

			FallbackFont(const std::string& path) {
				std::unique_ptr<IStream> s(FileManager::OpenForReading(path.c_str()));
				char buf[17];
				buf[16] = 0;
				if (s->Read(buf, 16) < 16)
					SPRaise("Reading %s: file truncated", path.c_str());
				if (std::strcmp(buf, "OpenSpadesFontFl"))
					SPRaise("Reading %s: font file corrupted", path.c_str());

				uint32_t numGlyphs;
				if (s->Read(&numGlyphs, 4) < 4)
					SPRaise("Reading %s: file truncated", path.c_str());

				uint32_t fontSize;
				if (s->Read(&fontSize, 4) < 4)
					SPRaise("Reading %s: file truncated", path.c_str());

				if (numGlyphs > 0x110000 || fontSize == 0 || fontSize > 4096)
					SPRaise("Reading %s: invalid font dimensions", path.c_str());
				this->fontSize = static_cast<float>(fontSize);
				fontSizeInverse = 1.0F / fontSize;

				std::vector<GlyphInfo> infos;
				infos.resize(static_cast<std::size_t>(numGlyphs));
				size_t siz = infos.size() * sizeof(GlyphInfo);
				if (s->Read(infos.data(), siz) < siz) {
					SPRaise("Reading %s: file truncated: "
						"trying to read %zu byte(s)", path.c_str(), siz);
				}

				for (const auto& i : infos)
					glyphs[i.unicode] = i;

				// remove .ospfont and look for the atlas image (PNG preferred, TGA for legacy mods)
				std::string p = path.substr(0, path.size() - 8);
				imagePath = p + ".png";
				if (FileManager::FileExists(imagePath.c_str()))
					return;
				imagePath = p + ".tga";
				if (FileManager::FileExists(imagePath.c_str()))
					return;

				SPRaise("Reading %s: failed to find %s.(png|tga)", path.c_str(), p.c_str());
			}
		};

		// One immutable description/bitmap snapshot and one set of GPU images
		// per renderer, shared by its fonts. Reload builds all replacements
		// before changing anything the current UI can still be drawing with.
		struct FallbackFontData {
			std::vector<FallbackFont> fonts;
			std::vector<Handle<Bitmap>> atlases;

			FallbackFontData() {
				auto files = FileManager::EnumFiles("Gfx/Fonts");
				std::unordered_set<std::string> seen;
				for (std::string file : files) {
					std::string key = file;
					for (char& c : key)
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					if (key.size() >= 5 && key.compare(key.size() - 5, 5, ".weak") == 0) {
						file.resize(file.size() - 5);
						key.resize(key.size() - 5);
					}
					if (key.size() < 8 || key.compare(key.size() - 8, 8, ".ospfont") != 0 ||
					    !seen.insert(key).second)
						continue;
					fonts.emplace_back("Gfx/Fonts/" + file);
					auto bitmap = Bitmap::Load(fonts.back().imagePath);
					for (const auto& glyph : fonts.back().glyphs) {
						const auto& g = glyph.second;
						// Empty glyphs still carry an advance, but have no atlas
						// rectangle. Unifont uses x=y=65535 for spaces, for example.
						if (g.w == 0 || g.h == 0)
							continue;
						if (int(g.x) + int(g.w) > bitmap->GetWidth() ||
						    int(g.y) + int(g.h) > bitmap->GetHeight())
							SPRaise("Reading %s: glyph outside font atlas", file.c_str());
					}
					atlases.push_back(std::move(bitmap));
				}
			}

			std::vector<Handle<IImage>> CreateImages(IRenderer& renderer) const {
				std::vector<Handle<IImage>> images;
				for (const auto& bitmap : atlases) {
					auto image = renderer.CreateImage(*bitmap);
					if (!image)
						SPRaise("Cannot create fallback font atlas");
					images.push_back(std::move(image));
				}
				return images;
			}
		};

		struct FallbackFontState {
			std::shared_ptr<FallbackFontData> data;
			std::vector<Handle<IImage>> images;
		};

		class FallbackFontManager {
			std::shared_ptr<FallbackFontData> data = std::make_shared<FallbackFontData>();
			std::unordered_map<IRenderer*, std::weak_ptr<FallbackFontState>> renderers;

		public:
			static FallbackFontManager* GetInstance() {
				static auto* instance = new FallbackFontManager();
				return instance;
			}

			void Reload() {
				auto next = std::make_shared<FallbackFontData>();
				struct Staged {
					std::shared_ptr<FallbackFontState> state;
					std::vector<Handle<IImage>> images;
				};
				std::vector<Staged> staged;
				for (auto it = renderers.begin(); it != renderers.end();) {
					if (auto state = it->second.lock()) {
						staged.push_back({std::move(state), next->CreateImages(*it->first)});
						++it;
					} else {
						it = renderers.erase(it);
					}
				}
				// No I/O or allocations after this point.
				for (auto& entry : staged) {
					entry.state->images.swap(entry.images);
					entry.state->data = next;
				}
				data.swap(next);
			}

			FallbackFontRenderer* CreateRenderer(IRenderer* renderer) {
				auto& weak = renderers[renderer];
				auto state = weak.lock();
				if (!state) {
					state = std::make_shared<FallbackFontState>();
					state->data = data;
					state->images = data->CreateImages(*renderer);
					weak = state;
				}
				return new FallbackFontRenderer(renderer, std::move(state));
			}
		};

		FallbackFontRenderer::FallbackFontRenderer(IRenderer* renderer,
		                                           std::shared_ptr<FallbackFontState> state)
		    : renderer(renderer), state(std::move(state)), roundSize(renderer->IsRendererSW()) {}

		FallbackFontRenderer::~FallbackFontRenderer() = default;

		FallbackFontRenderer::FindResult FallbackFontRenderer::FindGlyph(uint32_t unicode) {
			FindResult result;
			for (size_t i = 0; i < state->data->fonts.size(); i++) {
				const auto* font = &state->data->fonts[i];
				const auto& it = font->glyphs.find(unicode);
				if (it == font->glyphs.end())
					continue;
				result.img = state->images[i].GetPointerOrNull();
				result.size = font->fontSize;
				result.sizeInverse = font->fontSizeInverse;
				result.advance = it->second.advance;
				result.x = it->second.x;
				result.y = it->second.y;
				result.w = it->second.w;
				result.h = it->second.h;
				result.offX = it->second.offX;
				result.offY = it->second.offY;
				break;
			}
			return result;
		}

		void FallbackFontRenderer::Draw(uint32_t unicode, spades::Vector2 offset, float size,
										spades::Vector4 color) {
			renderer->SetColorAlphaPremultiplied(color);
			float x = offset.x;
			float y = offset.y;

			auto glyph = FindGlyph(unicode);
			if (glyph.img == nullptr) { // no glyph found! draw box
				// A null image draws a solid fill using the shared white image.
				renderer->DrawImage(nullptr, AABB2(x, y, size, 1.0F));
				renderer->DrawImage(nullptr, AABB2(x, y + size - 1.0F, size, 1.0F));
				renderer->DrawImage(nullptr, AABB2(x, y + 1.0F, 1.0F, size - 2.0F));
				renderer->DrawImage(nullptr, AABB2(x + size - 1.0F, y + 1.0F, 1.0F, size - 2.0F));
				return;
			}

			if (glyph.w == 0 || glyph.h == 0)
				return; // Empty glyphs advance the cursor without sampling the atlas.

			float scale = size * glyph.sizeInverse;
			if (roundSize || scale < 1.0F) {
				float newScale = std::max(1.0F, floorf(scale));
				// vertical-align: baseline
				offset.y += (scale - newScale) * glyph.size;
				scale = newScale;
			}

			AABB2 inRect(
				static_cast<float>(glyph.x),
				static_cast<float>(glyph.y),
				static_cast<float>(glyph.w),
				static_cast<float>(glyph.h)
			);
			AABB2 outRect(
				static_cast<float>(glyph.offX),
				static_cast<float>(glyph.offY),
				static_cast<float>(glyph.w),
				static_cast<float>(glyph.h)
			);

			// margin to make the border completely transparent
			// (considering the OpenGL's linear interpolation)
			if (!roundSize) {
				inRect.min.x -= 0.5F;
				inRect.min.y -= 0.5F;
				outRect.min.x -= 0.5F;
				outRect.min.y -= 0.5F;
				inRect.max.x += 0.5F;
				inRect.max.y += 0.5F;
				outRect.max.x += 0.5F;
				outRect.max.y += 0.5F;
			}

			outRect.min *= scale;
			outRect.max *= scale;
			outRect.min += offset;
			outRect.max += offset;

			renderer->DrawImage(glyph.img, outRect, inRect);
		}

		float FallbackFontRenderer::Measure(uint32_t unicode, float size) {
			auto glyph = FindGlyph(unicode);
			if (glyph.img == nullptr)
				return size + 1.0F; // no glyph found! draw box in the last resort

			float scale = size * glyph.sizeInverse;
			if (roundSize || scale < 1.0F) {
				float newScale = std::max(1.0F, floorf(scale));
				scale = newScale;
			}
			return glyph.advance * scale;
		}

		IFont::IFont(IRenderer* r) {
			fallback.Set(FallbackFontManager::GetInstance()->CreateRenderer(r), false);
		}

		void IFont::ReloadFallbackFonts() {
			FallbackFontManager::GetInstance()->Reload();
		}

		IFont::~IFont() {} //---

		float IFont::MeasureFallback(uint32_t unicodeCodePoint, float size) {
			return fallback->Measure(unicodeCodePoint, size);
		}

		void IFont::DrawFallback(uint32_t unicodeCodePoint, spades::Vector2 offset, float size,
								 spades::Vector4 color) {
			fallback->Draw(unicodeCodePoint, offset, size, color);
		}

		void IFont::DrawShadow(const std::string& message, const Vector2& offset, float scale,
							   const Vector4& color, const Vector4& shadowColor) {
			Draw(message, offset + MakeVector2(1, 1), scale, shadowColor);
			Draw(message, offset, scale, color);
		}

		void IFont::DrawOutline(const std::string& message, const Vector2& offset, float scale,
							   const Vector4& color, const Vector4& outlineColor) {
			DrawShadow(message, offset, scale, color, outlineColor);
		}
	} // namespace client
} // namespace spades