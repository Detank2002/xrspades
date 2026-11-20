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
#include <chrono>
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
#include <Gui/UI/Widgets/DialogChrome.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/DropDownList.h>

SPADES_SETTING(vr_bindings);
SPADES_SETTING(vr_swapSticks);

namespace spades {
	namespace client {
		using gui::DialogChrome;
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

			// The game's dialog look (DialogChrome): the view dimmed, a dark band
			// across the screen with a hairline at each edge, the title at its
			// top left and the buttons in a row along its bottom
			const float sw = manager->screenWidth, sh = manager->screenHeight;
			const float buttonH = DialogChrome::kButtonHeight, margin = 10.0F;
			const float titleH = 40.0F;
			// Placed as DialogChrome::Attach would, but the dim and the band are
			// drawn here (Render) under the picture, not as children over it
			contents = AABB2(20.0F, 30.0F, sw - 40.0F, sh - 60.0F);
			float iw = contents.GetWidth();
			float ih = iw * 9.0F / 16.0F;
			const float room = contents.GetHeight() - titleH - buttonH - margin;
			if (ih > room) {
				ih = room;
				iw = ih * 16.0F / 9.0F;
			}
			imageRect = AABB2(contents.GetMinX() + (contents.GetWidth() - iw) * 0.5F,
			                  contents.GetMinY() + titleH, iw, ih);

			Load();

			// A drop-down per button, in a column beside its hand, in the order
			// the buttons stand on the controller
			const float colW = std::min(300.0F, contents.GetWidth() * 0.24F), rowH = 30.0F;
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
				const float x = side == 0 ? contents.GetMinX() : contents.GetMaxX() - colW;
				const float total = static_cast<float>(mine.size()) * (rowH + 26.0F);
				float y = imageRect.GetMinY() + (imageRect.GetHeight() - total) * 0.45F;
				for (std::size_t i : mine) {
					Control& c = controls[i];
					c.rect = AABB2(x, y + 22.0F, colW, rowH);
					Handle<Button> b = Handle<Button>::New(manager);
					b->SetBounds(c.rect);
					b->activated = [this, i](UIElement&) { Choose(i); };
					b->mouseEntered = [this, i](UIElement&) { hovered = static_cast<int>(i); };
					b->mouseLeft = [this, i](UIElement&) {
						if (hovered == static_cast<int>(i))
							hovered = -1;
					};
					AddChild(b.GetPointerOrNull());
					c.button = b.GetPointerOrNull();
					y += rowH + 26.0F;
				}
			}

			const float by = contents.GetMaxY() - buttonH;
			const float bx = contents.GetMinX();
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
				b->SetBounds(DialogChrome::ButtonSlot(contents, 1));
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
				b->SetBounds(DialogChrome::ButtonSlot(contents, 0));
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
					SPLog("%s: %s", path.c_str(), reader.getFormatedErrorMessages().c_str());
					return;
				}
				if (root["size"].isArray() && root["size"].size() == 2)
					pictureSize = MakeVector2(static_cast<float>(root["size"][0u].asDouble()), static_cast<float>(root["size"][1u].asDouble()));
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
						c.point = MakeVector2(static_cast<float>(p[0u].asDouble()), static_cast<float>(p[1u].asDouble()));
						c.button = nullptr;
						c.colour = MakeVector4(0.96F, 0.70F, 0.24F, 1.0F);
						// Its name on this controller ("X button" on a left Touch),
						// else the generic one
						const Json::Value& label = root["label"][side][name];
						c.caption = label.isString() ? label.asString()
						                             : std::string(InputCaption(input));
						const Json::Value& col = root["colour"][name];
						if (col.isArray() && col.size() == 3)
							c.colour = MakeVector4(static_cast<float>(col[0u].asDouble()) / 255.0F,
							                       static_cast<float>(col[1u].asDouble()) / 255.0F,
							                       static_cast<float>(col[2u].asDouble()) / 255.0F,
							                       1.0F);
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

			// The view dimmed and the dark band, as every dialog of the game has
			// them (DialogChrome), then the controllers, and a line from each
			// drop-down to its button; the buttons last, over all of it
			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.7F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, b.GetWidth(), b.GetHeight()));
			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.75F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + contents.GetMinY() - 13.0F, b.GetWidth(),
			                           contents.GetHeight() + 27.0F));
			DialogChrome::DrawEdges(*this, contents);
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
			const Vector4 white = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			const Vector4 shadow = MakeVector4(0.0F, 0.0F, 0.0F, 0.5F);
			IFont& heading = ui->GetFontManager().GetHeadingFont();
			heading.DrawShadow(_Tr("Client", "Change buttons"), pos + contents.min, 1.0F, white,
			                   shadow);
			// Each hand under its controller
			const float sx = imageRect.GetWidth() / pictureSize.x;
			for (int h = 0; h < 2; h++) {
				const std::string s = h == 0 ? _Tr("Client", "Left hand") : _Tr("Client", "Right hand");
				const Vector2 sz = font.Measure(s);
				const Vector2 at = pos + MakeVector2(imageRect.GetMinX() + (h == 0 ? 630.0F : 970.0F) * sx -
				                                       sz.x * 0.5F,
				                                     imageRect.GetMinY() + 790.0F * sx);
				font.DrawShadow(s, at, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 0.6F), shadow);
			}
			for (std::size_t ci = 0; ci < controls.size(); ci++) {
				const Control& c = controls[ci];
				// In its marker's colour; while one drop-down is pointed at, the
				// others step back and its button is ringed
				const bool focus = hovered == static_cast<int>(ci);
				const bool dim = hovered >= 0 && !focus;
				// White, as the rest of the game's text: brighter for the one
				// pointed at, fainter for the others meanwhile
				const Vector4 lineCol = MakeVector4(1.0F, 1.0F, 1.0F, focus ? 0.9F : dim ? 0.15F : 0.35F);
				// The button's name over its drop-down
				font.DrawShadow(_Tr("Client", c.caption.c_str()),
				                pos + c.rect.min + MakeVector2(2.0F, -20.0F), 1.0F,
				                MakeVector4(1.0F, 1.0F, 1.0F, focus ? 1.0F : dim ? 0.4F : 0.7F),
				                shadow);
				const Vector2 to = pos + PicturePoint(c.point);
				const bool leftCol = c.rect.GetMinX() < imageRect.GetMinX() + imageRect.GetWidth() * 0.5F;
				const Vector2 from = pos + MakeVector2(leftCol ? c.rect.GetMaxX() : c.rect.GetMinX(),
				                                       c.rect.GetMinY() + c.rect.GetHeight() * 0.5F);
				// A line of small squares: the renderer draws no lines
				const Vector2 d = to - from;
				const float len = d.GetLength();
				const int n = std::max(2, static_cast<int>(len / 3.0F));
				const float w = focus ? 2.0F : 1.0F;
				SetColorNP(r, lineCol);
				for (int k = 0; k <= n; k++) {
					const Vector2 q = from + d * (static_cast<float>(k) / static_cast<float>(n));
					r.DrawImage(nullptr, AABB2(q.x - w, q.y - w, w * 2.0F, w * 2.0F));
				}
				if (focus) {
					// A ring round the button, breathing, so it is found at a glance
					const float scale = imageRect.GetWidth() / pictureSize.x;
					const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					                  std::chrono::steady_clock::now().time_since_epoch())
					                  .count();
					const float t = static_cast<float>(ms % 1000) / 1000.0F;
					const float radius = (24.0F + 4.0F * std::sin(t * 6.2831853F)) * scale;
					const int dots = 48;
					for (int k = 0; k < dots; k++) {
						const float a = static_cast<float>(k) / dots * 6.2831853F;
						const Vector2 q = to + MakeVector2(std::cos(a), std::sin(a)) * radius;
						r.DrawImage(nullptr, AABB2(q.x - 2.0F, q.y - 2.0F, 4.0F, 4.0F));
					}
				}
			}

			UIElement::Render();
		}
	} // namespace client
} // namespace spades
