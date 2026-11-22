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

#include "WeaponWindow.h"

#include <Client/ClientUI.h>
#include <Client/Fonts.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Settings/Formatters.h>
#include <Gui/UI/Settings/PreferenceLayouter.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/DialogChrome.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace client {
		using gui::DialogChrome;
		using gui::Formatter;
		using gui::StandardPreferenceLayouter;
		using gui::ui::Button;
		using gui::ui::Label;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		namespace {
			/** The rows, in a panel the width the Settings tabs are. */
			class WeaponPanel : public UIElement {
			public:
				Handle<StandardPreferenceLayouter> layouter;
				WeaponPanel(UIManager* manager, FontManager* fonts, bool vr) : UIElement(manager) {
					layouter = Handle<StandardPreferenceLayouter>::New(this, fonts);
					StandardPreferenceLayouter& l = *layouter;
					l.AddHeading(_Tr("Client", "Sight"));
					l.AddChoiceField(_Tr("Preferences", "Sight"), "cg_reflexScope",
					                 {_Tr("Preferences", "Iron Sights"), _Tr("Preferences", "Red Dot"),
					                  _Tr("Preferences", "Cross")},
					                 {0, 3, 4});
					l.AddHelp(_Tr("Preferences", "A red dot or cross replaces every gun's iron "
					                             "sights; it sits at infinity, so either eye can "
					                             "use it."));
					if (vr) {
						l.AddToggleField(_Tr("Preferences", "Rifle Scope"), "vr_scope");
						l.AddHelp(_Tr("Preferences",
						              "A magnifying scope on the rifle, seen when an eye looks "
						              "down its iron sights. Off by default: most players aim "
						              "better with the open sights."));
						l.AddHeading(_Tr("Client", "Aiming"));
						l.AddToggleField(_Tr("Preferences", "Laser Pointer"), "vr_laser");
						l.AddSliderField(_Tr("Preferences", "Gun Angle"), "vr_aimTilt", -10, 40, 1,
						                 gui::NumberFormatter(0, _Tr("Preferences", "\xc2\xb0 down")));
						Formatter smooth = gui::NumberFormatter(1, "");
						smooth.custom = [](float value) -> std::string {
							if (value <= 0.0F)
								return _Tr("Preferences", "Off");
							return gui::FormatConfigNumber(1, "", "", 1.0F, value);
						};
						l.AddSliderField(_Tr("Preferences", "Aim Smoothing"), "vr_aimSmooth", 0, 1,
						                 0.1F, smooth);
					} else {
						l.AddSliderField(_Tr("Preferences", "Scope Type"), "cg_pngScope", 0, 4, 1,
						                 gui::ScopeTypeFormatter());
						l.AddHeading(_Tr("Client", "Aiming"));
						l.AddToggleField(_Tr("Preferences", "Full Aim Down Sight"),
						                 "cg_trueAimDownSight");
						l.AddToggleField(_Tr("Preferences", "Hold Aim Down Sight"),
						                 "cg_holdAimDownSight");
						l.AddSliderField(_Tr("Preferences", "ADS Mouse Sens. Scale"),
						                 "cg_zoomedMouseSensScale", 0.05F, 3, 0.05F,
						                 gui::NumberFormatter(2, "x"));
						l.AddChoiceField(_Tr("Preferences", "Crosshair"), "cg_target",
						                 {_Tr("Preferences", "OFF"), _Tr("Preferences", "Default"),
						                  _Tr("Preferences", "Custom")},
						                 {0, 1, 2});
						l.AddHelp(_Tr("Preferences", "Settings, Interface has the crosshair's "
						                             "and the scope reticle's look."));
					}
					l.FinishLayout();
				}
			};
		} // namespace

		WeaponWindow::WeaponWindow(UIElement* owner, ClientUI* ui, bool forVR)
		    : UIElement(&owner->GetManager()), ui(ui), owner(owner) {
			UIManager* manager = &GetManager();
			SetBounds(manager->GetRootElement().GetBounds());
			const float sw = manager->screenWidth;
			const float panelW = gui::PreferenceMetrics::GetPanelWidth(sw);
			const float rows = forVR ? 8.0F : 9.0F;
			const float panelH = rows * 32.0F + 8.0F;
			contents = DialogChrome::Attach(*this, panelW, 40.0F + panelH + 10.0F +
			                                                 DialogChrome::kButtonHeight);
			{
				Handle<Label> l = Handle<Label>::New(manager);
				l->text = _Tr("Client", "Weapon");
				l->SetFont(&ui->GetFontManager().GetHeadingFont());
				l->SetBounds(AABB2(contents.GetMinX(), contents.GetMinY(), contents.GetWidth(), 32.0F));
				AddChild(l.GetPointerOrNull());
			}
			{
				Handle<WeaponPanel> p =
				  Handle<WeaponPanel>::New(manager, &ui->GetFontManager(), forVR);
				p->SetBounds(AABB2(contents.GetMinX() - 10.0F, contents.GetMinY() + 40.0F,
				                   panelW, panelH));
				// The rows' list is sized for the Settings window: fit it to here
				for (const Handle<UIElement>& c : p->GetChildren())
					if (auto* list = dynamic_cast<gui::ui::ListView*>(c.GetPointerOrNull())) {
						const AABB2 b = list->GetBounds();
						list->SetBounds(AABB2(b.GetMinX(), 0.0F, b.GetWidth(), panelH));
					}
				AddChild(p.GetPointerOrNull());
			}
			{
				Handle<Button> b = Handle<Button>::New(manager);
				b->caption = _Tr("Client", "Back");
				b->hotKeyText = _Tr("Client", "[Esc]");
				b->SetBounds(DialogChrome::ButtonSlot(contents, 0));
				b->activated = [this](UIElement&) { Close(); };
				AddChild(b.GetPointerOrNull());
			}
		}

		void WeaponWindow::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}

		void WeaponWindow::Close() {
			Handle<WeaponWindow> keepAlive(this);
			owner->enable = true;
			if (GetParent())
				GetParent()->RemoveChild(this);
			if (closed)
				closed(*this);
		}

		void WeaponWindow::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape")
				Close();
			else
				UIElement::HotKey(key);
		}

		void WeaponWindow::Render() {
			UIElement::Render();
			DialogChrome::DrawEdges(*this, contents);
		}
	} // namespace client
} // namespace spades
