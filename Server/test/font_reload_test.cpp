// Real FreeType, FontManager, fallback descriptions, glyph/atlas caches and
// existing font holders. Only the GPU and atlas image codec are headless fakes.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "skin_test_devices.h"
#include <Client/Fonts.h>
#include <Client/IImage.h>
#include <Core/FileManager.h>
#include <Core/IBitmapCodec.h>
#include <Core/IFileSystem.h>
#include <Core/MemoryStream.h>

using namespace spades;
using namespace spades::client;

namespace {
	void Check(bool ok, const char* what) {
		if (!ok)
			throw std::runtime_error(what);
	}
	std::string Lower(std::string s) {
		for (auto& c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}
	class Files : public IFileSystem {
	public:
		std::map<std::string, std::string> files;
		std::vector<std::string> EnumFiles(const char* path) override {
			std::vector<std::string> result;
			const auto prefix = Lower(std::string(path) + "/");
			for (const auto& file : files)
				if (file.first.compare(0, prefix.size(), prefix) == 0 &&
				    file.first.find('/', prefix.size()) == std::string::npos)
					result.push_back(file.first.substr(prefix.size()));
			return result;
		}
		std::unique_ptr<IStream> OpenForReading(const char* path) override {
			const auto& bytes = files.at(Lower(path));
			return std::make_unique<MemoryStream>(bytes.data(), bytes.size());
		}
		std::unique_ptr<IStream> OpenForWriting(const char*) override {
			throw std::runtime_error("unexpected write");
		}
		bool FileExists(const char* path) override { return files.count(Lower(path)) != 0; }
	};

	class Image : public IImage {
		int w, h;

	public:
		static int live;
		std::vector<std::uint32_t> pixels;
		explicit Image(Bitmap& b) : w(b.GetWidth()), h(b.GetHeight()), pixels(w * h) {
			++live;
			Update(b, 0, 0);
		}
		~Image() override { --live; }
		void Update(Bitmap& b, int x, int y) override {
			for (int row = 0; row < b.GetHeight(); ++row)
				std::copy_n(b.GetPixels() + row * b.GetWidth(), b.GetWidth(),
				            pixels.begin() + x + (y + row) * w);
		}
		float GetWidth() override { return float(w); }
		float GetHeight() override { return float(h); }
	};
	int Image::live = 0;

	class Renderer : public TestRenderer {
	public:
		bool failNextImage = false;
		int imagesCreated = 0;
		Handle<IImage> lastDraw;
		Handle<IImage> CreateImage(Bitmap& b) override {
			if (failNextImage) {
				failNextImage = false;
				throw std::runtime_error("image upload failed");
			}
			++imagesCreated;
			return Handle<Image>::New(b).Cast<IImage>();
		}
		void DrawImage(stmp::optional<IImage&> image, const AABB2&, const AABB2&) override {
			lastDraw = Handle<IImage>(image);
		}
	};

	// A tiny raw atlas codec, deliberately not a PNG implementation. It lets
	// production Bitmap/IFont loading and atlas validation run without SDL.
	class AtlasCodec : public IBitmapCodec {
	public:
		std::string GetName() override { return "Test raw RGBA atlas"; }
		bool CanLoad() override { return true; }
		bool CanSave() override { return false; }
		bool CheckExtension(const std::string& path) override { return EndsWith(path, ".png"); }
		Bitmap* Load(IStream* s) override {
			const auto data = s->ReadAllBytes();
			Check(data.size() == 16, "test atlas size");
			auto b = Handle<Bitmap>::New(2, 2);
			std::memcpy(b->GetPixels(), data.data(), data.size());
			return std::move(b).Unmanage();
		}
		void Save(IStream*, Bitmap*) override { throw std::runtime_error("unexpected save"); }
	};

	std::string Read(const std::string& path) {
		std::ifstream f(path, std::ios::binary);
		Check(f.good(), "font fixture is present");
		return std::string(std::istreambuf_iterator<char>(f), {});
	}
	void U16(std::string& out, unsigned v) {
		out.push_back(static_cast<char>(v));
		out.push_back(static_cast<char>(v >> 8));
	}
	void U32(std::string& out, unsigned v) {
		U16(out, v);
		U16(out, v >> 16);
	}
	std::string Fallback(unsigned advance, unsigned x = 0, unsigned y = 0, unsigned w = 2,
	                     unsigned h = 2) {
		std::string result = "OpenSpadesFontFl";
		U32(result, 1);
		U32(result, 16);
		U32(result, 0xF0000);
		U16(result, x);
		U16(result, y);
		result.push_back(static_cast<char>(w));
		result.push_back(static_cast<char>(h));
		U16(result, advance);
		U16(result, 0);
		U16(result, 0);
		return result;
	}
	bool ReloadFails() {
		try {
			FontManager::ReloadFonts();
		} catch (const std::exception&) {
			return true;
		}
		return false;
	}
} // namespace

int main() {
	int result = 0;
	try {
		AtlasCodec codec;
		auto* disk = new Files();
		FileManager::AppendFileSystem(disk);
		const auto base = std::string(ZS_TEST_RESOURCE_DIR) + "/Gfx/Fonts/";
		const auto a = Read(base + "OpenSans.ttf"), b = Read(base + "SquareFont.ttf");
		disk->files["gfx/fonts/opensans.ttf"] = a;
		disk->files["gfx/fonts/squarefont.ttf"] = b;

		// The real 100-unifont.ospfont encodes blank glyphs (e.g. U+0020 and
		// U+3000) with x=y=65535 and w=h=0. Those coordinates are never sampled.
		// Use a private-use code point absent from both bundled scalable faces
		// here to force fallback measurement/drawing, not OpenSans's space.
		const std::string glyph = "\xF3\xB0\x80\x80";
		disk->files["gfx/fonts/test.ospfont"] = Fallback(8, 0xFFFF, 0xFFFF, 0, 0);
		disk->files["gfx/fonts/test.png"] = std::string(16, '\xFF');
		auto r1 = Handle<Renderer>::New(), r2 = Handle<Renderer>::New();
		auto manager1 = Handle<FontManager>::New(r1.GetPointerOrNull());
		auto manager2 = Handle<FontManager>::New(r2.GetPointerOrNull());
		Handle<IFont> held(&manager1->GetGuiFont());
		auto* identity = held.GetPointerOrNull();
		Check(held->Measure(glyph).x == 8 && manager2->GetGuiFont().Measure(glyph).x == 8,
		      "Unifont's empty sentinel glyph loads at startup and retains its advance");
		held->Draw(glyph, MakeVector2(0, 0), 1, MakeVector4(1, 1, 1, 1));
		Check(!r1->lastDraw, "empty sentinel glyphs never sample the atlas");
		disk->files.erase("gfx/fonts/test.ospfont");
		disk->files.erase("gfx/fonts/test.png");
		FontManager::ReloadFonts();
		const std::string text = "iiiiMMMMWW";
		const auto original = held->Measure(text);
		held->Draw(text, MakeVector2(0, 0), 1, MakeVector4(1, 1, 1, 1));
		const auto uploads = r1->imagesCreated;

		disk->files["gfx/fonts/opensans.ttf"] = b;
		FontManager::ReloadFonts();
		const auto changed = held->Measure(text);
		Check(changed.x != original.x, "existing font's measured glyph advances refresh");
		Check(&manager1->GetGuiFont() == identity, "UI's IFont pointer is stable");
		Check(manager2->GetGuiFont().Measure(text).x == changed.x,
		      "all FontManagers/renderers see the new face");
		held->Draw(text, MakeVector2(0, 0), 1, MakeVector4(1, 1, 1, 1));
		Check(r1->imagesCreated > uploads, "old glyph atlas/raster caches are discarded");
		disk->files["gfx/fonts/opensans.ttf"] = a;
		FontManager::ReloadFonts();
		Check(held->Measure(text).x == original.x, "Default restores the exact font metrics");

		disk->files["gfx/fonts/opensans.ttf"] = "invalid font";
		Check(ReloadFails(), "invalid FreeType face is rejected");
		Check(held->Measure(text).x == original.x &&
		        manager2->GetGuiFont().Measure(text).x == original.x,
		      "failed face reload does not publish a partial font set");
		disk->files["gfx/fonts/opensans.ttf"] = a;

		const auto absent = held->Measure(glyph).x;
		disk->files["gfx/fonts/test.ospfont"] = Fallback(7);
		disk->files["gfx/fonts/test.png"] = std::string(16, '\xFF');
		FontManager::ReloadFonts();
		Check(held->Measure(glyph).x == 7 && manager2->GetGuiFont().Measure(glyph).x == 7,
		      "existing fallback renderers pick up a newly selected atlas font");
		held->Draw(glyph, MakeVector2(0, 0), 1, MakeVector4(1, 1, 1, 1));
		Check(r1->lastDraw && static_cast<Image&>(*r1->lastDraw).pixels[0] == 0xFFFFFFFF,
		      "fallback draw uses the selected atlas image");

		disk->files["gfx/fonts/test.ospfont"] = Fallback(11);
		disk->files["gfx/fonts/test.png"] = std::string(16, '\x77');
		FontManager::ReloadFonts();
		Check(held->Measure(glyph).x == 11, "fallback metrics are not stuck in the first font");
		held->Draw(glyph, MakeVector2(0, 0), 1, MakeVector4(1, 1, 1, 1));
		Check(static_cast<Image&>(*r1->lastDraw).pixels[0] == 0x77777777,
		      "fallback atlas bypasses stale RegisterImage cache entries");

		disk->files["gfx/fonts/opensans.ttf"] = b;
		disk->files["gfx/fonts/test.ospfont"] = "bad descriptor";
		Check(ReloadFails(), "corrupt fallback description is rejected");
		Check(held->Measure(text).x == original.x && held->Measure(glyph).x == 11,
		      "fallback failure keeps both old scalable and old fallback fonts");
		disk->files["gfx/fonts/test.ospfont"] = Fallback(15, 2);
		Check(ReloadFails(), "fallback glyphs outside the image width are rejected");
		disk->files["gfx/fonts/test.ospfont"] = Fallback(15, 0, 2);
		Check(ReloadFails(), "fallback glyphs outside the image height are rejected");
		disk->files["gfx/fonts/test.ospfont"] = Fallback(15);
		disk->files["gfx/fonts/test.png"] = std::string(16, '\x44');
		r2->failNextImage = true;
		Check(ReloadFails(), "renderer upload failure is propagated");
		Check(held->Measure(text).x == original.x && held->Measure(glyph).x == 11 &&
		        manager2->GetGuiFont().Measure(glyph).x == 11,
		      "failed upload cannot publish new fonts to just one renderer");
		held->Draw(glyph, MakeVector2(0, 0), 1, MakeVector4(1, 1, 1, 1));
		Check(static_cast<Image&>(*r1->lastDraw).pixels[0] == 0x77777777,
		      "failed upload retains the old atlas as well as its metrics");

		for (const auto& size :
		     {std::make_pair(0U, 0U), std::make_pair(0U, 2U), std::make_pair(2U, 0U)}) {
			disk->files["gfx/fonts/test.ospfont"] =
			  Fallback(9, 0xFFFF, 0xFFFF, size.first, size.second);
			FontManager::ReloadFonts();
			Check(held->Measure(glyph).x == 9 && manager2->GetGuiFont().Measure(glyph).x == 9,
			      "live reload preserves empty glyph advances on every renderer");
			r1->lastDraw = {};
			r2->lastDraw = {};
			held->Draw(glyph, MakeVector2(0, 0), 1, MakeVector4(1, 1, 1, 1));
			manager2->GetGuiFont().Draw(glyph, MakeVector2(0, 0), 1, MakeVector4(1, 1, 1, 1));
			Check(!r1->lastDraw && !r2->lastDraw, "zero-area glyphs never reach the renderer");
		}

		disk->files["gfx/fonts/opensans.ttf"] = a;
		disk->files.erase("gfx/fonts/test.ospfont");
		disk->files.erase("gfx/fonts/test.png");
		FontManager::ReloadFonts();
		Check(held->Measure(glyph).x == absent, "Default removes mod-only fallback glyphs");
		manager1 = {};
		manager2 = {};
		FontManager::ReloadFonts();
		Check(held->Measure(text).x == original.x,
		      "font references outlive their FontManager safely");
		std::printf("font_reload_test: all passed\n");
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "FAIL: %s\n", ex.what());
		result = 1;
	}
	if (Image::live != 0) {
		std::fprintf(stderr, "FAIL: renderer images outlived all font holders\n");
		result = 1;
	}
	FileManager::Close();
	return result;
}
