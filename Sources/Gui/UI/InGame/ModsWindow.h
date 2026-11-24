/*
 Copyright (c) 2026 ZeroSpades developers.
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <array>
#include <string>

#include <Gui/SkinSelection.h>
#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		class ModsScreenHelper;
		class SkinCatalog;
		namespace ui {
			class Button;
			class Label;
			class ListView;
		} // namespace ui
	} // namespace gui
	namespace client {
		class ClientUI;

		/**
		 * The Escape menu's skin selector. The main menu still manages which
		 * mods are enabled; this page only equips one of those skins per weapon.
		 */
		class ModsWindow : public gui::ui::UIElement {
			ClientUI* ui; // weak
			Handle<gui::ui::UIElement> owner;
			Handle<gui::ModsScreenHelper> helper;
			Handle<gui::SkinCatalog> catalog;
			gui::ui::ListView* grid; // weak; owned as children
			gui::ui::Label* statusLabel;
			gui::ui::Label* categoryTitle;
			std::array<gui::ui::Button*, gui::kSkinTypeCount> categoryButtons{};
			AABB2 contents;
			AABB2 sidebar;
			int columns;

			void ShowCategory(gui::SkinType type);
			void OnSkinSelected(gui::SkinType type, const std::string& modName);
			void SetStatus(const std::string& text, bool warning = false);

		public:
			gui::ui::EventHandler closed;

			ModsWindow(gui::ui::UIElement* owner, ClientUI* ui);
			~ModsWindow() override;
			void Run();
			void Close();
			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace client
} // namespace spades
