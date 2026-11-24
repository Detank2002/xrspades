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

#include <Client/Client.h>
#include <Client/ClientUI.h>
#include <Client/FTFont.h>
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Client/Player.h>
#include <Client/World.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
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
		using gui::ui::ListViewBase;
		using gui::ui::ListViewModel;
		using gui::ui::SetColorNP;
		using gui::ui::UIElement;
		using gui::ui::UIManager;
		using gui::ui::VoxelModelPreview;

		namespace {
			constexpr float kCardGap = 8.0F;

			std::string TypeCaption(SkinType type) {
				switch (type) {
					case SkinType::Spade: return _Tr("Client", "Spade");
					case SkinType::Grenade: return _Tr("Client", "Grenade");
					case SkinType::Rifle: return _Tr("Client", "Rifle");
					case SkinType::SMG: return _Tr("Client", "SMG");
					case SkinType::Shotgun: return _Tr("Client", "Shotgun");
					case SkinType::Font: return _Tr("Client", "Font");
					case SkinType::SFX: return _Tr("Client", "SFX");
					default: return {};
				}
			}

			std::string DisplayName(ModsScreenHelper& helper, const std::string& modName) {
				if (modName.empty())
					return _Tr("Client", "Default");
				const auto name = helper.GetModDisplayName(modName);
				return name.empty() ? modName : name;
			}

			/** Independently loaded previews, never RegisterModel's mounted winner. */
			class SkinCard : public ButtonBase {
				Handle<SkinCatalog> catalog;
				SkinType type;
				std::string modName;
				std::unique_ptr<VoxelModelPreview> preview;
				Handle<IFont> fontPreview;
				Handle<IImage> image;
				bool loaded = false;
				float animationStart = 0;
				float initialYaw;
				int renderedFrame = -1;

				void Load() {
					loaded = true;
					animationStart = GetManager().time;
					try {
						if (gui::IsWeaponSkin(type)) {
							auto scene = catalog->LoadPreviewScene(type, modName);
							if (!scene.diagnostic.empty())
								SPLog("Mod preview fallback (%s): %s", modName.c_str(),
								      scene.diagnostic.c_str());
							preview = std::make_unique<VoxelModelPreview>(scene);
						} else if (type == SkinType::Font) {
							if (auto bytes = catalog->LoadFontPreview(modName)) {
								auto set = std::make_shared<ngclient::FTFontSet>();
								set->AddFaceBytes(std::move(*bytes), modName);
								fontPreview =
								  Handle<ngclient::FTFont>::New(&GetManager().GetRenderer(),
								                                std::move(set), 40.0F, 40.0F)
								    .Cast<IFont>();
							}
						}
					} catch (const std::exception& ex) {
						// A damaged/complex preview cannot prevent equipping a usable mod.
						SPLog("Mod preview unavailable (%s): %s", modName.c_str(), ex.what());
					}
				}

				void DrawModel(IRenderer& r, const AABB2& bounds) {
					if (!preview || preview->IsEmpty() || bounds.GetWidth() <= 0 ||
					    bounds.GetHeight() <= 0)
						return;
					const float elapsed = std::max(0.0F, GetManager().time - animationStart);
					const int frame = int(elapsed * 24.0F);
					const float density =
					  std::min({std::max(1.0F, r.ScreenPixelRatio()), 2.0F,
					            512.0F / bounds.GetWidth(), 512.0F / bounds.GetHeight()});
					const int w = std::max(1, int(std::floor(bounds.GetWidth() * density)));
					const int h = std::max(1, int(std::floor(bounds.GetHeight() * density)));
					try {
						const bool resized =
						  !image || int(image->GetWidth()) != w || int(image->GetHeight()) != h;
						if (resized || renderedFrame != frame) {
							const float yaw =
							  std::fmod(initialYaw + float(frame) * (0.4F / 24), kTwoPi);
							auto pixels = preview->Rasterize(w, h, yaw);
							auto bitmap = Handle<Bitmap>::New(pixels.data(), w, h);
							if (resized)
								image = r.CreateImage(*bitmap);
							else
								image->Update(*bitmap, 0, 0);
							renderedFrame = frame;
						}
						if (image) {
							SetColorNP(r, MakeVector4(1, 1, 1, 1));
							r.DrawImage(image, bounds);
						}
					} catch (const std::exception& ex) {
						SPLog("Mod thumbnail unavailable (%s): %s", modName.c_str(), ex.what());
						preview.reset();
						image = {};
					}
				}

				void DrawFont(const AABB2& bounds) {
					auto* font = fontPreview ? fontPreview.GetPointerOrNull() : GetFont();
					if (!font || bounds.GetWidth() <= 12 || bounds.GetHeight() <= 12)
						return;
					const auto measure = font->Measure("Aa");
					const float scale =
					  std::min({fontPreview ? 1.0F : 2.0F,
					            (bounds.GetWidth() - 12) / std::max(1.0F, measure.x),
					            (bounds.GetHeight() - 12) / std::max(1.0F, measure.y)});
					font->DrawShadow("Aa", (bounds.min + bounds.max - measure * scale) * 0.5F,
					                 scale, MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.4F));
				}

				void DrawSound(IRenderer& r, const AABB2& bounds) {
					const auto center = (bounds.min + bounds.max) * 0.5F;
					const float scale =
					  std::min({1.0F, bounds.GetWidth() / 80, bounds.GetHeight() / 60});
					auto at = [&](float x, float y) { return center + MakeVector2(x, y) * scale; };
					SetColorNP(r, MakeVector4(1, 1, 1, 0.9F));
					r.DrawImage(nullptr, AABB2(at(-25, -8), at(-15, 8)));
					r.DrawFilledTriangle(at(-17, -8), at(-5, -18), at(-5, 18));
					r.DrawFilledTriangle(at(-17, -8), at(-5, 18), at(-17, 8));
					for (float radius : {15.0F, 25.0F})
						for (int segment = 0; segment < 12; ++segment) {
							const float a = -0.8F + float(segment) * (1.6F / 12);
							const float b = a + 1.6F / 12;
							r.DrawLine(at(-4 + radius * std::cos(a), radius * std::sin(a)),
							           at(-4 + radius * std::cos(b), radius * std::sin(b)),
							           2 * scale);
						}
				}

			public:
				SkinCard(UIManager* manager, SkinCatalog* catalog, SkinType type,
				         const std::string& modName, const std::string& name, int index)
				    : ButtonBase(manager),
				      catalog(catalog),
				      type(type),
				      modName(modName),
				      initialYaw((type == SkinType::Spade ? 0.35F : 1.15F) +
					             float(index % 7) * 0.1F) {
					caption = name;
				}

				void Render() override {
					auto& r = GetManager().GetRenderer();
					const auto p = GetScreenPosition();
					const bool selected = ModOverlay::GetEquippedSkin(type) == modName;
					// The pause menu's Button palette, including its selected state.
					const auto color = selected || (pressed && hover)
					                     ? MakeVector4(0.7F, 0.7F, 0.7F, 0.9F)
					                   : hover ? MakeVector4(0.4F, 0.4F, 0.4F, 0.7F)
					                           : MakeVector4(0.2F, 0.2F, 0.2F, 0.5F);
					SetColorNP(r, color);
					r.DrawFilledRect(p.x + 1, p.y + 1, p.x + size.x - 1, p.y + size.y - 1);
					SetColorNP(r, MakeVector4(0, 0, 0, color.w));
					r.DrawOutlinedRect(p.x + 1, p.y + 1, p.x + size.x - 1, p.y + size.y - 1);

					// Only visible rows load/rasterize anything. One cached image per
					// card, at 24 Hz, rather than thousands of UI draws every frame.
					if (!loaded)
						Load();
					const AABB2 area(p.x + 10, p.y + 8, size.x - 20, size.y - 44);
					if (gui::IsWeaponSkin(type))
						DrawModel(r, area);
					else if (type == SkinType::Font)
						DrawFont(area);
					else if (type == SkinType::SFX)
						DrawSound(r, area);

					if (auto* font = GetFont()) {
						const auto text = ElideText(*font, caption, size.x - 16);
						const auto measure = font->Measure(text);
						font->DrawShadow(text,
						                 p + MakeVector2((size.x - measure.x) * 0.5F,
						                                 size.y - 30 + (24 - measure.y) * 0.5F),
						                 1, MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.4F));
					}
				}
			};

			class SkinGridRow : public UIElement {
				int columns;

			public:
				SkinGridRow(UIManager* manager, int columns)
				    : UIElement(manager), columns(columns) {}
				void OnResized() override {
					const float w = size.x / float(columns);
					int i = 0;
					for (const auto& card : GetChildren())
						card->SetBounds(AABB2(float(i++) * w, 0, w - kCardGap, size.y - kCardGap));
				}
			};

			class SkinGridModel : public ListViewModel {
				UIManager* manager;
				Handle<SkinCatalog> catalog;
				SkinType type;
				int columns;
				std::vector<std::pair<std::string, std::string>> entries;
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
				int GetSelectedRow() const {
					const auto selected = ModOverlay::GetEquippedSkin(type);
					for (std::size_t i = 0; i < entries.size(); ++i)
						if (entries[i].first == selected)
							return int(i) / columns;
					return 0;
				}

				Handle<UIElement> CreateElement(int row) override {
					if (!rows[row]) {
						auto element = Handle<SkinGridRow>::New(manager, columns);
						const int end =
						  std::min(static_cast<int>(entries.size()), (row + 1) * columns);
						for (int i = row * columns; i < end; ++i) {
							const auto& entry = entries[i];
							auto card = Handle<SkinCard>::New(manager, catalog.GetPointerOrNull(),
							                                  type, entry.first, entry.second, i);
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
			gui::SkinPreviewOptions previewOptions;
			// Read-only snapshot: a preview ConfigItem never reaches Settings or
			// changes an optional sight/color used by the actual held weapon.
			for (const auto& name : Settings::GetInstance()->GetAllItemNames()) {
				Settings::ItemHandle item(name, nullptr);
				previewOptions.settings.emplace(name, static_cast<std::string>(item));
			}
			if (ui->client)
				if (auto* world = ui->client->GetWorld())
					if (auto player = world->GetLocalPlayer())
						previewOptions.teamColor = ConvertColorRGB(player->GetColor());
			catalog = Handle<SkinCatalog>::New(*helper, helper->GetEnabledMods(),
			                                   std::move(previewOptions));
			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(manager->GetRootElement().GetBounds());
			isMouseInteractive = true;

			const float w = std::min(manager->screenWidth - 32.0F, 1000.0F);
			const float h = std::min(manager->screenHeight - 48.0F, 640.0F);
			contents =
			  AABB2((manager->screenWidth - w) * 0.5F, (manager->screenHeight - h) * 0.5F, w, h);
			const AABB2 sidebar(contents.min.x + 12, contents.min.y + 48,
			                    Clamp(w * 0.18F, 96.0F, 164.0F), h - 108);
			const float gridX = sidebar.max.x + 12;

			{
				auto title = Handle<Label>::New(manager);
				title->text = _Tr("Client", "Mods");
				title->SetFont(&ui->GetFontManager().GetHeadingFont());
				title->shadowColor = MakeVector4(0, 0, 0, 0.4F);
				title->SetBounds(AABB2(contents.min.x + 16, contents.min.y + 12, w - 32, 28));
				AddChild(title.GetPointerOrNull());
			}
			const float step =
			  std::min(32.0F, (sidebar.GetHeight() - 8) / float(gui::kSkinTypeCount));
			for (std::size_t i = 0; i < gui::kSkinTypeCount; ++i) {
				const auto type = static_cast<SkinType>(i);
				auto button = Handle<Button>::New(manager);
				button->caption = TypeCaption(type);
				const float gap = i >= gui::kWeaponSkinTypeCount ? 8.0F : 0.0F;
				button->SetBounds(AABB2(sidebar.min.x, sidebar.min.y + float(i) * step + gap,
				                        sidebar.GetWidth(), step - 2));
				button->activated = [this, type](UIElement&) { ShowCategory(type); };
				categoryButtons[i] = button.GetPointerOrNull();
				AddChild(button.GetPointerOrNull());
			}
			{
				auto view = Handle<ListViewBase>::New(manager);
				grid = view.GetPointerOrNull();
				grid->SetBounds(
				  AABB2(gridX, sidebar.min.y, contents.max.x - gridX - 12, sidebar.GetHeight()));
				const int visibleRows = Clamp(int(sidebar.GetHeight() / 150.0F), 1, 3);
				grid->rowHeight = std::floor(sidebar.GetHeight() / float(visibleRows));
				columns = Clamp(static_cast<int>(grid->GetItemWidth() / 190.0F), 1, 3);
				AddChild(grid);
			}
			{
				auto button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Back");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(contents.max.x - 132, contents.max.y - 44, 116, 30));
				button->activated = [this](UIElement&) { Close(); };
				AddChild(button.GetPointerOrNull());
			}
			ShowCategory(SkinType::Spade);
		}

		ModsWindow::~ModsWindow() = default;

		void ModsWindow::ShowCategory(SkinType type) {
			for (std::size_t i = 0; i < gui::kSkinTypeCount; ++i)
				categoryButtons[i]->toggled = i == static_cast<std::size_t>(type);
			auto model = Handle<SkinGridModel>::New(
			  &GetManager(), catalog.GetPointerOrNull(), *helper, type, columns,
			  [this, type](const std::string& name) { OnSkinSelected(type, name); });
			grid->SetModel(model.GetPointerOrNull());
			grid->ScrollToRow(model->GetSelectedRow());
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
			// No success footer or second Apply step. The card's highlight reads
			// the committed choice, so a refused change never gets a false tick.
			if (result != ModApplyResult::Applied && error.empty())
				error = _Tr("Client", "Your previous selection is still active.");
			if (!error.empty()) {
				const auto title = result == ModApplyResult::Applied
				                     ? _Tr("Client", "Mod selected.")
				                     : _Tr("Client", "Mod not applied.");
				auto alert =
				  Handle<gui::AlertScreen>::New(this, title + "\n\n" + error, 220.0F, true);
				alert->Run();
			}
		}

		void ModsWindow::Run() {
			// ClientUI can close/rebuild this screen on resize/disconnection too.
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
			// Identical overlay/panel colors to ClientMenu, not opaque custom chrome.
			SetColorNP(r, MakeVector4(0, 0, 0, 0.7F));
			r.DrawImage(nullptr, GetScreenBounds());
			SetColorNP(r, MakeVector4(0, 0, 0, 0.5F));
			r.DrawImage(nullptr, contents);
			UIElement::Render();
		}
	} // namespace client
} // namespace spades
