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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>
#include <vector>

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		class ModsScreenHelper;
		class ModPreviewCache;
		namespace ui {
			class Button;
			class Label;
			class ListView;
		} // namespace ui
	} // namespace gui
	namespace client {
		class ClientUI;

		/**
		 * The Mods page of the in-game escape menu. Skins: every installed
		 * mod as a card with a picture of its model, by tool (or all of them);
		 * a click puts it on, there and then, for its tool, whether or not it
		 * is enabled in the main menu (SkinChoices). Favourites come first.
		 * After an idea and first version by Kyrope. All mods: the main menu's
		 * list, and Apply. Shown over its owner, which it disables meanwhile.
		 */
		class ModsWindow : public gui::ui::UIElement {
			ClientUI* ui;                 // weak
			gui::ui::UIElement* owner;    // weak
			Handle<gui::ModsScreenHelper> helper;
			Handle<gui::ModPreviewCache> previews;
			gui::ui::UIElement* skinsPage; // weak
			gui::ui::UIElement* listPage;  // weak
			gui::ui::ListView* grid;       // weak
			gui::ui::ListView* list;       // weak
			gui::ui::Label* statusLabel;   // weak
			gui::ui::Button* pageButtons[2];
			std::vector<gui::ui::Button*> filterButtons;
			int filter = -1; // a SkinSlot, -1 for all
			float gridWidth = 0.0F;
			bool downloading = false;

			void ShowPage(int page);
			void SetFilter(int slot);
			void LoadSkins();
			void OnSkinPicked(const std::string& mod, int slot);
			void OnFavoriteToggled(const std::string& mod);

			void LoadModList();
			void OnModToggle(const std::string& modName);
			void OnApply(gui::ui::UIElement& sender);
			void OnDisableAll(gui::ui::UIElement& sender);
			void OnDownload(gui::ui::UIElement& sender);
			void OnDownloadConfirmed(gui::ui::UIElement& sender);
			void CheckDownload();

		public:
			gui::ui::EventHandler closed;

			ModsWindow(gui::ui::UIElement* owner, ClientUI* ui);
			void Run();
			void Close();
			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace client
} // namespace spades
