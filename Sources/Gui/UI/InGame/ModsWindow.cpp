/*
 Copyright (c) 2026 ZeroSpades developers.
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ModsWindow.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <Client/ClientUI.h>
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/Strings.h>
#include <Core/VoxelModel.h>
#include <Gui/ModOverlay.h>
#include <Gui/ModsScreenHelper.h>
#include <Gui/SkinCatalog.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>
#include <Gui/UI/Widgets/MessageBox.h>
#include <Gui/UI/Widgets/VoxelModelPreview.h>

namespace spades {
	namespace client {
		using gui::ModApplyResult;
		using gui::ModOverlay;
		using gui::ModsScreenHelper;
		using gui::SkinCatalog;
		using gui::SkinMod;
		using gui::SkinType;
		using gui::ui::Button;
		using gui::ui::ButtonBase;
		using gui::ui::ElideText;
		using gui::ui::Label;
		using gui::ui::ListView;
		using gui::ui::ListViewModel;
		using gui::ui::SetColorNP;
		using gui::ui::UIElement;
		using gui::ui::UIManager;
		using gui::ui::VoxelModelPreview;

		namespace {
			const Vector4 kAccent = MakeVector4(1.0F, 0.84F, 0.28F, 1.0F);
			constexpr float kCardGap = 8.0F;

			std::string TypeCaption(SkinType type) {
				switch (type) {
					case SkinType::Spade: return _Tr("Client", "Spade");
					case SkinType::Grenade: return _Tr("Client", "Grenade");
					case SkinType::Rifle: return _Tr("Client", "Rifle");
					case SkinType::SMG: return _Tr("Client", "SMG");
					case SkinType::Shotgun: return _Tr("Client", "Shotgun");
					default: return {};
				}
			}

			std::string DisplayName(ModsScreenHelper& helper, const std::string& modName) {
				if (modName.empty())
					return _Tr("Client", "Default");
				const auto name = helper.GetModDisplayName(modName);
				return name.empty() ? modName : name;
			}

			class CategoryButton : public Button {
			public:
				CategoryButton(UIManager* manager) : Button(manager) {}
				void Render() override {
					Button::Render();
					if (toggled) {
						auto& r = GetManager().GetRenderer();
						const auto p = GetScreenPosition();
						SetColorNP(r, kAccent);
						r.DrawImage(nullptr, AABB2(p.x + 1, p.y + 1, 3, size.y - 2));
					}
				}
			};

			/** One independently loaded model; never use RegisterModel's shared winner. */
			class SkinCard : public ButtonBase {
				Handle<SkinCatalog> catalog;
				SkinType type;
				std::string modName;
				std::unique_ptr<VoxelModelPreview> preview;
				bool loaded = false;

			public:
				SkinCard(UIManager* manager, SkinCatalog* catalog, SkinType type,
				         const std::string& modName, const std::string& name)
				    : ButtonBase(manager), catalog(catalog), type(type), modName(modName) {
					caption = name;
				}

				void Render() override {
					auto& r = GetManager().GetRenderer();
					const auto p = GetScreenPosition();
					const bool selected = ModOverlay::GetEquippedSkin(type) == modName;
					SetColorNP(r, selected
					                ? MakeVector4(0.13F, 0.12F, 0.075F, 1)
									: MakeVector4(hover ? 0.10F : 0.055F, hover ? 0.10F : 0.055F,
									              hover ? 0.10F : 0.055F, 1));
					r.DrawImage(nullptr, AABB2(p.x, p.y, size.x, size.y));
					SetColorNP(r, selected ? kAccent : MakeVector4(1, 1, 1, hover ? 0.65F : 0.22F));
					r.DrawOutlinedRect(p.x, p.y, p.x + size.x, p.y + size.y, selected ? 2 : 1);

					// Only visible rows load/mesh models. The row model retains them
					// while scrolling, but offscreen cards do no rendering work.
					if (!loaded) {
						loaded = true;
						try {
							auto voxels = catalog->LoadPreview(type, modName);
							preview.reset(new VoxelModelPreview(*voxels));
						} catch (const std::exception& ex) {
							SPLog("Skin preview unavailable (%s): %s", modName.c_str(), ex.what());
						}
					}
					if (preview && !preview->IsEmpty()) {
						const float yaw = std::fmod(GetManager().time * 0.5F + 0.8F, kTwoPi);
						preview->Render(r, AABB2(p.x + 10, p.y + 18, size.x - 20, size.y - 54),
						                yaw);
					} else if (auto* font = GetFont()) {
						const auto text = ElideText(*font, _Tr("Client", "Preview unavailable"),
						                            size.x - 16, 0.75F);
						const auto measure = font->Measure(text) * 0.75F;
						font->Draw(text,
						           p + MakeVector2((size.x - measure.x) * 0.5F,
						                           (size.y - 28 - measure.y) * 0.5F),
						           0.75F, MakeVector4(0.55F, 0.55F, 0.55F, 1));
					}

					SetColorNP(r, MakeVector4(0, 0, 0, 0.65F));
					r.DrawImage(nullptr, AABB2(p.x + 2, p.y + size.y - 30, size.x - 4, 28));
					if (auto* font = GetFont()) {
						const auto text = ElideText(*font, caption, size.x - 16);
						const auto measure = font->Measure(text);
						font->Draw(text,
						           p + MakeVector2((size.x - measure.x) * 0.5F,
						                           size.y - 30 + (28 - measure.y) * 0.5F),
						           1, selected ? kAccent : MakeVector4(1, 1, 1, 1));
						if (selected) {
							const auto badge =
							  ElideText(*font, _Tr("Client", "Equipped"), size.x - 16, 0.65F);
							font->Draw(badge, p + MakeVector2(8, 4), 0.65F, kAccent);
						}
					}
				}
			};

			class SkinGridRow : public UIElement {
				int columns;

			public:
				SkinGridRow(UIManager* manager, int columns)
				    : UIElement(manager), columns(columns) {}
				void OnResized() override {
					const float w = (size.x - kCardGap) / float(columns);
					int i = 0;
					for (const auto& card : GetChildren()) {
						card->SetBounds(
						  AABB2(kCardGap + float(i++) * w, 4, w - kCardGap, size.y - kCardGap));
					}
				}
			};

			class SkinGridModel : public ListViewModel {
				UIManager* manager;
				Handle<SkinCatalog> catalog;
				SkinType type;
				int columns;
				std::vector<std::pair<std::string, std::string>> entries; // mod name, caption
				std::vector<Handle<UIElement>> rows;
				std::function<void(const std::string&)> select;

			public:
				SkinGridModel(UIManager* manager, SkinCatalog* catalog, ModsScreenHelper& helper,
				              SkinType type, int columns,
				              std::function<void(const std::string&)> select)
				    : manager(manager),
				      catalog(catalog),
				      type(type),
				      columns(columns),
				      select(std::move(select)) {
					for (const SkinMod& mod : catalog->GetMods())
						if (mod.HasType(type))
							entries.emplace_back(mod.name, DisplayName(helper, mod.name));
					std::stable_sort(
					  entries.begin(), entries.end(),
					  [](const auto& a, const auto& b) { return a.second < b.second; });
					entries.insert(entries.begin(), {"", _Tr("Client", "Default")});
					rows.resize((entries.size() + columns - 1) / columns);
				}

				int GetNumRows() override { return static_cast<int>(rows.size()); }
				int GetNumSkins() const { return static_cast<int>(entries.size()) - 1; }

				Handle<UIElement> CreateElement(int row) override {
					if (!rows[row]) {
						auto element = Handle<SkinGridRow>::New(manager, columns);
						const int end =
						  std::min(static_cast<int>(entries.size()), (row + 1) * columns);
						for (int i = row * columns; i < end; ++i) {
							const auto& entry = entries[i];
							auto card = Handle<SkinCard>::New(manager, catalog.GetPointerOrNull(),
							                                  type, entry.first, entry.second);
							card->activated = [this, name = entry.first](UIElement&) {
								select(name);
							};
							element->AddChild(card.GetPointerOrNull());
						}
						rows[row] = element.Cast<UIElement>();
					}
					return rows[row];
				}
			};
		} // namespace

		ModsWindow::ModsWindow(UIElement* owner, ClientUI* ui)
		    : UIElement(&owner->GetManager()), ui(ui), owner(owner) {
			helper = Handle<ModsScreenHelper>::New();
			catalog = Handle<SkinCatalog>::New(*helper, helper->GetEnabledMods());
			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(manager->GetRootElement().GetBounds());
			isMouseInteractive = true;

			const float w = std::min(manager->screenWidth - 32.0F, 1000.0F);
			const float h = std::min(manager->screenHeight - 48.0F, 640.0F);
			contents =
			  AABB2((manager->screenWidth - w) * 0.5F, (manager->screenHeight - h) * 0.5F, w, h);
			sidebar = AABB2(contents.min.x + 12, contents.min.y + 64,
			                Clamp(w * 0.18F, 96.0F, 164.0F), h - 132);
			const float gridX = sidebar.max.x + 10;

			{
				auto title = Handle<Label>::New(manager);
				title->text = _Tr("Client", "Mods");
				title->SetFont(&ui->GetFontManager().GetHeadingFont());
				title->SetBounds(
				  AABB2(contents.min.x + 16, contents.min.y + 8, sidebar.GetWidth(), 28));
				AddChild(title.GetPointerOrNull());
			}
			{
				auto label = Handle<Label>::New(manager);
				categoryTitle = label.GetPointerOrNull();
				label->textColor = kAccent;
				label->SetBounds(
				  AABB2(gridX + 4, contents.min.y + 12, contents.max.x - gridX - 16, 24));
				AddChild(label.GetPointerOrNull());
			}
			{
				auto label = Handle<Label>::New(manager);
				label->SetFont(&ui->GetFontManager().GetSmallGuiFont());
				label->text =
				  ElideText(*label->GetFont(),
				            _Tr("Client", "Choose a skin to equip it immediately."), w - 32);
				label->textColor = MakeVector4(0.65F, 0.65F, 0.65F, 1);
				label->SetBounds(AABB2(contents.min.x + 16, contents.min.y + 37, w - 32, 20));
				AddChild(label.GetPointerOrNull());
			}
			const float categoryStep =
			  std::min(38.0F, (sidebar.GetHeight() - 12) / float(gui::kSkinTypeCount));
			for (std::size_t i = 0; i < gui::kSkinTypeCount; ++i) {
				const auto type = static_cast<SkinType>(i);
				auto button = Handle<CategoryButton>::New(manager);
				button->caption = TypeCaption(type);
				button->SetBounds(AABB2(sidebar.min.x + 5,
				                        sidebar.min.y + 6 + float(i) * categoryStep,
				                        sidebar.GetWidth() - 10, categoryStep - 6));
				button->activated = [this, type](UIElement&) { ShowCategory(type); };
				categoryButtons[i] = button.GetPointerOrNull();
				AddChild(button.GetPointerOrNull());
			}
			{
				auto view = Handle<ListView>::New(manager);
				grid = view.GetPointerOrNull();
				grid->SetBounds(
				  AABB2(gridX, sidebar.min.y, contents.max.x - gridX - 12, sidebar.GetHeight()));
				grid->rowHeight = std::min(164.0F, std::floor(sidebar.GetHeight() * 0.5F));
				columns = Clamp(static_cast<int>(grid->GetItemWidth() / 178.0F), 1, 3);
				AddChild(grid);
			}
			{
				auto label = Handle<Label>::New(manager);
				statusLabel = label.GetPointerOrNull();
				label->SetBounds(AABB2(contents.min.x + 16, contents.max.y - 57, w - 164, 24));
				AddChild(statusLabel);
			}
			{
				auto label = Handle<Label>::New(manager);
				label->SetFont(&ui->GetFontManager().GetSmallGuiFont());
				label->text = ElideText(
				  *label->GetFont(),
				  _Tr("Client", "Enable more skins in the main menu's Mods tab."), w - 164);
				label->textColor = MakeVector4(0.55F, 0.55F, 0.55F, 1);
				label->SetBounds(AABB2(contents.min.x + 16, contents.max.y - 31, w - 164, 22));
				AddChild(label.GetPointerOrNull());
			}
			{
				auto button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Back");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(contents.max.x - 132, contents.max.y - 48, 116, 32));
				button->activated = [this](UIElement&) { Close(); };
				AddChild(button.GetPointerOrNull());
			}
			ShowCategory(SkinType::Spade);
		}

		ModsWindow::~ModsWindow() = default;

		void ModsWindow::ShowCategory(SkinType type) {
			for (std::size_t i = 0; i < gui::kSkinTypeCount; ++i)
				categoryButtons[i]->toggled = i == static_cast<std::size_t>(type);
			categoryTitle->text = _Tr("Client", "{0} skins", TypeCaption(type));
			auto model = Handle<SkinGridModel>::New(
			  &GetManager(), catalog.GetPointerOrNull(), *helper, type, columns,
			  [this, type](const std::string& name) { OnSkinSelected(type, name); });
			grid->SetModel(model.GetPointerOrNull());
			grid->ScrollToTop();
			if (model->GetNumSkins() == 0)
				SetStatus(_Tr("Client", "No enabled skins for this weapon. Default is available."));
			else
				SetStatus(_Tr("Client", "Equipped: {0}",
				              DisplayName(*helper, ModOverlay::GetEquippedSkin(type))));
		}

		void ModsWindow::SetStatus(const std::string& text, bool warning) {
			statusLabel->text = ElideText(*statusLabel->GetFont(), text, statusLabel->size.x);
			statusLabel->textColor = warning ? kAccent : MakeVector4(0.8F, 0.8F, 0.8F, 1);
		}

		void ModsWindow::OnSkinSelected(SkinType type, const std::string& modName) {
			std::string error;
			ModApplyResult result;
			try {
				result = ModOverlay::ApplySkin(type, modName, *ui->GetRenderer(),
				                               *ui->GetAudioDevice(), error);
			} catch (const std::exception& ex) {
				result = ModApplyResult::Refused;
				error = ex.what();
			}
			if (result == ModApplyResult::Applied)
				SetStatus(_Tr("Client", "Equipped: {0}", DisplayName(*helper, modName)),
				          !error.empty());
			else
				SetStatus(_Tr("Client", "Skin not applied. Your previous skin is still equipped."),
				          true);
			if (!error.empty()) {
				const auto title = result == ModApplyResult::Applied
				                     ? _Tr("Client", "Skin equipped.")
				                     : _Tr("Client", "Skin not applied.");
				auto alert =
				  Handle<gui::AlertScreen>::New(this, title + "\n\n" + error, 220.0F, true);
				alert->Run();
			}
		}

		void ModsWindow::Run() {
			// Be the active screen, not an untracked sibling of the pause menu.
			// ClientUI can then close/rebuild it on resize or disconnection too.
			ui->SetActiveUI(this);
		}

		void ModsWindow::Close() {
			Handle<ModsWindow> keepAlive(this);
			ui->SetActiveUI(owner.GetPointerOrNull());
			if (closed)
				closed(*this);
		}

		void ModsWindow::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape")
				Close();
			else
				UIElement::HotKey(key);
		}

		void ModsWindow::Render() {
			auto& r = GetManager().GetRenderer();
			SetColorNP(r, MakeVector4(0, 0, 0, 0.5F));
			r.DrawImage(nullptr, GetScreenBounds());
			SetColorNP(r, MakeVector4(0.035F, 0.035F, 0.035F, 0.98F));
			r.DrawImage(nullptr, contents);
			SetColorNP(r, MakeVector4(1, 1, 1, 0.16F));
			r.DrawOutlinedRect(contents.min.x, contents.min.y, contents.max.x, contents.max.y);
			r.DrawOutlinedRect(sidebar.min.x, sidebar.min.y, sidebar.max.x, sidebar.max.y);
			UIElement::Render();
		}
	} // namespace client
} // namespace spades
