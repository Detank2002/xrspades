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

#include "SkinCatalog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "ModOverlay.h"
#include "ModsScreenHelper.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/IStream.h>
#include <Core/MemoryStream.h>
#include <Core/VoxelModel.h>
#include <Core/ZipFileSystem.h>

namespace spades {
	namespace gui {
		namespace {
			std::string Lower(std::string text) {
				for (char& c : text)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return text;
			}

			bool EndsWith(const std::string& text, const std::string& suffix) {
				return text.size() >= suffix.size() &&
				       text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
			}

			std::vector<std::string> RegisteredModelPaths(const std::string& source) {
				std::vector<std::string> result;
				for (std::size_t i = 0; i < source.size() && result.size() < 32;) {
					// Skip comments/strings, rather than mistaking an example or an
					// unused registration in a comment for the displayed body.
					if (source.compare(i, 2, "//") == 0) {
						i = source.find('\n', i + 2);
						if (i == std::string::npos)
							break;
						continue;
					}
					if (source.compare(i, 2, "/*") == 0) {
						i = source.find("*/", i + 2);
						if (i == std::string::npos)
							break;
						i += 2;
						continue;
					}
					if (source[i] == '"' || source[i] == '\'') {
						const char quote = source[i++];
						while (i < source.size()) {
							const char c = source[i++];
							if (c == quote)
								break;
							if (c == '\\' && i < source.size())
								++i;
						}
						continue;
					}
					if (source.compare(i, 13, "RegisterModel") != 0) {
						++i;
						continue;
					}
					i += 13;
					auto space = [&] {
						while (i < source.size() &&
						       std::isspace(static_cast<unsigned char>(source[i])))
							++i;
					};
					space();
					if (i == source.size() || source[i++] != '(')
						continue;
					space();
					if (i == source.size() || source[i++] != '"')
						continue;
					const auto end = source.find('"', i);
					if (end == std::string::npos)
						break;
					if (end - i <= 512)
						result.push_back(Lower(source.substr(i, end - i)));
					i = end + 1;
				}
				return result;
			}

			Handle<VoxelModel> LoadPreviewBytes(const std::string& bytes) {
				// Browsing a custom pak must not allocate an arbitrarily large model.
				// Check the KV6 header before LoadKV6 allocates its block/offset arrays.
				if (bytes.size() < 32 || bytes.compare(0, 4, "Kvxl") != 0)
					SPRaise("Invalid preview model header");
				auto u32 = [&bytes](std::size_t at) {
					const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + at);
					return std::uint64_t(p[0]) | (std::uint64_t(p[1]) << 8) |
					       (std::uint64_t(p[2]) << 16) | (std::uint64_t(p[3]) << 24);
				};
				const auto w = u32(4), h = u32(8), d = u32(12), blocks = u32(28);
				constexpr std::uint64_t limit = 4 * 1024 * 1024;
				if (!w || !h || !d || w > 512 || h > 512 || d > 64 || w * h * d > limit ||
				    blocks > w * h * d || 32 + blocks * 8 + w * 4 + w * h * 2 > bytes.size())
					SPRaise("Preview model is too large or truncated");
				MemoryStream stream(bytes.data(), bytes.size());
				return VoxelModel::LoadKV6(stream);
			}
		} // namespace

		SkinCatalog::SkinCatalog(ModsScreenHelper& helper,
		                         const std::vector<std::string>& enabled) {
			for (const std::string& name : enabled) {
				if (paks.count(name) != 0)
					continue;
				SkinMod mod;
				mod.name = name;
				if (auto type = SkinTypeForModName(name))
					mod.types[static_cast<std::size_t>(*type)] = true;
				std::vector<std::string> paths, contents;
				bool complete = true;
				for (const std::string& path : helper.GetModPakPaths(name)) {
					try {
						auto stream = ModOverlay::OpenBaseResource(path.c_str());
						ZipFileSystem fs(stream.get());
						for (const std::string& file : fs.GetAllFiles()) {
							if (file.empty() || file.back() == '/')
								continue;
							contents.push_back(file);
							if (auto type = SkinTypeForPath(file))
								mod.types[static_cast<std::size_t>(*type)] = true;
						}
						paths.push_back(path);
					} catch (const std::exception& ex) {
						complete = false;
						SPLog("Cannot read skin pak %s: %s", path.c_str(), ex.what());
					}
				}
				// Never offer a partially readable folder pack as a working skin.
				// Missing, disabled and unreadable mods never become selectable.
				if (!complete || paths.empty())
					continue;
				std::sort(contents.begin(), contents.end());
				contents.erase(std::unique(contents.begin(), contents.end()), contents.end());
				mods.push_back(std::move(mod));
				paks.emplace(name, std::move(paths));
				files.emplace(name, std::move(contents));
			}
		}

		const std::vector<std::string>& SkinCatalog::GetPakPaths(const std::string& modName) const {
			static const std::vector<std::string> empty;
			auto it = paks.find(modName);
			return it == paks.end() ? empty : it->second;
		}

		std::optional<std::string> SkinCatalog::ReadModFile(const std::string& modName,
		                                                    const std::string& path,
		                                                    std::size_t maxBytes) const {
			const auto& paths = GetPakPaths(modName);
			// Like FileManager: real files beat .weak files, then the last pak wins.
			for (const std::string& candidate : {path, path + ".weak"}) {
				for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
					auto stream = ModOverlay::OpenBaseResource(it->c_str());
					ZipFileSystem fs(stream.get());
					if (fs.FileExists(candidate.c_str())) {
						// Bound the decompressed payload too, before ZipFileSystem
						// buffers it. Four million KV6 blocks plus offset tables.
						return fs.OpenForReading(candidate.c_str(), maxBytes)->ReadAllBytes();
					}
				}
			}
			return std::nullopt;
		}

		Handle<VoxelModel> SkinCatalog::LoadPreview(SkinType type,
		                                            const std::string& modName) const {
			if (!IsWeaponSkin(type))
				SPRaise("This category has no voxel preview");
			if (!modName.empty()) {
				// Some skins leave a placeholder at Weapon.kv6 but register a
				// differently named display body in ThirdPerson.as. Read that
				// literal, without compiling/executing or mounting its scripts.
				const std::string dir = Lower(SkinModelDirectory(type));
				const std::string script =
				  "Scripts/Skin/" + std::string(SkinTypeKey(type)) + "/ThirdPerson.as";
				if (auto source = ReadModFile(modName, script, 512 * 1024)) {
					for (const std::string& path : RegisteredModelPaths(*source))
						if (path.compare(0, dir.size(), dir) == 0 && EndsWith(path, ".kv6"))
							if (auto bytes = ReadModFile(modName, path))
								return LoadPreviewBytes(*bytes);
				}
				if (auto bytes = ReadModFile(modName, SkinPreviewPath(type)))
					return LoadPreviewBytes(*bytes);

				// For model-only packs, prefer a body, never a loose magazine,
				// scope, casing, etc. as the skin's only display model.
				auto it = files.find(modName);
				if (it != files.end()) {
					for (const std::string& file : it->second) {
						if (file.compare(0, dir.size(), dir) != 0 ||
						    (!EndsWith(file, ".kv6") && !EndsWith(file, ".kv6.weak")))
							continue;
						const std::string base = file.substr(dir.size());
						bool part = false;
						for (const char* fragment : {"magazine", "sight", "scope", "casing", "bolt",
						                             "pump", "shell", "rail"})
							part = part || base.find(fragment) != std::string::npos;
						if (!part) {
							if (auto bytes = ReadModFile(modName, file))
								return LoadPreviewBytes(*bytes);
						}
					}
				}
			}
			// Sound/script-only skins still have the stock body. In particular,
			// Default must not accidentally preview the currently equipped mod.
			return LoadPreviewBytes(
			  ModOverlay::OpenBaseResource(SkinPreviewPath(type))->ReadAllBytes());
		}

		std::optional<std::string> SkinCatalog::LoadFontPreview(const std::string& modName) const {
			constexpr const char* path = "Gfx/Fonts/OpenSans.ttf";
			if (modName.empty())
				return ModOverlay::OpenBaseResource(path)->ReadAllBytes();
			if (auto bytes = ReadModFile(modName, path))
				return bytes;
			const auto it = files.find(modName);
			if (it != files.end())
				for (std::string file : it->second) {
					if (EndsWith(file, ".weak"))
						file.resize(file.size() - 5);
					if (SkinTypeForPath(file) == SkinType::Font &&
					    (EndsWith(file, ".ttf") || EndsWith(file, ".otf") ||
					     EndsWith(file, ".ttc")))
						return ReadModFile(modName, file);
				}
			return std::nullopt;
		}
	} // namespace gui
} // namespace spades
