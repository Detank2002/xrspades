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

#include <string>
#include <vector>

#include <Client/IImage.h>
#include <Client/VRState.h>
#include <Core/RefCountedObject.h>
#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		namespace ui {
			class Button;
		}
	} // namespace gui
	namespace client {
		class ClientUI;

		/**
		 * Change Buttons: the controllers in use, drawn bare, with a drop-down
		 * beside each button joined to it by a line. Each lists every action
		 * the layout has (the common ones first) and Nothing; a choice is saved
		 * to vr_bindings at once. Swap Sticks and Reset under it. The menus
		 * always take the buttons as pressed (VRFrameState::rawHands), so this
		 * screen stays usable whatever is chosen on it.
		 */
		class VRBindingsWindow : public gui::ui::UIElement {
			ClientUI* ui; // weak
			std::string family;
			Handle<IImage> image;
			bool tried = false;
			AABB2 imageRect;
			Vector2 pictureSize{1600.0F, 900.0F};

			struct Control {
				int role;            // VRHandRole of its hand
				VRInput input;       // which of that hand's buttons
				Vector2 point;       // on the picture, its pixels
				AABB2 rect;          // the drop-down, in this element
				gui::ui::Button* button; // weak: a child
			};
			std::vector<Control> controls;
			gui::ui::Button* swapButton = nullptr; // weak

			VRBindings bindings;

			void Load();
			void Save();
			void Refresh();
			void Choose(std::size_t control);
			Vector2 PicturePoint(const Vector2& p) const;

		public:
			explicit VRBindingsWindow(ClientUI* ui);
			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace client
} // namespace spades
