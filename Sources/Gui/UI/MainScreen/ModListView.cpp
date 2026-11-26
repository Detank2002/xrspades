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
#include <cmath>

#include "ModListView.h"
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/Strings.h>
#include <Gui/ModPreview.h>
#include <Gui/ModsScreenHelper.h>
#include <Gui/SkinChoice.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::FormatFileSize;
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		namespace {
			// Maps an official CATEGORY tag to a weapon icon in the game
			// resources. Returns "" for categories with no weapon icon (FONT,
			// SFX, VFX, ...), which are shown as a text pill instead. Matching is
			// against the uppercase tags used by the official repo.
			std::string ModCategoryIcon(const std::string& category) {
				if (category == "SEMI")
					return "Gfx/Hotbar/Rifle.png";
				if (category == "SMG")
					return "Gfx/Hotbar/SMG.png";
				if (category == "SHOTGUN")
					return "Gfx/Hotbar/Shotgun.png";
				if (category == "SPADE")
					return "Gfx/Hotbar/Spade.png";
				if (category == "GRENADE")
					return "Gfx/Hotbar/Grenade.png";
				return std::string{};
			}

			// The mark of a verified official mod; it reads apart from the enabled
			// green. A modified one is told in a warning amber.
			const Vector4 kOfficialColor = MakeVector4(0.45F, 0.75F, 1.0F, 1.0F);
			const Vector4 kModifiedColor = MakeVector4(1.0F, 0.7F, 0.3F, 1.0F);
			constexpr float kOfficialMarkSize = 10.0F;
			constexpr float kOfficialMarkThickness = 2.0F;
		} // namespace

		// -- ModListItem --

		ModListItem::ModListItem(UIManager* manager, const std::string& modName,
		                         const std::string& category, const std::string& displayName,
		                         const std::string& author, std::int64_t totalSize, bool enabled,
		                         bool exists, OfficialPakState official, int orderNum, float checkColWidth,
		                         float orderColWidth, float tagColWidth, float nameColWidth,
		                         float authorColWidth, float sizeColWidth)
		    : ui::ButtonBase(manager),
		      category(category),
		      displayName(displayName),
		      author(author),
		      totalSize(totalSize),
		      enabled(enabled),
		      exists(exists),
		      official(official),
		      orderNum(orderNum),
		      checkColWidth(checkColWidth),
		      orderColWidth(orderColWidth),
		      tagColWidth(tagColWidth),
		      nameColWidth(nameColWidth),
		      authorColWidth(authorColWidth),
		      sizeColWidth(sizeColWidth),
		      modName(modName) {}

		void ModListItem::RenderTag(client::IRenderer& r, client::IFont* font, float cellX,
		                            float cellY, float cellH, const Vector4& fgcolor) {
			// A picture of the mod's model, where it has one
			if (previews) {
				const int slot = previews->FirstModelSlot(modName);
				if (slot >= 0) {
					if (client::IImage* img = previews->Get(r, modName, static_cast<SkinSlot>(slot))) {
						const float h = std::min(cellH - 2.0F, (tagColWidth - 4.0F) * 0.5F);
						const float w = h * 2.0F;
						SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
						r.DrawImage(img, AABB2(cellX + (tagColWidth - w) * 0.5F,
						                       cellY + (cellH - h) * 0.5F, w, h));
						return;
					}
				}
			}
			if (category.empty())
				return;
			std::string iconPath = ModCategoryIcon(category);
			if (!iconPath.empty()) {
				Handle<client::IImage> icon = r.RegisterImage(iconPath.c_str());
				// Fit within the cell height, preserving aspect ratio.
				float h = cellH - 8.0F;
				float w = h * (icon->GetWidth() / icon->GetHeight());
				if (w > tagColWidth - 6.0F) {
					w = tagColWidth - 6.0F;
					h = w * (icon->GetHeight() / icon->GetWidth());
				}
				float ix = cellX + (tagColWidth - w) * 0.5F;
				float iy = cellY + (cellH - h) * 0.5F;
				SetColorNP(r, fgcolor);
				r.DrawImage(icon, AABB2(ix, iy, w, h));
			} else if (font) {
				// Non-weapon category: draw the tag text, centered in the cell.
				float textW = font->Measure(category).x;
				float tx = cellX + (tagColWidth - textW) * 0.5F;
				font->Draw(category, MakeVector2(tx, cellY + 2.0F), 1.0F, fgcolor);
			}
		}

		void ModListItem::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			Vector4 bgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 0.0F);
			// White when disabled, green when enabled, orange when enabled but
			// the mod is no longer on disk.
			Vector4 fgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			if (enabled)
				fgcolor = exists ? MakeVector4(0.4F, 1.0F, 0.4F, 1.0F)
				                 : MakeVector4(1.0F, 0.62F, 0.1F, 1.0F);

			if (pressed && hover) {
				bgcolor.w = 0.3F;
			} else if (hover) {
				bgcolor.w = 0.15F;
			}

			SetColorNP(r, bgcolor);
			r.DrawImage(nullptr, AABB2(pos.x + 1.0F, pos.y + 1.0F, sz.x, sz.y));

			// Checkbox.
			float boxSize = 14.0F;
			float boxX = pos.x + 5.0F;
			float boxY = pos.y + (sz.y - boxSize) * 0.5F;
			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.25F));
			r.DrawImage(nullptr, AABB2(boxX, boxY, boxSize, boxSize));
			if (enabled) {
				SetColorNP(r, fgcolor);
				r.DrawImage(nullptr,
				            AABB2(boxX + 3.0F, boxY + 3.0F, boxSize - 6.0F, boxSize - 6.0F));
			}

			// The favourite star, beside the box
			{
				const Vector2 c = MakeVector2(pos.x + kStarX + kStarW * 0.5F, pos.y + sz.y * 0.5F);
				const float rad = std::min(sz.y * 0.36F, 9.0F);
				Vector2 p[10];
				for (int i = 0; i < 10; i++) {
					const float a = -1.5707963F + 3.14159265F * i / 5.0F;
					const float rr = (i & 1) ? rad * 0.45F : rad;
					p[i] = MakeVector2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
				}
				SetColorNP(r, favorite ? MakeVector4(1.0F, 0.82F, 0.2F, 1.0F)
				                       : MakeVector4(1.0F, 1.0F, 1.0F, hover ? 0.4F : 0.18F));
				for (int i = 0; i < 10; i++)
					r.DrawLine(p[i], p[(i + 1) % 10], favorite ? 2.0F : 1.0F);
				if (favorite)
					for (int i = 0; i < 10; i += 2)
						r.DrawLine(c, p[i], 2.0F);
			}

			// Apply-order number (own column, blank when disabled).
			float x = pos.x + checkColWidth;
			if (enabled && orderNum > 0)
				font->Draw(std::to_string(orderNum), MakeVector2(x + 2.0F, pos.y + 2.0F), 1.0F,
				           fgcolor);

			// Tag column (weapon icon or category pill).
			x = pos.x + checkColWidth + orderColWidth;
			RenderTag(r, font, x, pos.y, sz.y, fgcolor);

			// Mod name. For an unstructured name this holds the full filename and
			// runs into the author column, matching the previous behaviour.
			x = pos.x + checkColWidth + orderColWidth + tagColWidth + 2.0F;
			font->Draw(displayName, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);

			// Author.
			x += nameColWidth;
			if (!author.empty())
				font->Draw(author, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);

			// Size.
			x += authorColWidth;
			font->Draw(exists ? FormatFileSize(totalSize) : "-", MakeVector2(x, pos.y + 2.0F), 1.0F,
			           fgcolor);

			// Official: a check mark for a verified pak, drawn as two strokes so it
			// does not depend on the font carrying a check glyph; "Modified" for an
			// official pak changed since it was installed; nothing for the
			// player's own.
			x += sizeColWidth;
			if (official == OfficialPakState::Modified) {
				font->Draw(_Tr("MainScreen", "Modified"), MakeVector2(x, pos.y + 2.0F), 1.0F,
				           kModifiedColor);
			} else if (official == OfficialPakState::Verified) {
				const float s = kOfficialMarkSize;
				const float mx = x + 4.0F;
				const float my = pos.y + (sz.y - s) * 0.5F;
				const Vector2 start = MakeVector2(mx, my + s * 0.55F);
				const Vector2 corner = MakeVector2(mx + s * 0.38F, my + s * 0.9F);
				const Vector2 end = MakeVector2(mx + s, my + s * 0.1F);
				SetColorNP(r, kOfficialColor);
				r.DrawLine(start, corner, kOfficialMarkThickness);
				r.DrawLine(corner, end, kOfficialMarkThickness);
			}
		}

		void ModListItem::MouseDown(ui::MouseButton button, Vector2 clientPosition) {
			starPressed = button == ui::MouseButton::Left && favoriteToggled &&
			              clientPosition.x >= kStarX && clientPosition.x < kStarX + kStarW;
			ButtonBase::MouseDown(button, clientPosition);
		}

		void ModListItem::OnActivated() {
			if (starPressed) {
				starPressed = false;
				favoriteToggled(modName);
				return;
			}
			ButtonBase::OnActivated();
		}

		// -- ModListModel --

		ModListModel::ModListModel(UIManager* manager, ModsScreenHelper* helper,
		                           std::vector<std::string> list, std::vector<int> orders,
		                           std::vector<bool> exists, float checkColWidth,
		                           float orderColWidth, float tagColWidth, float nameColWidth,
		                           float authorColWidth, float sizeColWidth)
		    : manager(manager),
		      helper(helper),
		      list(std::move(list)),
		      orders(std::move(orders)),
		      exists(std::move(exists)),
		      checkColWidth(checkColWidth),
		      orderColWidth(orderColWidth),
		      tagColWidth(tagColWidth),
		      nameColWidth(nameColWidth),
		      authorColWidth(authorColWidth),
		      sizeColWidth(sizeColWidth) {
			// Favourites first, in the order they were
			const std::vector<std::string> fav = FavoriteMods::Load();
			std::vector<std::size_t> order(this->list.size());
			for (std::size_t i = 0; i < order.size(); i++)
				order[i] = i;
			for (std::size_t i = 0; i < this->list.size(); i++)
				favorites.push_back(std::find(fav.begin(), fav.end(), this->list[i]) != fav.end());
			std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
				return favorites[a] && !favorites[b];
			});
			std::vector<std::string> l;
			std::vector<int> o;
			std::vector<bool> e, f;
			for (std::size_t i : order) {
				l.push_back(this->list[i]);
				o.push_back(this->orders[i]);
				e.push_back(this->exists[i]);
				f.push_back(favorites[i]);
			}
			this->list = std::move(l);
			this->orders = std::move(o);
			this->exists = std::move(e);
			favorites = std::move(f);
			itemElements.resize(this->list.size());
		}

		void ModListModel::OnItemClicked(UIElement& sender) {
			ModListItem* item = dynamic_cast<ModListItem*>(&sender);
			if (item && itemActivated)
				itemActivated(item->modName);
		}

		Handle<UIElement> ModListModel::CreateElement(int row) {
			if (!itemElements[row]) {
				const std::string& name = list[row];
				bool ex = exists[row];
				std::int64_t size = ex ? helper->GetModTotalSize(name) : 0;
				std::string category = helper->GetModCategory(name);
				std::string displayName = helper->GetModDisplayName(name);
				std::string author = helper->GetModAuthor(name);
				Handle<ModListItem> item = Handle<ModListItem>::New(
				    manager, name, category, displayName, author, size, orders[row] > 0, ex,
				    helper->GetModOfficialState(name), orders[row], checkColWidth, orderColWidth,
				    tagColWidth, nameColWidth, authorColWidth, sizeColWidth);
				item->activated = [this](UIElement& s) { OnItemClicked(s); };
				item->favorite = favorites[row];
				item->previews = previews;
				if (favoriteToggled)
					item->favoriteToggled = [this](const std::string& m) { favoriteToggled(m); };
				itemElements[row] = item;
			}
			return itemElements[row].Cast<UIElement>();
		}

		// -- ModListHeader --

		void ModListHeader::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			font->Draw(text, pos + MakeVector2(2.0F, (sz.y - font->Measure(text).y) * 0.5F), 1.0F,
			           MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
		}

		// -- ModsProgressBar --

		void ModsProgressBar::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			float f = fraction;
			if (f < 0.0F)
				f = 0.0F;
			if (f > 1.0F)
				f = 1.0F;

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.12F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.55F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x * f, sz.y));
		}
	} // namespace gui
} // namespace spades
