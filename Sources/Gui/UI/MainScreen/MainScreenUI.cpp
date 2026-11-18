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
#include <memory>
#include <random>

#include "CreateProfileScreen.h"
#include "MainMenu.h"
#include "MainScreenUI.h"
#include <Client/Fonts.h>
#include <Client/GameMap.h>
#include <Client/IAudioDevice.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <Gui/MainScreenHelper.h>
#include <Gui/UI/Components/UpdateNotice.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/OfficialMods.h>
#include <Training/MapGen.h>
#include <Training/VoxelMap.h>
#include <Core/MemoryStream.h>
#include <Gui/SelfUpdater.h>
#include <Gui/UpdateChecker.h>

SPADES_SETTING(cg_playerName); // defined in Gui/MainScreen.cpp
DEFINE_SPADES_SETTING(cg_playerNameIsSet, "0");

// The map behind the main menu: "random" (one of the menu maps, a new one
// each launch), a map in Maps/ by name, "generated" (the Training terrain) or
// "Title" (the old one); and how long a lap round it takes (s)
DEFINE_SPADES_SETTING(cg_menuMap, "random");
DEFINE_SPADES_SETTING(cg_menuMapOrbit, "360");

namespace spades {
	namespace gui {
		MainScreenUI::MainScreenUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
		                           client::FontManager* fontManager, MainScreenHelper* helper)
		    : renderer(renderer),
		      audioDevice(audioDevice),
		      fontManager(fontManager),
		      helper(helper) {
			SetupRenderer();

			manager = Handle<ui::UIManager>::New(renderer, audioDevice);
			manager->GetRootElement().SetFont(&fontManager->GetGuiFont());

			// The menu bakes the screen extent into its layout, so a resize has to
			// rebuild it rather than just resize the root.
			manager->screenSizeChanged = [this] { Reload(); };

			Init();

			// Runs once per process; the main screen is rebuilt on every return from
			// a game, and keeps the first answer.
			UpdateChecker::Get().Start();
			OfficialModsChecker::Get().Start();

			// Let the new player choose their IGN
			std::string nameStr = cg_playerName;
			if (nameStr != "" && nameStr != "Deuce")
				cg_playerNameIsSet = 1;

			// prompt window if user has an empty name
			if (nameStr == "")
				cg_playerNameIsSet = 0;

			if (!static_cast<bool>(cg_playerNameIsSet)) {
				Handle<CreateProfileScreen> al =
				    Handle<CreateProfileScreen>::New(mainMenu.GetPointerOrNull(), fontManager);
				al->Run();
			}
		}

		MainScreenUI::~MainScreenUI() {}

		void MainScreenUI::Init() {
			mainMenu = Handle<MainScreenMainMenu>::New(this);
			mainMenu->SetBounds(manager->GetRootElement().GetBounds());
			manager->GetRootElement().AddChild(mainMenu.GetPointerOrNull());
		}

		void MainScreenUI::Reload() {
			// Detach every top-level element, not just the menu: modals attach
			// themselves next to their owner (`owner->GetParent()->AddChild`), so a
			// message box, the settings view or the profile prompt is a sibling of the
			// menu. Leaving one behind would orphan it over a menu it can no longer
			// re-enable. A dialog open across a resize is dismissed instead.
			ui::UIElement& root = manager->GetRootElement();
			while (!root.GetChildren().empty())
				root.RemoveChild(root.GetChildren()[0].GetPointerOrNull());

			// Rebuild through the constructor so the new layout is exactly what a fresh
			// launch at this extent would have produced, then restore what survives.
			MainScreenMainMenuState state = mainMenu->GetState();
			Init();
			mainMenu->SetState(state);
		}

		void MainScreenUI::OnReturnedToMenu() {
			if (mainMenu)
				mainMenu->RefreshVisibleTab();
		}

		void MainScreenUI::SetupRenderer() {
			// The map behind the menu (cg_menuMap): one in Maps/ by name, the
			// Training generator's terrain ("generated"), or the old title map
			// ("Title"), which keeps its old dark, sliding view.
			std::string name = cg_menuMap;
			if (name == "random") {
				// Chosen once a launch: coming back from a game keeps it.
				static const std::string chosen = [] {
					static const char* const kMenuMaps[] = {
					  "Menu/Normandie", "Menu/GhostTown", "Menu/Quarantine",
					  "Menu/MashesswoodIsland"};
					std::random_device rd;
					return std::string(kMenuMaps[rd() % (sizeof(kMenuMaps) / sizeof(kMenuMaps[0]))]);
				}();
				name = chosen;
			}
			titleMap = Handle<client::GameMap>();
			if (name == "generated") {
				host::VoxelMap m;
				host::generate_map(m, 0x5EED);
				const std::vector<uint8_t> bytes = m.save_vxl();
				MemoryStream stream(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				titleMap.Set(client::GameMap::Load(&stream), false);
			} else if (name != "Title" && FileManager::FileExists(("Maps/" + name + ".vxl").c_str())) {
				std::unique_ptr<IStream> stream{
				  FileManager::OpenForReading(("Maps/" + name + ".vxl").c_str())};
				titleMap.Set(client::GameMap::Load(stream.get()), false);
			}
			orbit = static_cast<bool>(titleMap);
			if (!orbit) {
				std::unique_ptr<IStream> stream{FileManager::OpenForReading("Maps/Title.vxl")};
				titleMap.Set(client::GameMap::Load(stream.get()), false);
			}
			renderer->SetGameMap(*titleMap);
			if (orbit) {
				// Daylight: the sky's blue in the distance
				renderer->SetFogColor(MakeVector3(128.0F, 200.0F, 245.0F) / 255.0F);
				renderer->SetFogDistance(128.0F);
				SetupOrbit();
			} else {
				renderer->SetFogColor(MakeVector3(0.1F, 0.1F, 0.1F));
				renderer->SetFogDistance(128.0F);
			}
			time = -1.0F;

			// returned from the client game, so reload the server list.
			if (mainMenu)
				mainMenu->LoadServerList();

			if (manager)
				manager->KeyPanic();
		}

		void MainScreenUI::SetupOrbit() {
			client::GameMap& map = *titleMap;
			const float cx = map.Width() * 0.5F, cy = map.Height() * 0.5F;
			orbitRadius = std::min(map.Width(), map.Height()) * 0.15F;
			// The highest ground under the circle (the smallest z: z is down),
			// and the middle's, which the camera looks at
			int highest = 63;
			for (int i = 0; i < 180; i++) {
				const float a = static_cast<float>(i) * (2.0F * static_cast<float>(M_PI) / 180.0F);
				for (float r = orbitRadius - 6.0F; r <= orbitRadius + 6.0F; r += 3.0F) {
					const int x = static_cast<int>(cx + std::cos(a) * r);
					const int y = static_cast<int>(cy + std::sin(a) * r);
					highest = std::min(highest, map.GetTop(x, y));
				}
			}
			int middle = 0, n = 0;
			for (int dy = -24; dy <= 24; dy += 8)
				for (int dx = -24; dx <= 24; dx += 8) {
					middle += map.GetTop(static_cast<int>(cx) + dx, static_cast<int>(cy) + dy);
					n++;
				}
			orbitZ = std::max(-30.0F, static_cast<float>(highest) - 16.0F);
			orbitCentre = MakeVector3(cx, cy, static_cast<float>(middle) / n - 2.0F);
		}

		void MainScreenUI::MouseEvent(float x, float y) { manager->MouseEvent(x, y); }
		void MainScreenUI::WheelEvent(float x, float y) { manager->WheelEvent(x, y); }
		void MainScreenUI::KeyEvent(const std::string& key, bool down) {
			manager->KeyEvent(key, down);
		}
		void MainScreenUI::TextInputEvent(const std::string& text) {
			manager->TextInputEvent(text);
		}
		void MainScreenUI::TextEditingEvent(const std::string& text, int start, int len) {
			manager->TextEditingEvent(text, start, len);
		}

		bool MainScreenUI::AcceptsTextInput() { return manager->AcceptsTextInput(); }
		AABB2 MainScreenUI::GetTextInputRect() { return manager->GetTextInputRect(); }

		client::SceneDefinition MainScreenUI::SetupCamera(client::SceneDefinition sceneDef,
		                                                  Vector3 eye, Vector3 at, Vector3 up,
		                                                  float fov, float ratio) {
			Vector3 dir = (at - eye).Normalize();
			Vector3 side = Vector3::Cross(dir, up).Normalize();
			up = -Vector3::Cross(dir, side);
			sceneDef.viewOrigin = eye;
			sceneDef.viewAxis[0] = side;
			sceneDef.viewAxis[1] = up;
			sceneDef.viewAxis[2] = dir;
			sceneDef.fovY = DEG2RAD(fov);
			sceneDef.fovX = 2.0F * std::atan(std::tan(sceneDef.fovY * 0.5F) * ratio);
			return sceneDef;
		}

		void MainScreenUI::RunFrame(float dt) {
			if (time < 0.0F)
				time = 0.0F;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			client::SceneDefinition sceneDef;
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);
			if (orbit) {
				// Round the map once in six minutes (cg_menuMapOrbit seconds)
				const float lap = std::max(10.0F, static_cast<float>(cg_menuMapOrbit));
				const float a = 0.6F + time * 2.0F * static_cast<float>(M_PI) / lap;
				const Vector3 eye = MakeVector3(orbitCentre.x + std::cos(a) * orbitRadius,
				                                orbitCentre.y + std::sin(a) * orbitRadius, orbitZ);
				sceneDef = SetupCamera(sceneDef, eye, orbitCentre, up, 45.0F, sw / sh);
			} else {
				float cameraX = time;
				cameraX -= std::floor(cameraX / 512.0F) * 512.0F;
				cameraX = 512.0F - cameraX;

				Vector3 orig = MakeVector3(cameraX, 256.0F, 12.0F);
				Vector3 aimAt = MakeVector3(cameraX + 0.1F, 257.0F, 12.5F);
				sceneDef = SetupCamera(sceneDef, orig, aimAt, up, 30.0F, sw / sh);
			}
			sceneDef.zNear = 0.1F;
			sceneDef.zFar = 160.0F;
			sceneDef.time = static_cast<unsigned int>(time * 1000.0F);
			sceneDef.viewportWidth = static_cast<int>(sw);
			sceneDef.viewportHeight = static_cast<int>(sh);
			sceneDef.denyCameraBlur = true;
			sceneDef.depthOfFieldFocalLength = 100.0F;
			sceneDef.skipWorld = false;

			// fade the map
			float fade = Clamp((time - 1.0F) / 2.2F, 0.0F, 1.0F);
			sceneDef.globalBlur = Clamp((1.0F - (time - 1.0F) / 2.5F), 0.0F, 1.0F);
			if (!mainMenu->IsEnabled())
				sceneDef.globalBlur = std::max(sceneDef.globalBlur, 0.5F);

			renderer->StartScene(sceneDef);
			renderer->EndScene();

			// fade the map (draw)
			if (fade < 1.0F) {
				ui::SetColorNP(*renderer, MakeVector4(0.0F, 0.0F, 0.0F, 1.0F - fade));
				renderer->DrawImage(nullptr, AABB2(0.0F, 0.0F, sw, sh));
			}

			// draw title logo
			Handle<client::IImage> img = renderer->RegisterImage("Gfx/Title/Logo.png");
			ui::SetColorNP(*renderer, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			renderer->DrawImage(img, MakeVector2((sw - img->GetWidth()) * 0.5F, 64.0F));

			// The check answers asynchronously; announce a new release once, as soon
			// as the menu is free of other dialogs.
			SelfUpdater::RunTestIfAsked();

			UpdatePromptScreen::ShowIfPending(mainMenu.GetPointerOrNull(),
			                                  [this] { shouldExit = true; });

			manager->RunFrame(dt);
			manager->Render();

			time += std::min(dt, 0.05F);
		}

		void MainScreenUI::Closing() { shouldExit = true; }

		bool MainScreenUI::WantsToBeClosed() { return shouldExit; }
	} // namespace gui
} // namespace spades
