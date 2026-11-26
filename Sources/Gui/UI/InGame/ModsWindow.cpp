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

#include <algorithm>
#include <vector>

#include "ModsWindow.h"
#include <Client/ClientUI.h>
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/Strings.h>
#include <Gui/ModOverlay.h>
#include <Gui/ModPreview.h>
#include <Gui/ModsScreenHelper.h>
#include <Gui/SkinChoice.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/MainScreen/ModListView.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/ButtonBase.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>
#include <Gui/UI/Widgets/MessageBox.h>

namespace spades {
	namespace client {
		using gui::ConfirmScreen;
		using gui::FavoriteMods;
		using gui::kNumSkinSlots;
		using gui::ModApplyResult;
		using gui::ModListHeader;
		using gui::ModListModel;
		using gui::ModOverlay;
		using gui::ModPreviewCache;
		using gui::ModsScreenHelper;
		using gui::SkinChoices;
		using gui::SkinSlot;
		using gui::ui::Button;
		using gui::ui::ButtonBase;
		using gui::ui::Label;
		using gui::ui::ListView;
		using gui::ui::ListViewModel;
		using gui::ui::SetColorNP;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		namespace {
			// The main menu's Mods tab columns.
			const float kColumns[] = {60.0F, 90.0F, 56.0F, 200.0F, 150.0F, 100.0F};

			constexpr float kCardW = 168.0F, kCardH = 118.0F, kCardGap = 6.0F;

			const char* const kSlotIcons[kNumSkinSlots] = {
			  "Gfx/Hotbar/Spade.png", "Gfx/Hotbar/Block.png", "Gfx/Hotbar/Grenade.png",
			  "Gfx/Hotbar/Rifle.png", "Gfx/Hotbar/SMG.png",   "Gfx/Hotbar/Shotgun.png"};

			std::string SlotCaption(int slot) {
				switch (slot) {
					case 0: return _Tr("Client", "Spade");
					case 1: return _Tr("Client", "Block");
					case 2: return _Tr("Client", "Grenade");
					case 3: return _Tr("Client", "Rifle");
					case 4: return _Tr("Client", "SMG");
					case 5: return _Tr("Client", "Shotgun");
				}
				return _Tr("Client", "All");
			}

			// A five-pointed star, its outline; brighter and bolder when on
			void DrawStar(client::IRenderer& r, Vector2 c, float radius, bool on) {
				Vector2 p[10];
				for (int i = 0; i < 10; i++) {
					const float a = -1.5707963F + 3.14159265F * i / 5.0F;
					const float rr = (i & 1) ? radius * 0.45F : radius;
					p[i] = MakeVector2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
				}
				SetColorNP(r, on ? MakeVector4(1.0F, 0.82F, 0.2F, 1.0F)
				                 : MakeVector4(1.0F, 1.0F, 1.0F, 0.35F));
				for (int i = 0; i < 10; i++)
					r.DrawLine(p[i], p[(i + 1) % 10], on ? 2.0F : 1.0F);
				if (on) // filled enough to read at a glance
					for (int i = 0; i < 10; i += 2)
						r.DrawLine(c, p[i], 2.0F);
			}

			// The star in a card's corner: marks a favourite
			class FavoriteStar : public ButtonBase {
			public:
				bool on = false;
				FavoriteStar(UIManager* m) : ButtonBase(m) {}
				void Render() override {
					client::IRenderer& r = GetManager().GetRenderer();
					const Vector2 pos = GetScreenPosition();
					if (hover) {
						SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.15F));
						r.DrawImage(nullptr, AABB2(pos.x, pos.y, size.x, size.y));
					}
					DrawStar(r, pos + size * 0.5F, size.x * 0.36F, on);
				}
			};

			/** One mod, or the stock files (an empty name), for a tool */
			class SkinCard : public ButtonBase {
				ModPreviewCache* cache; // weak
				FavoriteStar* star = nullptr;

			public:
				std::string mod;
				std::string title;
				int slot; // the tool its picture is of; -1: it has no model
				bool selected = false;

				SkinCard(UIManager* m, ModPreviewCache* cache, const std::string& mod,
				         const std::string& title, int slot, bool favorite)
				    : ButtonBase(m), cache(cache), mod(mod), title(title), slot(slot) {
					if (!mod.empty()) {
						Handle<FavoriteStar> s = Handle<FavoriteStar>::New(m);
						s->on = favorite;
						s->SetBounds(AABB2(kCardW - 30.0F, 2.0F, 28.0F, 28.0F));
						star = s.GetPointerOrNull();
						AddChild(star);
					}
				}
				FavoriteStar* GetStar() { return star; }

				void Render() override {
					client::IRenderer& r = GetManager().GetRenderer();
					const Vector2 pos = GetScreenPosition();
					const Vector2 sz = size;
					float bg = selected ? 0.32F : (pressed && hover) ? 0.28F : hover ? 0.18F : 0.08F;
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, bg));
					r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));
					if (selected) {
						SetColorNP(r, MakeVector4(0.4F, 1.0F, 0.4F, 0.9F));
						r.DrawImage(nullptr, AABB2(pos.x, pos.y + sz.y - 3.0F, sz.x, 3.0F));
					}

					// The picture, 2:1, or the tool's icon if there is none
					const AABB2 area(pos.x + 6.0F, pos.y + 8.0F, sz.x - 12.0F, (sz.x - 12.0F) * 0.5F);
					client::IImage* img =
					  slot >= 0 ? cache->Get(r, mod, static_cast<SkinSlot>(slot)) : nullptr;
					if (img) {
						SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
						r.DrawImage(img, area);
					} else {
						const int iconSlot = slot >= 0 ? slot : cache->FirstModelSlot(mod);
						if (iconSlot >= 0) {
							Handle<client::IImage> icon = r.RegisterImage(kSlotIcons[iconSlot]);
							const float h = area.GetHeight() * 0.6F;
							const float w = h * icon->GetWidth() / std::max(1.0F, icon->GetHeight());
							SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.35F));
							r.DrawImage(icon, AABB2(area.GetMinX() + (area.GetWidth() - w) * 0.5F,
							                        area.GetMinY() + (area.GetHeight() - h) * 0.5F,
							                        w, h));
						}
					}

					if (client::IFont* font = GetFont()) {
						std::string t = title;
						const float maxW = sz.x - 10.0F;
						while (t.size() > 3 && font->Measure(t).x * 0.85F > maxW)
							t = t.substr(0, t.size() - 4) + "...";
						const float tw = font->Measure(t).x * 0.85F;
						font->Draw(t, MakeVector2(pos.x + (sz.x - tw) * 0.5F, pos.y + sz.y - 26.0F),
						           0.85F,
						           selected ? MakeVector4(0.5F, 1.0F, 0.5F, 1.0F)
						                    : MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
					}
					UIElement::Render(); // the star
				}
			};

			/** A row of cards */
			class SkinGridModel : public ListViewModel {
				UIManager* manager;
				std::vector<Handle<SkinCard>> cards;
				int perRow;
				std::vector<Handle<UIElement>> rows;

			public:
				SkinGridModel(UIManager* m, std::vector<Handle<SkinCard>> cards, int perRow)
				    : manager(m), cards(std::move(cards)), perRow(std::max(1, perRow)) {
					rows.resize((this->cards.size() + this->perRow - 1) / this->perRow);
				}
				int GetNumRows() override { return static_cast<int>(rows.size()); }
				Handle<UIElement> CreateElement(int row) override {
					if (!rows[row]) {
						Handle<UIElement> e = Handle<UIElement>::New(manager);
						for (int i = 0; i < perRow; i++) {
							const std::size_t k = static_cast<std::size_t>(row * perRow + i);
							if (k >= cards.size())
								break;
							cards[k]->SetBounds(
							  AABB2(i * (kCardW + kCardGap), 0.0F, kCardW, kCardH));
							e->AddChild(cards[k].GetPointerOrNull());
						}
						rows[row] = e;
					}
					return rows[row];
				}
			};
		} // namespace

		ModsWindow::ModsWindow(UIElement* owner, ClientUI* ui)
		    : UIElement(&owner->GetManager()), ui(ui), owner(owner) {
			helper = Handle<ModsScreenHelper>::New();
			previews = Handle<ModPreviewCache>::New();
			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(owner->GetBounds());

			float sw = manager->screenWidth;
			float sh = manager->screenHeight;
			float winW = std::min(sw - 16.0F, 720.0F);
			float winH = std::min(sh - 16.0F, 500.0F);
			float winX = (sw - winW) * 0.5F;
			float winY = (sh - winH) * 0.5F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				label->SetBounds(AABB2(winX - 8.0F, winY - 8.0F, winW + 16.0F, winH + 16.0F));
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text = _Tr("Client", "Mods");
				label->SetBounds(AABB2(winX, winY, 120.0F, 30.0F));
				AddChild(label.GetPointerOrNull());
			}
			// Skins | All mods
			for (int i = 0; i < 2; i++) {
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = i == 0 ? _Tr("Client", "Skins") : _Tr("Client", "All mods");
				button->toggle = true;
				button->SetBounds(AABB2(winX + winW - 260.0F + i * 130.0F, winY, 128.0F, 30.0F));
				button->activated = [this, i](UIElement&) { ShowPage(i); };
				pageButtons[i] = button.GetPointerOrNull();
				AddChild(button.GetPointerOrNull());
			}

			const float top = winY + 40.0F;
			const float footerY = winY + winH - 30.0F;
			{
				Handle<Label> label = Handle<Label>::New(manager);
				statusLabel = label.GetPointerOrNull();
				statusLabel->SetBounds(AABB2(winX, footerY - 34.0F, winW, 24.0F));
				AddChild(statusLabel);
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Back");
				button->SetBounds(AABB2(winX + winW - 130.0F, footerY, 130.0F, 30.0F));
				button->activated = [this](UIElement&) { Close(); };
				AddChild(button.GetPointerOrNull());
			}

			// -- Skins: the tools down the left, the cards beside them
			{
				Handle<UIElement> page = Handle<UIElement>::New(manager);
				page->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				skinsPage = page.GetPointerOrNull();
				AddChild(skinsPage);

				const float filterW = 110.0F;
				for (int i = -1; i < kNumSkinSlots; i++) {
					Handle<Button> button = Handle<Button>::New(manager);
					button->caption = SlotCaption(i);
					button->toggle = true;
					button->SetBounds(AABB2(winX, top + (i + 1) * 32.0F, filterW, 30.0F));
					button->activated = [this, i](UIElement&) { SetFilter(i); };
					filterButtons.push_back(button.GetPointerOrNull());
					skinsPage->AddChild(button.GetPointerOrNull());
				}
				Handle<ListView> view = Handle<ListView>::New(manager);
				grid = view.GetPointerOrNull();
				grid->rowHeight = kCardH + kCardGap;
				gridWidth = winW - filterW - 10.0F;
				grid->SetBounds(AABB2(winX + filterW + 10.0F, top, gridWidth, footerY - 40.0F - top));
				skinsPage->AddChild(grid);
			}

			// -- All mods: the main menu's list
			{
				Handle<UIElement> page = Handle<UIElement>::New(manager);
				page->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				listPage = page.GetPointerOrNull();
				AddChild(listPage);

				const std::string titles[] = {_Tr("MainScreen", "Status"),
				                              _Tr("MainScreen", "Apply Order"),
				                              _Tr("MainScreen", "Type"), _Tr("MainScreen", "Mod"),
				                              _Tr("MainScreen", "Author"),
				                              _Tr("MainScreen", "Size")};
				float x = winX;
				for (int i = 0; i < 6; i++) {
					Handle<ModListHeader> header = Handle<ModListHeader>::New(manager);
					header->SetBounds(AABB2(x, top, kColumns[i], 24.0F));
					header->text = titles[i];
					listPage->AddChild(header.GetPointerOrNull());
					x += kColumns[i];
				}
				Handle<ListView> view = Handle<ListView>::New(manager);
				list = view.GetPointerOrNull();
				list->SetBounds(AABB2(winX, top + 26.0F, winW, footerY - 40.0F - (top + 26.0F)));
				listPage->AddChild(list);

				{
					// For players who never downloaded them from the main menu.
					Handle<Button> button = Handle<Button>::New(manager);
					button->caption = _Tr("MainScreen", "Download official mods...");
					button->SetBounds(AABB2(winX, footerY, 220.0F, 30.0F));
					button->activated = [this](UIElement& s) { OnDownload(s); };
					listPage->AddChild(button.GetPointerOrNull());
				}
				{
					Handle<Button> button = Handle<Button>::New(manager);
					button->caption = _Tr("MainScreen", "Disable all");
					button->SetBounds(AABB2(winX + winW - 400.0F, footerY, 130.0F, 30.0F));
					button->activated = [this](UIElement& s) { OnDisableAll(s); };
					listPage->AddChild(button.GetPointerOrNull());
				}
				{
					Handle<Button> button = Handle<Button>::New(manager);
					button->caption = _Tr("MainScreen", "Apply changes");
					button->SetBounds(AABB2(winX + winW - 265.0F, footerY, 130.0F, 30.0F));
					button->activated = [this](UIElement& s) { OnApply(s); };
					listPage->AddChild(button.GetPointerOrNull());
				}
			}

			LoadModList();
			SetFilter(-1);
			ShowPage(0);
		}

		void ModsWindow::ShowPage(int page) {
			skinsPage->visible = page == 0;
			listPage->visible = page == 1;
			for (int i = 0; i < 2; i++)
				pageButtons[i]->toggled = i == page;
			statusLabel->text.clear();
		}

		void ModsWindow::SetFilter(int slot) {
			filter = slot;
			for (std::size_t i = 0; i < filterButtons.size(); i++)
				filterButtons[i]->toggled = static_cast<int>(i) - 1 == slot;
			LoadSkins();
			grid->ScrollToTop();
		}

		// Every installed mod with something for the tool (any tool, for All):
		// the stock ones first for a tool, then favourites, then by name
		void ModsWindow::LoadSkins() {
			UIManager* manager = &GetManager();
			const SkinChoices mounted = ModOverlay::GetMountedChoices();
			const std::vector<std::string> favorites = FavoriteMods::Load();
			auto isFavorite = [&](const std::string& m) {
				return std::find(favorites.begin(), favorites.end(), m) != favorites.end();
			};

			struct Entry {
				std::string mod, title;
				bool favorite;
			};
			std::vector<Entry> entries;
			for (const std::string& name : helper->GetModNames()) {
				const auto& info = previews->GetInfo(name);
				if (filter >= 0 ? !info.slots[filter] : !info.HasAny())
					continue;
				entries.push_back({name, helper->GetModDisplayName(name), isFavorite(name)});
			}
			std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
				if (a.favorite != b.favorite)
					return a.favorite;
				return a.title < b.title;
			});

			std::vector<Handle<SkinCard>> cards;
			auto add = [&](const std::string& mod, const std::string& title, bool favorite) {
				int slot = filter;
				if (slot < 0 || previews->GetInfo(mod).model[slot].empty())
					slot = previews->FirstModelSlot(mod);
				Handle<SkinCard> card =
				  Handle<SkinCard>::New(manager, previews.GetPointerOrNull(), mod, title, slot, favorite);
				// Picked for the tool shown, or (All) for any of its tools
				if (filter >= 0) {
					card->selected = mounted.chosen[filter] && mounted.mod[filter] == mod;
				} else {
					for (int i = 0; i < kNumSkinSlots; i++)
						card->selected =
						  card->selected || (mounted.chosen[i] && !mod.empty() && mounted.mod[i] == mod);
				}
				const int pickSlot = filter;
				card->activated = [this, mod, pickSlot](UIElement&) { OnSkinPicked(mod, pickSlot); };
				if (FavoriteStar* star = card->GetStar())
					star->activated = [this, mod](UIElement&) { OnFavoriteToggled(mod); };
				cards.push_back(card);
			};
			if (filter >= 0)
				add(std::string(), _Tr("Client", "Default"), false);
			for (const Entry& e : entries)
				add(e.mod, e.title, e.favorite);

			const int perRow = std::max(1, static_cast<int>((gridWidth - 16.0F) / (kCardW + kCardGap)));
			const int top = grid->GetTopRowIndex();
			Handle<SkinGridModel> model =
			  Handle<SkinGridModel>::New(manager, std::move(cards), perRow);
			grid->SetModel(model.GetPointerOrNull());
			grid->ScrollToRow(top);
		}

		void ModsWindow::OnSkinPicked(const std::string& mod, int slot) {
			SkinChoices next = ModOverlay::GetMountedChoices();
			if (slot >= 0) {
				next.chosen[slot] = true;
				next.mod[slot] = mod;
			} else {
				// All: for every tool it has something for
				const auto& info = previews->GetInfo(mod);
				for (int i = 0; i < kNumSkinSlots; i++) {
					if (info.slots[i]) {
						next.chosen[i] = true;
						next.mod[i] = mod;
					}
				}
			}
			std::string error;
			switch (ModOverlay::ApplySkinChoices(next, *ui->GetRenderer(), *ui->GetAudioDevice(),
			                                     error)) {
				case ModApplyResult::Applied:
					statusLabel->text = mod.empty()
					                      ? _Tr("Client", "Back to the stock {0}.", SlotCaption(slot))
					                      : _Tr("Client", "{0} is on.", helper->GetModDisplayName(mod));
					break;
				case ModApplyResult::NeedsRestart:
					statusLabel->text =
					  _Tr("Client", "Saved. This change takes effect the next time the game starts.");
					break;
				case ModApplyResult::Refused:
					statusLabel->text =
					  _Tr("Client", "Not applied, its scripts don't build: {0}", error);
					break;
			}
			LoadSkins();
			LoadModList();
		}

		void ModsWindow::OnFavoriteToggled(const std::string& mod) {
			FavoriteMods::Set(mod, !FavoriteMods::Contains(mod));
			LoadSkins();
			LoadModList();
		}

		void ModsWindow::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}

		void ModsWindow::Close() {
			// The parent holds the only strong reference: keep this alive until
			// `closed` has run.
			Handle<ModsWindow> keepAlive(this);
			owner->enable = true;
			GetParent()->RemoveChild(this);
			if (closed)
				closed(*this);
		}

		void ModsWindow::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape")
				Close();
			else
				UIElement::HotKey(key);
		}

		// The same rows as the main menu: every mod on disk, then enabled mods that
		// are gone, so they can be disabled.
		void ModsWindow::LoadModList() {
			std::vector<std::string> names = helper->GetModNames();
			std::vector<std::string> enabled = helper->GetEnabledMods();

			std::vector<std::string> rows;
			std::vector<int> orders;
			std::vector<bool> exists;
			for (const std::string& name : names) {
				auto it = std::find(enabled.begin(), enabled.end(), name);
				rows.push_back(name);
				orders.push_back(it == enabled.end() ? 0
				                                     : static_cast<int>(it - enabled.begin()) + 1);
				exists.push_back(true);
			}
			for (std::size_t i = 0; i < enabled.size(); i++) {
				if (std::find(names.begin(), names.end(), enabled[i]) == names.end()) {
					rows.push_back(enabled[i]);
					orders.push_back(static_cast<int>(i) + 1);
					exists.push_back(false);
				}
			}

			Handle<ModListModel> model = Handle<ModListModel>::New(
			  &GetManager(), helper.GetPointerOrNull(), std::move(rows), std::move(orders),
			  std::move(exists), kColumns[0], kColumns[1], kColumns[2], kColumns[3], kColumns[4],
			  kColumns[5]);
			model->previews = previews.GetPointerOrNull();
			model->favoriteToggled = [this](const std::string& name) { OnFavoriteToggled(name); };
			model->itemActivated = [this](const std::string& name) { OnModToggle(name); };
			const int top = list->GetTopRowIndex();
			list->SetModel(model.GetPointerOrNull());
			list->ScrollToRow(top);
		}

		void ModsWindow::Render() {
			previews->NewFrame();
			CheckDownload();
			UIElement::Render();
		}

		void ModsWindow::OnDownload(UIElement&) {
			if (downloading)
				return;
			std::string body =
			  _Tr("MainScreen", "Download the official ZeroSpades mod pack?") + "\n\n";
			body +=
			  _Tr("MainScreen", "These are mods reviewed and hosted by the ZeroSpades team.") +
			  "\n\n";
			body += _Tr("MainScreen", "Source: ") +
			        "https://github.com/zerospades/zerospades-paks/releases\n";
			body += _Tr("MainScreen", "Files will be saved into your Mods/ folder, overwriting "
			                          "any existing copies.");
			Handle<ConfirmScreen> cs = Handle<ConfirmScreen>::New(
			  this, body, std::min(500.0F, GetManager().screenHeight - 100.0F), true);
			cs->closed = [this](UIElement& s) { OnDownloadConfirmed(s); };
			cs->Run();
		}

		void ModsWindow::OnDownloadConfirmed(UIElement& sender) {
			ConfirmScreen* cs = dynamic_cast<ConfirmScreen*>(&sender);
			if (cs == nullptr || !cs->GetResult() || downloading)
				return;
			downloading = true;
			helper->StartRefresh();
			statusLabel->text = _Tr("MainScreen", "Fetching mod list...");
		}

		void ModsWindow::CheckDownload() {
			if (!downloading)
				return;
			if (helper->PollRefreshState()) {
				downloading = false;
				previews = Handle<ModPreviewCache>::New();
				LoadModList();
				LoadSkins();
				std::string err = helper->GetRefreshMessage();
				statusLabel->text = err.empty()
				                      ? _Tr("Client", "Mods downloaded. Pick some, then press "
				                                      "'Apply changes'.")
				                      : _Tr("MainScreen", "Mod download failed:") + " " + err;
				return;
			}
			// As the main screen's Mods page shows it: the step, and how far
			std::string label = helper->GetRefreshStatus();
			const float fraction = helper->GetRefreshFraction();
			if (fraction > 0.0F)
				label += " " + std::to_string(static_cast<int>(fraction * 100.0F + 0.5F)) + "%";
			statusLabel->text = label.empty() ? _Tr("MainScreen", "Fetching mod list...") : label;
		}

		void ModsWindow::OnModToggle(const std::string& modName) {
			if (downloading)
				return;
			std::vector<std::string> enabled = helper->GetEnabledMods();
			if (std::find(enabled.begin(), enabled.end(), modName) != enabled.end())
				helper->DisableMod(modName);
			else
				helper->EnableMod(modName);
			statusLabel->text = _Tr("Client", "Press 'Apply changes' to apply your mods.");
			LoadModList();
		}

		void ModsWindow::OnDisableAll(UIElement&) {
			if (downloading)
				return;
			helper->ClearEnabledMods();
			statusLabel->text = _Tr("Client", "Press 'Apply changes' to apply your mods.");
			LoadModList();
		}

		void ModsWindow::OnApply(UIElement&) {
			if (downloading)
				return;
			std::string error;
			switch (ModOverlay::Apply(*ui->GetRenderer(), *ui->GetAudioDevice(), error)) {
				case ModApplyResult::Applied:
					statusLabel->text = _Tr("Client", "Mods applied.");
					break;
				case ModApplyResult::NeedsRestart:
					statusLabel->text =
					  _Tr("Client", "Saved. This change takes effect the next time the game starts.");
					break;
				case ModApplyResult::Refused:
					statusLabel->text =
					  _Tr("Client", "Not applied, their scripts don't build: {0}", error);
					break;
			}
			// A refused set is enabled again as it was.
			LoadModList();
		}
	} // namespace client
} // namespace spades
