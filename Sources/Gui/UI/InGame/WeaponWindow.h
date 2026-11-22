/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <Core/RefCountedObject.h>
#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		class StandardPreferenceLayouter;
	}
	namespace client {
		class ClientUI;

		/**
		 * The Escape menu's Weapon page (testers: changing a sight meant digging
		 * through Settings): the sight (iron sights, red dot, cross; the rifle's
		 * scope in a headset), and the aiming options that go with it, the
		 * headset's or the monitor's. The same rows as Settings, changing the
		 * same settings, at once.
		 */
		class WeaponWindow : public gui::ui::UIElement {
			ClientUI* ui;              // weak
			// Held: the menu can be dropped (the screens rebuilt on a resize)
			// while this page is up, and Close touches it
			Handle<gui::ui::UIElement> owner;
			Handle<gui::StandardPreferenceLayouter> layouter;
			AABB2 contents;

		public:
			gui::ui::EventHandler closed;

			WeaponWindow(gui::ui::UIElement* owner, ClientUI* ui, bool forVR);
			void Run();
			void Close();
			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace client
} // namespace spades
