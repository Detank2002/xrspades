/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

#include <algorithm>

#include "ConfigDesigners.h"
#include "PreferenceView.h"
#include <Client/Fonts.h>
#include <Client/IRenderer.h>
#include <Client/VRState.h>
#include <Core/Settings.h>
#include <Gui/UI/Widgets/Button.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>

// Settings shows every row (1) or the common ones for this screen (0, Simple)
DEFINE_SPADES_SETTING(cl_settingsAdvanced, "0");

namespace spades {
	namespace gui {
		using ui::Label;
		using ui::ListView;
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		// -- HeadingNavButton --

		void HeadingNavButton::SetTarget(ListView* list, int row) {
			targetList = list;
			targetRow = row;
		}

		void HeadingNavButton::OnActivated() {
			if (targetList != nullptr)
				targetList->ScrollToRow(targetRow);
		}

		// -- PreferenceView --

		PreferenceView::PreferenceView(UIElement* owner, const PreferenceViewOptions& options,
		                               client::FontManager* fontManager)
		    : UIElement(&owner->GetManager()), owner(owner), persistedState(options.persistedState) {
			SetBounds(owner->GetBounds());

			UIManager* manager = &GetManager();
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			contentsWidth = sw - 16.0F;
			float maxContentsWidth = PreferenceMetrics::maxContentsWidth;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			contentsHeight = sh - 8.0F;
			float maxContentsHeight = 550.0F;
			if (contentsHeight > maxContentsHeight)
				contentsHeight = maxContentsHeight;

			contentsTop = (sh - contentsHeight) * 0.5F;
			contentsLeft = (sw - contentsWidth) * 0.5F;
			contentsRight = contentsLeft + contentsWidth;

			tabWidth = PreferenceMetrics::tabWidth;
			tabRowHeight = 30.0F;
			// The search box and the Simple / Advanced switch above the tabs
			tabTop = contentsTop + 2.0F + searchHeight + 4.0F + 26.0F + 6.0F;
			tabLeft = contentsLeft + PreferenceMetrics::tabInset;
			tabRight = tabLeft + tabWidth;

			float panelBorderOffset = 14.0F;
			panelTop = contentsTop - panelBorderOffset;
			panelBottom = contentsTop + contentsHeight + panelBorderOffset;

			// draw full background
			if (options.showOverlay) {
				Handle<Label> overlay = Handle<Label>::New(manager);
				overlay->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.7F);
				overlay->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				AddChild(overlay.GetPointerOrNull());
			}

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				label->SetBounds(AABB2(0.0F, panelTop + 1.0F, size.x, (panelBottom - panelTop) - 1.0F));
				AddChild(label.GetPointerOrNull());
			}

			{
				Handle<ui::Field> f = Handle<ui::Field>::New(manager);
				f->placeholder = _Tr("Preferences", "Search settings");
				f->SetBounds(AABB2(tabLeft, contentsTop + 2.0F, tabWidth, searchHeight));
				f->changed = [this](UIElement&) { ApplySearch(); };
				searchField = f.GetPointerOrNull();
				AddChild(searchField);
			}
			{
				const float y = contentsTop + 2.0F + searchHeight + 4.0F;
				const float half = (tabWidth - 2.0F) * 0.5F;
				Handle<ui::Button> s = Handle<ui::Button>::New(manager);
				s->caption = _Tr("Preferences", "Simple");
				s->toggle = true;
				s->SetBounds(AABB2(tabLeft, y, half, 26.0F));
				s->activated = [this](UIElement&) { SetAdvanced(false); };
				AddChild(s.GetPointerOrNull());
				simpleButton = s.GetPointerOrNull();
				Handle<ui::Button> a = Handle<ui::Button>::New(manager);
				a->caption = _Tr("Preferences", "Advanced");
				a->toggle = true;
				a->SetBounds(AABB2(tabLeft + half + 2.0F, y, half, 26.0F));
				a->activated = [this](UIElement&) { SetAdvanced(true); };
				AddChild(a.GetPointerOrNull());
				advancedButton = a.GetPointerOrNull();
			}

			Handle<HeadingNavIndex> gameNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> hudNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> rendererNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> controlsNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> vrNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> miscNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> audioNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> advancedNav = Handle<HeadingNavIndex>::New();

			AddTab(Handle<GameOptionsPanel>::New(manager, options, fontManager,
			                                     gameNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Gameplay"), gameNav.GetPointerOrNull());

			Handle<InterfaceOptions> interfacePanel = Handle<InterfaceOptions>::New(
			    manager, options, fontManager, hudNav.GetPointerOrNull());
			interfacePanel->onEditHUDRequested = [this](UIElement& s) { OnEditHUDRequested(s); };
			AddTab(interfacePanel.GetPointerOrNull(), _Tr("Preferences", "Interface"),
			       hudNav.GetPointerOrNull());

			AddTab(Handle<RendererOptionsPanel>::New(manager, options, fontManager,
			                                         rendererNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Graphics"), rendererNav.GetPointerOrNull());
			AddTab(Handle<AudioOptionsPanel>::New(manager, options, fontManager,
			                                      audioNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Audio"), audioNav.GetPointerOrNull());
			AddTab(Handle<ControlOptionsPanel>::New(manager, options, fontManager,
			                                        controlsNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Controls"), controlsNav.GetPointerOrNull());
			AddTab(Handle<VROptionsPanel>::New(manager, options, fontManager,
			                                   vrNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "VR"), vrNav.GetPointerOrNull());
			AddTab(Handle<MiscOptionsPanel>::New(manager, options, fontManager,
			                                     miscNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Demos"), miscNav.GetPointerOrNull());
			AddTab(Handle<AdvancedOptionsPanel>::New(manager, options, fontManager,
			                                         advancedNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Advanced"), advancedNav.GetPointerOrNull());

			float backButtonTop = tabTop + static_cast<float>(tabs.size()) * tabRowHeight + 5.0F;

			{
				Handle<PreferenceTabButton> button = Handle<PreferenceTabButton>::New(manager);
				button->caption = _Tr("Preferences", "Back");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(tabLeft, backButtonTop, tabWidth, tabRowHeight));
				button->activated = [this](UIElement& s) { OnClosePressed(s); };
				AddChild(button.GetPointerOrNull());
				backButton = button.GetPointerOrNull();
			}

			// build navigation containers in the sidebar
			{
				float btnH = 24.0F;
				float btnY = backButtonTop + tabRowHeight + btnH;
				float btnGap = 2.0F;

				for (size_t i = 0; i < tabs.size(); i++) {
					PreferenceTab* tab = tabs[i].GetPointerOrNull();

					HeadingNavIndex* nav = tab->headingNav.GetPointerOrNull();
					if (nav == nullptr)
						continue;

					int numHeadings = static_cast<int>(nav->entries.size());
					if (numHeadings < 2)
						continue;

					float height = static_cast<float>(numHeadings) * (btnH + btnGap) - btnGap;

					Handle<UIElement> container = Handle<UIElement>::New(manager);
					container->SetBounds(AABB2(tabLeft, btnY, tabWidth, height));
					container->visible = false;

					for (int j = 0; j < numHeadings; j++) {
						const HeadingNavEntry& ent = nav->entries[j];
						Handle<HeadingNavButton> btn = Handle<HeadingNavButton>::New(manager);
						btn->caption = ent.caption;
						btn->SetTarget(nav->list, ent.row);
						btn->SetBounds(
						    AABB2(0.0F, static_cast<float>(j) * (btnH + btnGap), tabWidth, btnH));
						container->AddChild(btn.GetPointerOrNull());
					}

					AddChild(container.GetPointerOrNull());
					tab->navPanel = container.GetPointerOrNull();
				}
			}

			// help box shown in the sidebar space left below the navigation buttons
			helpTop = backButtonTop + tabRowHeight + 12.0F;
			{
				Handle<ui::TextViewer> e = Handle<ui::TextViewer>::New(manager);
				e->SetFont(&fontManager->GetSmallFont());
				e->rowHeight = 12.0F;
				// read-only box: not hoverable, so it never shows the I-beam cursor
				e->isMouseInteractive = false;
				e->parseInlineCode = true;
				e->SetBounds(AABB2(tabLeft, helpTop, tabWidth, panelBottom - 10.0F - helpTop));
				AddChild(e.GetPointerOrNull());
				helpView = e.GetPointerOrNull();

				for (size_t i = 0; i < tabs.size(); i++) {
					HeadingNavIndex* nav = tabs[i]->headingNav.GetPointerOrNull();
					if (nav != nullptr)
						nav->helpHandler = [this](const std::string& s) { SetHelpText(s); };
				}
			}

			RestorePersistedState();
			SetAdvanced((bool)cl_settingsAdvanced);
		}

		void PreferenceView::SetAdvanced(bool advanced) {
			cl_settingsAdvanced = advanced ? 1 : 0;
			simpleButton->toggled = !advanced;
			advancedButton->toggled = advanced;
			ApplySearch(true);
		}

		void PreferenceView::ApplySearch(bool force) {
			std::string q = searchField ? searchField->GetText() : std::string();
			// Trimmed, lower case
			while (!q.empty() && q.back() == ' ')
				q.pop_back();
			while (!q.empty() && q.front() == ' ')
				q.erase(q.begin());
			for (char& c : q)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (q == searchQuery && !force)
				return;
			searchQuery = q;
			// Simple mode: the common rows, and only this screen's kind (a
			// headset's or a monitor's). A search looks through every tier.
			const bool advanced = (bool)cl_settingsAdvanced;
			const bool inHeadset = client::GetVRSystem() != nullptr;
			auto allowed = [&](HeadingNavIndex* nav, std::size_t r) {
				const int plat = r < nav->rowPlatform.size() ? nav->rowPlatform[r] : PlatformBoth;
#if defined(__ANDROID__)
				// No monitor on a standalone headset: its rows never show, in
				// Advanced either (testers set Render Scale there and nothing changed)
				if (plat == PlatformFlat)
					return false;
#endif
				if (advanced)
					return true;
				if (plat == PlatformVR && !inHeadset)
					return false;
				if (plat == PlatformFlat && inHeadset)
					return false;
				const bool adv = r < nav->rowAdvanced.size() && nav->rowAdvanced[r];
				return !adv || !q.empty();
			};

			int firstMatch = -1;
			bool selectedHasMatches = false;
			for (size_t i = 0; i < tabs.size(); i++) {
				HeadingNavIndex* nav = tabs[i]->headingNav.GetPointerOrNull();
				PreferenceTabButton* button = tabs[i]->tabButton;
				if (i >= tabCaptions.size())
					tabCaptions.resize(i + 1);
				if (tabCaptions[i].empty())
					tabCaptions[i] = button->caption;
				if (!nav || !nav->list || nav->rows.empty())
					continue;
				std::vector<Handle<UIElement>> shown;
				int matches = 0;
				{
					// A matching row, with its section's heading over it once; a
					// heading that matches brings its whole section
					int heading = -1;
					bool headingShown = false, sectionMatches = false;
					for (size_t r = 0; r < nav->rows.size(); r++) {
						if (nav->rowHeading[r]) {
							heading = static_cast<int>(r);
							headingShown = false;
							sectionMatches = !q.empty() && nav->rowText[r].find(q) != std::string::npos;
							continue;
						}
						if (!allowed(nav, r))
							continue;
						if (!q.empty() && !sectionMatches &&
						    nav->rowText[r].find(q) == std::string::npos)
							continue;
						if (heading >= 0 && !headingShown) {
							shown.push_back(nav->rows[heading]);
							headingShown = true;
						}
						shown.push_back(nav->rows[r]);
						matches++;
					}
				}
				Handle<StandardPreferenceLayouterModel> model =
				  Handle<StandardPreferenceLayouterModel>::New(shown);
				nav->list->SetModel(model.GetPointerOrNull());
				nav->list->ScrollToTop();
				button->caption = q.empty() ? tabCaptions[i]
				                            : tabCaptions[i] + " (" + std::to_string(matches) + ")";
				button->enable = matches > 0;
				// A tab with nothing to show in Simple mode (VR on a monitor, the
				// Advanced tab) is not shown at all
				button->visible = matches > 0 || !q.empty();
				if (matches > 0 && firstMatch < 0)
					firstMatch = static_cast<int>(i);
				if (static_cast<int>(i) == selectedTabIndex && matches > 0)
					selectedHasMatches = true;
			}
			if (!selectedHasMatches && firstMatch >= 0)
				selectedTabIndex = firstMatch;
			// The visible tabs one under another, the Back button after them
			float y = tabTop;
			for (auto& tab : tabs) {
				if (!tab->tabButton->visible)
					continue;
				tab->tabButton->SetBounds(AABB2(tabLeft, y, tabWidth, tabRowHeight));
				y += tabRowHeight;
			}
			if (backButton)
				backButton->SetBounds(AABB2(tabLeft, y + 5.0F, tabWidth, tabRowHeight));
			// The help goes under Back, wherever the tabs now end
			helpTop = y + 5.0F + tabRowHeight + 12.0F;
			UpdateTabs();
		}

		ListView* PreferenceView::GetTabList(int idx) {
			if (idx < 0 || idx >= static_cast<int>(tabs.size()))
				return nullptr;
			UIElement* view = tabs[idx]->view;
			if (view == nullptr)
				return nullptr;
			for (const Handle<UIElement>& child : view->GetChildren()) {
				ListView* list = dynamic_cast<ListView*>(child.GetPointerOrNull());
				if (list != nullptr)
					return list;
			}
			return nullptr;
		}

		void PreferenceView::RestorePersistedState() {
			if (!persistedState)
				return;

			if (persistedState->selectedTabIndex >= 0 &&
			    persistedState->selectedTabIndex < static_cast<int>(tabs.size()))
				selectedTabIndex = persistedState->selectedTabIndex;

			size_t limit = tabs.size();
			if (persistedState->tabScrollRows.size() < limit)
				limit = persistedState->tabScrollRows.size();
			for (size_t i = 0; i < limit; i++) {
				ListView* list = GetTabList(static_cast<int>(i));
				if (list != nullptr)
					list->ScrollToRow(persistedState->tabScrollRows[i]);
			}
		}

		void PreferenceView::SavePersistedState() {
			if (!persistedState)
				return;

			persistedState->selectedTabIndex = selectedTabIndex;
			persistedState->tabScrollRows.resize(tabs.size());
			for (size_t i = 0; i < tabs.size(); i++) {
				ListView* list = GetTabList(static_cast<int>(i));
				persistedState->tabScrollRows[i] = (list != nullptr) ? list->GetTopRowIndex() : 0;
			}
		}

		void PreferenceView::AddTab(UIElement* view, const std::string& caption,
		                            HeadingNavIndex* nav) {
			UIManager* manager = &GetManager();
			Handle<PreferenceTabButton> button = Handle<PreferenceTabButton>::New(manager);
			button->toggle = true;
			button->caption = caption;
			button->SetBounds(AABB2(tabLeft, tabTop + static_cast<float>(tabs.size()) * tabRowHeight,
			                        tabWidth, tabRowHeight));
			button->activated = [this](UIElement& s) { OnTabButtonActivated(s); };
			// From the tabs' top to the contents' bottom. It was contentsHeight
			// tall from tabTop, which the search box and the Simple / Advanced
			// switch moved down: the list's last rows hung below the panel, out
			// of reach of the scroll bar and drawn over its edge.
			const float viewHeight = contentsTop + contentsHeight - 6.0F - tabTop;
			view->SetBounds(AABB2(tabRight, tabTop, contentsRight - tabRight, viewHeight));
			view->visible = false;
			// The layouter sized its list from the screen; the list fills the view
			for (const Handle<UIElement>& child : view->GetChildren())
				if (ListView* list = dynamic_cast<ListView*>(child.GetPointerOrNull())) {
					AABB2 lb = list->GetBounds();
					list->SetBounds(AABB2(lb.GetMinX(), lb.GetMinY(), lb.GetWidth(), viewHeight));
				}

			Handle<PreferenceTab> tab =
			    Handle<PreferenceTab>::New(view, button.GetPointerOrNull());
			tab->headingNav = nav;
			AddChild(view);
			AddChild(button.GetPointerOrNull());
			tabs.push_back(tab);
		}

		void PreferenceView::OnTabButtonActivated(UIElement& sender) {
			for (size_t i = 0; i < tabs.size(); i++) {
				if (static_cast<UIElement*>(tabs[i]->tabButton) == &sender) {
					selectedTabIndex = static_cast<int>(i);
					UpdateTabs();
				}
			}
		}

		void PreferenceView::UpdateTabs() {
			for (size_t i = 0; i < tabs.size(); i++) {
				bool selected = selectedTabIndex == static_cast<int>(i);
				tabs[i]->tabButton->toggled = selected;
				tabs[i]->view->visible = selected;
				// The section buttons jump to rows a search has filtered away
				if (tabs[i]->navPanel != nullptr)
					tabs[i]->navPanel->visible =
					  selected && searchQuery.empty() && (bool)cl_settingsAdvanced;
			}

			// switching tabs must not keep the previous tab's help text
			SetHelpText(std::string());
		}

		void PreferenceView::SetHelpText(const std::string& text) {
			if (helpView == nullptr || selectedTabIndex < 0 ||
			    selectedTabIndex >= static_cast<int>(tabs.size()))
				return;

			if (text.empty()) {
				helpView->SetText(text);
				return;
			}

			// start right below this tab's nav buttons (or below Back when it has none)
			float top = helpTop;
			ui::UIElement* navPanel = tabs[selectedTabIndex]->navPanel;
			if (navPanel != nullptr && navPanel->visible)
				top = navPanel->GetBounds().max.y + 12.0F;

			// use whatever sidebar height is left
			float height = std::max(0.0F, panelBottom - 10.0F - top);
			helpView->SetBounds(AABB2(tabLeft, top, tabWidth, height));
			helpView->SetText(text);
		}

		void PreferenceView::OnClosePressed(UIElement&) { Close(); }

		void PreferenceView::OnEditHUDRequested(UIElement&) {
			visible = false;
			owner->visible = false;
			Handle<ConfigHUDEdit> overlay = Handle<ConfigHUDEdit>::New(&GetManager());
			overlay->OnDone = [this](UIElement& s) { OnHUDEditDone(s); };
			GetParent()->AddChild(overlay.GetPointerOrNull());
		}

		void PreferenceView::OnHUDEditDone(UIElement& sender) {
			sender.SetParent(nullptr);
			visible = true;
			owner->visible = true;
		}

		void PreferenceView::OnClosed() {
			if (closed)
				closed(*this);
		}

		void PreferenceView::HotKey(const std::string& key) {
			if (key == "Escape") {
				Close();
			} else {
				UIElement::HotKey(key);
			}
		}

		void PreferenceView::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + panelTop, sz.x, 1.0F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + panelBottom, sz.x, 1.0F));

			UIElement::Render();
		}

		void PreferenceView::Close() {
			// Both Esc and the Back button route through Close(), so this is the
			// single save point for the persisted position.
			SavePersistedState();
			owner->enable = true;
			SetParent(nullptr);
			OnClosed();
		}

		void PreferenceView::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}
	} // namespace gui
} // namespace spades
