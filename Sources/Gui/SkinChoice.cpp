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
#include <cstdio>

#include "Main.h"
#include "SkinChoice.h"

namespace spades {
	namespace gui {
		namespace {
			const char* const kDirs[kNumSkinSlots] = {"Spade", "Block",   "Grenade",
			                                          "Rifle", "SMG",     "Shotgun"};
			const char* const kKeys[kNumSkinSlots] = {"spade", "block", "grenade",
			                                          "rifle", "smg",   "shotgun"};

			std::string UserFile(const char* name) {
				return std::string(spades::g_userResourceDirectory) + "/" + name;
			}

			std::vector<std::string> ReadLines(const char* name) {
				std::vector<std::string> out;
				std::FILE* f = std::fopen(UserFile(name).c_str(), "rb");
				if (!f)
					return out;
				std::string all;
				char buf[4096];
				std::size_t rd;
				while ((rd = std::fread(buf, 1, sizeof(buf), f)) > 0)
					all.append(buf, rd);
				std::fclose(f);
				std::size_t start = 0;
				while (start < all.size()) {
					std::size_t nl = all.find('\n', start);
					if (nl == std::string::npos)
						nl = all.size();
					std::string line = all.substr(start, nl - start);
					if (!line.empty() && line.back() == '\r')
						line.pop_back();
					if (!line.empty())
						out.push_back(line);
					start = nl + 1;
				}
				return out;
			}

			bool WriteLines(const char* name, const std::vector<std::string>& lines) {
				std::FILE* f = std::fopen(UserFile(name).c_str(), "wb");
				if (!f)
					return false;
				bool ok = true;
				for (const std::string& line : lines)
					ok = ok && std::fputs(line.c_str(), f) >= 0 && std::fputc('\n', f) != EOF;
				return std::fclose(f) == 0 && ok;
			}

			bool StartsWithFolded(const std::string& s, std::size_t at, const char* prefix) {
				for (std::size_t i = 0; prefix[i]; i++) {
					if (at + i >= s.size())
						return false;
					char c = s[at + i];
					if (c == '\\')
						c = '/';
					if (std::tolower(static_cast<unsigned char>(c)) !=
					    std::tolower(static_cast<unsigned char>(prefix[i])))
						return false;
				}
				return true;
			}
		} // namespace

		const char* SkinSlotDirectory(SkinSlot slot) { return kDirs[static_cast<int>(slot)]; }

		int SkinSlotOfPath(const std::string& path) {
			std::size_t at = 0;
			while (at < path.size() && (path[at] == '/' || path[at] == '\\'))
				at++;
			std::size_t rest;
			if (StartsWithFolded(path, at, "models/weapons/"))
				rest = at + 15;
			else if (StartsWithFolded(path, at, "sounds/weapons/"))
				rest = at + 15;
			else if (StartsWithFolded(path, at, "scripts/skin/"))
				rest = at + 13;
			else
				return -1;
			for (int i = 0; i < kNumSkinSlots; i++) {
				const std::string dir = std::string(kDirs[i]) + "/";
				if (StartsWithFolded(path, rest, dir.c_str()))
					return i;
			}
			return -1;
		}

		SkinChoices SkinChoices::Load() {
			SkinChoices c;
			for (const std::string& line : ReadLines("SkinChoices.txt")) {
				const std::size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				const std::string key = line.substr(0, eq);
				for (int i = 0; i < kNumSkinSlots; i++) {
					if (key == kKeys[i]) {
						c.chosen[i] = true;
						c.mod[i] = line.substr(eq + 1);
					}
				}
			}
			return c;
		}

		bool SkinChoices::Save() const {
			std::vector<std::string> lines;
			for (int i = 0; i < kNumSkinSlots; i++)
				if (chosen[i])
					lines.push_back(std::string(kKeys[i]) + "=" + mod[i]);
			return WriteLines("SkinChoices.txt", lines);
		}

		bool SkinChoices::operator==(const SkinChoices& o) const {
			for (int i = 0; i < kNumSkinSlots; i++)
				if (chosen[i] != o.chosen[i] || (chosen[i] && mod[i] != o.mod[i]))
					return false;
			return true;
		}

		std::vector<std::string> FavoriteMods::Load() { return ReadLines("FavoriteMods.txt"); }

		bool FavoriteMods::Contains(const std::string& mod) {
			const auto all = Load();
			return std::find(all.begin(), all.end(), mod) != all.end();
		}

		void FavoriteMods::Set(const std::string& mod, bool favorite) {
			auto all = Load();
			all.erase(std::remove(all.begin(), all.end(), mod), all.end());
			if (favorite)
				all.push_back(mod);
			WriteLines("FavoriteMods.txt", all);
		}
	} // namespace gui
} // namespace spades
