// Native thumbnail driver for the Python/official-pak checks. No window/GPU,
// downloads, live script module, mod mount or real audio device is involved.
#include <Core/IStream.h>
#include <Core/StdStream.h>
#include <Core/ZipFileSystem.h>
#include <Gui/SkinPreviewScript.h>
#include <Gui/UI/Widgets/VoxelModelPreview.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

using namespace spades;
using namespace spades::gui;

namespace {
	std::string Lower(std::string text) {
		for (auto& c : text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return text;
	}
	void Json(std::ostream& out, const std::string& text) {
		out << '"';
		for (const unsigned char c : text) {
			if (c == '"' || c == '\\')
				out << '\\' << c;
			else if (c < 32)
				out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
			else
				out << c;
		}
		out << '"';
	}
} // namespace

int main(int argc, char** argv) {
	try {
		if (argc < 5)
			throw std::runtime_error("usage: skin_preview_fixture Resources type pack.pak|- "
			                         "output-prefix [key=value ...]");
		SkinType type = SkinType::Count;
		for (std::size_t i = 0; i < kWeaponSkinTypeCount; ++i)
			if (Lower(argv[2]) == SkinTypeKey(static_cast<SkinType>(i)))
				type = static_cast<SkinType>(i);
		if (!IsWeaponSkin(type))
			throw std::runtime_error("unknown weapon type");
		std::map<std::string, std::filesystem::path> base;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[1]))
			if (entry.is_regular_file())
				base.emplace(Lower(entry.path().lexically_relative(argv[1]).generic_string()),
				             entry.path());
		std::unique_ptr<StdStream> stream;
		std::unique_ptr<ZipFileSystem> pak;
		if (std::string(argv[3]) != "-") {
			auto* file = std::fopen(argv[3], "rb");
			if (!file)
				throw std::runtime_error("cannot open fixture pak");
			stream = std::make_unique<StdStream>(file, true);
			pak = std::make_unique<ZipFileSystem>(stream.get());
		}
		const SkinPreviewReader reader = [&](const std::string& path,
		                                     std::size_t limit) -> std::optional<std::string> {
			if (pak)
				for (const std::string& candidate : {path, path + ".weak"})
					if (pak->FileExists(candidate.c_str()))
						return pak->OpenForReading(candidate.c_str(), limit)->ReadAllBytes();
			for (const std::string& candidate : {path, path + ".weak"}) {
				const auto it = base.find(Lower(candidate));
				if (it == base.end())
					continue;
				if (std::filesystem::file_size(it->second) > limit)
					throw std::runtime_error("oversized base fixture");
				std::ifstream file(it->second, std::ios::binary);
				return std::string(std::istreambuf_iterator<char>(file), {});
			}
			return std::nullopt;
		};
		SkinPreviewOptions options;
		options.settings["cg_weaponCharms"] = "0";
		for (int i = 5; i < argc; ++i) {
			const std::string argument(argv[i]);
			const auto equals = argument.find('=');
			if (equals == std::string::npos)
				throw std::runtime_error("expected config key=value");
			if (argument.substr(0, equals) == "team") {
				std::string rgb = argument.substr(equals + 1);
				std::replace(rgb.begin(), rgb.end(), ',', ' ');
				std::istringstream input(rgb);
				if (!(input >> options.teamColor.x >> options.teamColor.y >> options.teamColor.z))
					throw std::runtime_error("expected team=r,g,b");
			} else {
				options.settings[argument.substr(0, equals)] = argument.substr(equals + 1);
			}
		}
		const auto scene = CaptureSkinPreview(type, reader, options);
		ui::VoxelModelPreview preview(scene);
		if (preview.IsEmpty())
			throw std::runtime_error("empty assembled preview");
		constexpr int width = 192, height = 128, angles = 24;
		std::filesystem::path prefix(argv[4]);
		std::filesystem::create_directories(prefix.parent_path());
		std::ofstream json(prefix.string() + ".json");
		json << std::setprecision(9) << "{\"scripted\":" << (scene.scripted ? "true" : "false")
		     << ",\"faces\":" << preview.GetFaceCount() << ",\"width\":" << width
		     << ",\"height\":" << height << ",\"angles\":" << angles
		     << ",\"beams\":" << scene.beams.size() << ",\"parts\":[";
		for (std::size_t i = 0; i < scene.parts.size(); ++i) {
			const auto& part = scene.parts[i];
			if (i)
				json << ',';
			json << "{\"path\":";
			Json(json, part.path);
			json << ",\"matrix\":[";
			for (int j = 0; j < 16; ++j) {
				if (j)
					json << ',';
				json << part.matrix.m[j];
			}
			const auto origin = part.model->GetOrigin();
			json << "],\"color\":[" << part.customColor.x << ',' << part.customColor.y << ','
			     << part.customColor.z << "],\"opacity\":" << part.opacity << ",\"origin\":["
			     << origin.x << ',' << origin.y << ',' << origin.z << "],\"dimensions\":["
			     << part.model->GetWidth() << ',' << part.model->GetHeight() << ','
			     << part.model->GetDepth() << "]}";
		}
		json << "],\"coverage\":[";
		for (int angle = 0; angle < angles; ++angle) {
			const float yaw = 1.15F + float(angle) * kTwoPi / angles;
			for (const auto& quad : preview.Project(AABB2(0, 0, width, height), yaw))
				for (const auto p :
				     {quad.origin, quad.origin + quad.axisU, quad.origin + quad.axisV,
				      quad.origin + quad.axisU + quad.axisV})
					if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.y < 0 ||
					    p.x > width || p.y > height)
						throw std::runtime_error(
						  "assembled preview escapes its card during rotation");
			const auto pixels = preview.Rasterize(width, height, yaw);
			const auto count =
			  std::count_if(pixels.begin(), pixels.end(), [](auto p) { return p >> 24; });
			if (!count)
				throw std::runtime_error("assembled preview is invisible during rotation");
			if (angle)
				json << ',';
			json << count;
			if (angle == 1 || angle % 6 == 0) {
				std::ofstream image(prefix.string() + "-" + std::to_string(angle) + ".rgba",
				                    std::ios::binary);
				for (const auto pixel : pixels)
					for (int channel = 0; channel < 4; ++channel)
						image.put(static_cast<char>(pixel >> (channel * 8)));
				if (!image.good())
					throw std::runtime_error("cannot write preview pixels");
			}
		}
		json << "]}\n";
		if (!json.good())
			throw std::runtime_error("cannot write preview metadata");
		std::cout << "Captured " << scene.parts.size() << " parts, " << scene.beams.size()
		          << " beams, " << preview.GetFaceCount() << " faces\n";
		return 0;
	} catch (const std::exception& ex) {
		std::cerr << ex.what() << '\n';
		return 1;
	}
}
