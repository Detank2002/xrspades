// Test-only replacements for the application entry point, collection I/O and
// script engine. Real zip files, file mounting/filtering, model loading and
// hot-reload change detection are exercised by skin_overlay_test. The script
// fake fails on a known marker to test rollback without running arbitrary code.

#include <Core/FileManager.h>
#include <Core/IFileSystem.h>
#include <Core/StdStream.h>
#include <Gui/Main.h>
#include <Gui/ModsScreenHelper.h>
#include <ScriptBindings/ScriptManager.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace spades {
	std::string g_userResourceDirectory;
	bool g_tryMod = false;
	namespace gui {
		ModsScreenHelper::ModsScreenHelper() : modsCached(false) {}
		ModsScreenHelper::~ModsScreenHelper() = default;
		std::vector<std::string> ModsScreenHelper::GetEnabledMods() {
			std::ifstream in(g_userResourceDirectory + "/EnabledMods.txt");
			std::vector<std::string> v;
			std::string line;
			while (std::getline(in, line))
				if (!line.empty())
					v.push_back(line);
			return v;
		}
		void ModsScreenHelper::SetEnabledMods(const std::vector<std::string>& v) {
			std::ofstream out(g_userResourceDirectory + "/EnabledMods.txt");
			for (auto& x : v)
				out << x << '\n';
		}
		std::vector<std::string> ModsScreenHelper::GetModPakPaths(const std::string& name) {
			const std::filesystem::path p = g_userResourceDirectory + "/Mods/" + name;
			std::vector<std::string> r;
			if (std::filesystem::is_regular_file(p))
				r.push_back("Mods/" + name);
			else if (std::filesystem::is_directory(p)) {
				for (const auto& f : std::filesystem::directory_iterator(p))
					r.push_back("Mods/" + name + "/" + f.path().filename().string());
				std::sort(r.begin(), r.end());
			}
			return r;
		}
	} // namespace gui
	bool alwaysBad = false;
	ScriptManager::ScriptManager() : engine(nullptr) {}
	ScriptManager::~ScriptManager() = default;
	ScriptManager* ScriptManager::GetInstance() {
		static ScriptManager m;
		return &m;
	}
	std::string ScriptManager::CheckScripts() {
		return alwaysBad || FileManager::ReadAllBytes("Scripts/Skin/Rifle/View.as") == "BROKEN"
		         ? "bad script"
				 : "";
	}
	std::string ScriptManager::ReloadScripts() {
		auto e = CheckScripts();
		if (e.empty())
			++generation;
		return e;
	}
} // namespace spades
class SkinTestDiskFS : public spades::IFileSystem {
	std::string root;

public:
	SkinTestDiskFS(const std::string& r) : root(r) {}
	std::vector<std::string> EnumFiles(const char*) override { return {}; }
	std::unique_ptr<spades::IStream> OpenForReading(const char* p) override {
		auto* f = std::fopen((root + "/" + p).c_str(), "rb");
		if (!f)
			throw std::runtime_error(p);
		return std::unique_ptr<spades::IStream>(new spades::StdStream(f, true));
	}
	std::unique_ptr<spades::IStream> OpenForWriting(const char*) override {
		throw std::runtime_error("unsupported");
	}
	bool FileExists(const char* p) override {
		return std::filesystem::is_regular_file(root + "/" + p);
	}
};
void MountSkinTestDisk(const std::string& path) {
	spades::FileManager::AppendFileSystem(new SkinTestDiskFS(path));
}
