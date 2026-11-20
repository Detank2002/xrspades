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

#include "VRSetupScreen.h"

#include <cstdlib>

#include <Client/VRState.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/DialogChrome.h>
#include <Gui/UI/Widgets/Label.h>

// Set once the first-time VR setup has been seen (Settings, VR, to see it again)
DEFINE_SPADES_SETTING(vr_setupDone, "0");

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::Label;
		using ui::UIElement;

		namespace {
			const float kRowHeight = 40.0F;
			const float kLabelWidth = 260.0F;
			const float kWidth = 700.0F;
			const int kRows = 4;
		} // namespace

		bool VRSetupScreen::IsDue() {
			return client::GetVRSystem() != nullptr && !(bool)vr_setupDone;
		}

		VRSetupScreen::VRSetupScreen(ui::UIManager* manager) : UIElement(manager) {
			SetBounds(manager->GetRootElement().GetBounds());
			SPLog("[VR] First-time setup shown");
			const float introH = 70.0F;
			const float height = introH + kRows * kRowHeight + 20.0F + DialogChrome::kButtonHeight;
			contents = DialogChrome::Attach(*this, kWidth, height);

			{
				Handle<Label> l = Handle<Label>::New(manager);
				l->text = _Tr("Client", "Welcome to XRSpades in VR");
				l->textScale = 1.3F;
				l->SetBounds(AABB2(contents.GetMinX(), contents.GetMinY(), contents.GetWidth(), 30.0F));
				AddChild(l.GetPointerOrNull());
			}
			{
				Handle<Label> l = Handle<Label>::New(manager);
				l->text = _Tr("Client", "A few choices to make it comfortable. Change them any time in "
				                        "Settings, VR.");
				l->textColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.7F);
				l->SetBounds(AABB2(contents.GetMinX(), contents.GetMinY() + 34.0F, contents.GetWidth(), 24.0F));
				AddChild(l.GetPointerOrNull());
			}

			float y = contents.GetMinY() + introH;
			AddRow(y, _Tr("Client", "Your dominant hand"), "vr_leftHanded",
			       {{_Tr("Client", "Right"), "0"}, {_Tr("Client", "Left"), "1"}});
			y += kRowHeight;
			AddRow(y, _Tr("Client", "How you play"), "vr_seated",
			       {{_Tr("Client", "Standing"), "0"}, {_Tr("Client", "Seated"), "1"}});
			y += kRowHeight;
			AddRow(y, _Tr("Client", "Turning"), "vr_snapTurn",
			       {{_Tr("Client", "Snap 45°"), "45"},
			        {_Tr("Client", "Snap 30°"), "30"},
			        {_Tr("Client", "Smooth"), "0"}});
			y += kRowHeight;
			AddRow(y, _Tr("Client", "Comfort vignette when moving"), "vr_vignette",
			       {{_Tr("Client", "Off"), "0"}, {_Tr("Client", "Light"), "0.5"},
			        {_Tr("Client", "Strong"), "0.9"}});

			{
				Handle<Button> b = Handle<Button>::New(manager);
				b->caption = _Tr("Client", "Done");
				b->SetBounds(DialogChrome::ButtonSlot(contents, 0));
				b->activated = [this](UIElement&) {
					Settings::ItemHandle("vr_setupDone", nullptr) = 1;
					if (done)
						done();
				};
				AddChild(b.GetPointerOrNull());
			}
			Refresh();
		}

		void VRSetupScreen::AddRow(float y, const std::string& caption, const std::string& setting,
		                           const std::vector<std::pair<std::string, std::string>> options) {
			ui::UIManager* manager = &GetManager();
			{
				Handle<Label> l = Handle<Label>::New(manager);
				l->text = caption;
				l->alignment = MakeVector2(0.0F, 0.5F);
				l->SetBounds(AABB2(contents.GetMinX(), y, kLabelWidth, kRowHeight - 6.0F));
				AddChild(l.GetPointerOrNull());
			}
			const float x0 = contents.GetMinX() + kLabelWidth;
			const float w = (contents.GetMaxX() - x0) / static_cast<float>(options.size());
			for (std::size_t i = 0; i < options.size(); i++) {
				Handle<Button> b = Handle<Button>::New(manager);
				b->caption = options[i].first;
				b->toggle = true;
				b->SetBounds(AABB2(x0 + w * static_cast<float>(i) + 2.0F, y, w - 4.0F,
				                   kRowHeight - 6.0F));
				const std::string value = options[i].second;
				b->activated = [this, setting, value](UIElement&) {
					Settings::ItemHandle(setting, nullptr) = value;
					Refresh();
				};
				AddChild(b.GetPointerOrNull());
				choices.push_back({setting, value, b.GetPointerOrNull()});
			}
		}

		void VRSetupScreen::Refresh() {
			// A choice is lit while its setting holds its value (as a number, so
			// "0.50" lights "0.5"); a setting matching none lights none
			for (Choice& c : choices) {
				Settings::ItemHandle h(c.setting, nullptr);
				const float current = std::atof(std::string(h).c_str());
				const float mine = std::atof(c.value.c_str());
				c.button->toggled = std::abs(current - mine) < 1.0E-3F;
			}
		}

		void VRSetupScreen::Render() {
			UIElement::Render();
			DialogChrome::DrawEdges(*this, contents);
		}
	} // namespace gui
} // namespace spades
