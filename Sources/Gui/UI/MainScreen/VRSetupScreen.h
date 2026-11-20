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

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		namespace ui {
			class Button;
		}

		/**
		 * The first time the game starts in a headset (vr_setupDone): a few
		 * choices that decide whether VR is comfortable, asked up front rather
		 * than left to be found in Settings: the dominant hand, standing or
		 * seated, how turning works, and the comfort vignette. In the game's
		 * dialog look (DialogChrome). Every choice is an ordinary setting, kept
		 * as soon as it is picked; Done only closes it and marks it seen.
		 */
		class VRSetupScreen : public ui::UIElement {
			struct Choice {
				std::string setting;
				std::string value;
				ui::Button* button; // weak: a child
			};
			std::vector<Choice> choices;
			AABB2 contents;

			void AddRow(float y, const std::string& caption, const std::string& setting,
			            const std::vector<std::pair<std::string, std::string>> options);
			void Refresh();

		public:
			VRSetupScreen(ui::UIManager* manager);
			/** Called once Done is pressed (the setup is marked seen first). */
			std::function<void()> done;
			void Render() override;

			/** Whether the setup is still to be shown: in a headset, not yet seen. */
			static bool IsDue();
		};
	} // namespace gui
} // namespace spades
