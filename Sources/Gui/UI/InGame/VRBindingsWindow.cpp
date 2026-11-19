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

#include "VRBindingsWindow.h"
#include "VRControlsWindow.h"

#include <algorithm>
#include <cmath>

#include <json/json.h>

#include <Client/ClientUI.h>
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/DropDownList.h>

SPADES_SETTING(vr_bindings);
SPADES_SETTING(vr_swapSticks);

namespace spades {
	namespace client {
		using gui::ui::Button;
		using gui::ui::SetColorNP;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		namespace {
			// Every action, as the default layout has it on each button, in the
			// order the drop-downs list them: what people move most first.
			struct Action {
				int role;
				VRInput input;
				const char* label;
			};
			const Action kActions[] = {
			  {VRRoleTool, VRInput::Trigger, "Fire / use the tool"},
			  {VRRoleTool, VRInput::Primary, "Jump"},
			  {VRRoleTool, VRInput::Secondary, "Reload"},
			  {VRRoleTool, VRInput::StickClick, "Crouch"},
			  {VRRoleOff, VRInput::StickClick, "Sprint"},
			  {VRRoleOff, VRInput::Secondary, "Big map"},
			  {VRRoleTool, VRInput::Grip, "Dig / build a line / holsters"},
			  {VRRoleOff, VRInput::Grip, "Hold the barrel / lamp / scoreboard"},
			  {VRRoleOff, VRInput::Trigger, "Brace the gun / big map"},
			  {VRRoleOff, VRInput::Primary, "Copy a block's colour"},
			  {VRRoleOff, VRInput::Menu, "Game menu"},
			  {VRRoleTool, VRInput::Menu, "(gun hand's menu: unused)"},
			};
			constexpr int kNumActions = static_cast<int>(sizeof(kActions) / sizeof(kActions[0]));

			int ActionIndex(const VRBindTarget& t) {
				if (t.role < 0)
					return kNumActions; // Nothing, last
				for (int i = 0; i < kNumActions; i++)
					if (kActions[i].role == t.role && kActions[i].input == t.input)
						return i;
				return kNumActions;
			}
			std::string ActionLabel(int i) {
				return i >= kNumActions ? _Tr("Client", "Nothing") : _Tr("Client", kActions[i].label);
			}
			const char* InputCaption(VRInput i) {
				switch (i) {
					case VRInput::Trigger: return "Trigger";
					case VRInput::Grip: return "Grip";
					case VRInput::Primary: return "A / X";
					case VRInput::Secondary: return "B / Y";
					case VRInput::StickClick: return "Stick click";
					case VRInput::Menu: return "Menu";
					default: return "";
				}
			}
			bool InputByName(const std::string& n, VRInput& out) {
				static const char* const names[] = {"Trigger", "Grip", "Primary", "Secondary",
				                                    "StickClick", "Menu"};
				for (int i = 0; i < kVRInputCount; i++)
					if (n == names[i]) {
						out = static_cast<VRInput>(i);
						return true;
					}
				return false;
			}
		} // namespace

		VRBindingsWindow::VRBindingsWindow(ClientUI* ui)
		    : UIElement(&ui->GetUIManager()), ui(ui) {
			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(manager->GetRootElement().GetBounds());
			bindings = VRParseBindings(vr_bindings);

			// The controllers in use (the Body slide has no buttons)
			family = VRControlsSlideForControllers();

			const float sw = manager->screenWidth, sh = manager->screenHeight;
			const float buttonH = 30.0F, margin = 10.0F;
			float iw = sw - margin * 2.0F;
			float ih = iw * 9.0F / 16.0F;
			if (ih > sh - buttonH - margin * 3.0F) {
				ih = sh - buttonH - margin * 3.0F;
				iw = ih * 16.0F / 9.0F;
			}
			imageRect = AABB2((sw - iw) * 0.5F, margin, iw, ih);

			Load();

			// A drop-down per button, in a column beside its hand, in the order
			// the buttons stand on the controller
			const float colW = std::min(300.0F, iw * 0.26F), rowH = 30.0F;
			for (int side = 0; side < 2; side++) {
				std::vector<std::size_t> mine;
				for (std::size_t i = 0; i < controls.size(); i++) {
					const bool left = controls[i].point.x < pictureSize.x * 0.5F;
					if (left == (side == 0))
						mine.push_back(i);
				}
				std::sort(mine.begin(), mine.end(), [&](std::size_t a, std::size_t b) {
					return controls[a].point.y < controls[b].point.y;
				});
				const float x = side == 0 ? imageRect.GetMinX() + margin
				                          : imageRect.GetMaxX() - margin - colW;
				const float total = static_cast<float>(mine.size()) * (rowH + 26.0F);
				float y = imageRect.GetMinY() + (imageRect.GetHeight() - total) * 0.45F;
				for (std::size_t i : mine) {
					Control& c = controls[i];
					c.rect = AABB2(x, y + 22.0F, colW, rowH);
					Handle<Button> b = Handle<Button>::New(manager);
					b->SetBounds(c.rect);
					b->activated = [this, i](UIElement&) { Choose(i); };
					AddChild(b.GetPointerOrNull());
					c.button = b.GetPointerOrNull();
					y += rowH + 26.0F;
				}
			}

			const float by = imageRect.GetMaxY() + margin;
			const float bx = imageRect.GetMinX();
			{
				Handle<Button> b = Handle<Button>::New(manager);
				b->SetBounds(AABB2(bx, by, 220.0F, buttonH));
				b->activated = [this](UIElement&) {
					vr_swapSticks = (bool)vr_swapSticks ? 0 : 1;
					Refresh();
				};
				AddChild(b.GetPointerOrNull());
				swapButton = b.GetPointerOrNull();
			}
			{
				Handle<Button> b = Handle<Button>::New(manager);
				b->caption = _Tr("Client", "Reset to default");
				b->SetBounds(AABB2(bx + 230.0F, by, 180.0F, buttonH));
				b->activated = [this](UIElement&) {
					bindings = VRDefaultBindings();
					vr_swapSticks = 0;
					Save();
					Refresh();
				};
				AddChild(b.GetPointerOrNull());
			}
			{
				Handle<Button> b = Handle<Button>::New(manager);
				b->caption = _Tr("Client", "Back");
				b->hotKeyText = _Tr("Client", "[Esc]");
				b->SetBounds(AABB2(imageRect.GetMaxX() - 150.0F, by, 150.0F, buttonH));
				b->activated = [this](UIElement&) { this->ui->EnterVRControlsWindow(); };
				AddChild(b.GetPointerOrNull());
			}
			Refresh();
		}

		void VRBindingsWindow::Load() {
			// Where each button is on the picture (Tools/vr-controls, "bind")
			const std::string path = "Gfx/VR/Controls/Bind-" + family + ".json";
			try {
				std::string text = FileManager::ReadAllBytes(path.c_str());
				Json::Value root;
				Json::Reader reader;
				if (!reader.parse(text, root, false)) {
					SPLog("%s: %s", path.c_str(), reader.getFormattedErrorMessages().c_str());
					return;
				}
				if (root["size"].isArray() && root["size"].size() == 2)
					pictureSize = MakeVector2(root["size"][0].asFloat(), root["size"][1].asFloat());
				const int tool = VRToolHand();
				for (const char* side : {"left", "right"}) {
					const Json::Value& hand = root[side];
					if (!hand.isObject())
						continue;
					const int physical = side[0] == 'l' ? VRHandLeft : VRHandRight;
					for (const std::string& name : hand.getMemberNames()) {
						VRInput input;
						const Json::Value& p = hand[name];
						if (!InputByName(name, input) || !p.isArray() || p.size() != 2)
							continue;
						Control c;
						c.role = physical == tool ? VRRoleTool : VRRoleOff;
						c.input = input;
						c.point = MakeVector2(p[0].asFloat(), p[1].asFloat());
						c.button = nullptr;
						controls.push_back(c);
					}
				}
			} catch (const std::exception& ex) {
				SPLog("%s: %s", path.c_str(), ex.what());
			}
		}

		void VRBindingsWindow::Save() {
			vr_bindings = VRFormatBindings(bindings);
		}

		void VRBindingsWindow::Refresh() {
			for (auto& c : controls)
				if (c.button)
					c.button->caption =
					  ActionLabel(ActionIndex(bindings.target[c.role][static_cast<int>(c.input)]));
			if (swapButton)
				swapButton->caption = (bool)vr_swapSticks
				                        ? _Tr("Client", "Sticks: walk right, turn left")
				                        : _Tr("Client", "Sticks: walk left, turn right");
		}

		void VRBindingsWindow::Choose(std::size_t i) {
			if (i >= controls.size() || !controls[i].button)
				return;
			std::vector<std::string> items;
			for (int a = 0; a <= kNumActions; a++)
				items.push_back(ActionLabel(a));
			const Control& c = controls[i];
			const int current = ActionIndex(bindings.target[c.role][static_cast<int>(c.input)]);
			gui::ui::ShowDropDownList(
			  *controls[i].button, items,
			  [this, i](int chosen) {
				  if (chosen < 0 || i >= controls.size())
					  return;
				  const Control& c = controls[i];
				  VRBindTarget& t = bindings.target[c.role][static_cast<int>(c.input)];
				  if (chosen >= kNumActions) {
					  t.role = -1;
				  } else {
					  t.role = kActions[chosen].role;
					  t.input = kActions[chosen].input;
				  }
				  Save();
				  Refresh();
			  },
			  current);
		}

		Vector2 VRBindingsWindow::PicturePoint(const Vector2& p) const {
			return MakeVector2(imageRect.GetMinX() + p.x / pictureSize.x * imageRect.GetWidth(),
			                   imageRect.GetMinY() + p.y / pictureSize.y * imageRect.GetHeight());
		}

		void VRBindingsWindow::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape")
				ui->EnterVRControlsWindow();
			else
				UIElement::HotKey(key);
		}

		void VRBindingsWindow::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			IFont& font = ui->GetFontManager().GetGuiFont();
			const Vector2 pos = GetScreenPosition();
			const AABB2 b = GetBounds();

			// Under the drop-downs: the dimmed backdrop, the controllers, and a
			// line from each drop-down to its button
			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.85F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, b.GetWidth(), b.GetHeight()));
			if (!tried) {
				tried = true;
				const std::string path = "Gfx/VR/Controls/Bind-" + family + ".png";
				try {
					image = r.RegisterImage(path.c_str());
				} catch (const std::exception& ex) {
					SPLog("%s: %s", path.c_str(), ex.what());
				}
			}
			if (image) {
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
				r.DrawImage(*image, imageRect.Translated(pos));
			}
			const std::string title = _Tr("Client", "Change buttons");
			font.Draw(title, pos + imageRect.min + MakeVector2(16.0F, 12.0F), 1.5F,
			          MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			const Vector4 lineCol = MakeVector4(0.96F, 0.70F, 0.24F, 1.0F);
			for (const Control& c : controls) {
				// The button's name over its drop-down
				font.Draw(_Tr("Client", InputCaption(c.input)),
				          pos + c.rect.min + MakeVector2(2.0F, -20.0F), 1.0F,
				          MakeVector4(0.96F, 0.70F, 0.24F, 1.0F));
				const Vector2 to = pos + PicturePoint(c.point);
				const bool leftCol = c.rect.GetMinX() < imageRect.GetMinX() + imageRect.GetWidth() * 0.5F;
				const Vector2 from = pos + MakeVector2(leftCol ? c.rect.GetMaxX() : c.rect.GetMinX(),
				                                       c.rect.GetMinY() + c.rect.GetHeight() * 0.5F);
				// A line of small squares: the renderer draws no lines
				const Vector2 d = to - from;
				const float len = d.GetLength();
				const int n = std::max(2, static_cast<int>(len / 3.0F));
				SetColorNP(r, lineCol);
				for (int k = 0; k <= n; k++) {
					const Vector2 q = from + d * (static_cast<float>(k) / static_cast<float>(n));
					r.DrawImage(nullptr, AABB2(q.x - 1.0F, q.y - 1.0F, 2.0F, 2.0F));
				}
			}

			UIElement::Render();
		}
	} // namespace client
} // namespace spades
