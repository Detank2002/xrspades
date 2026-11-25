/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

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

#include "DocumentTypes.h"
#include "UI/Editor/KV6/KV6EditorView.h"
#include "MainScreen.h"
#include "MainScreenHelper.h"
#include <Client/Client.h>
#include <Client/GameMap.h>
#include <Client/SceneDefinition.h>
#include <Client/VRState.h>
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/MemoryStream.h>
#include <Training/MapGen.h>
#include <Training/VoxelMap.h>
#include <chrono>
#include <cmath>
#include <Client/Fonts.h>
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/TrainingHost.h>
#include <Gui/UI/MainScreen/MainScreenUI.h>
#include <Gui/UI/Settings/QualityPresets.h>
#include <Gui/UI/Widgets/MessageBox.h>
#include <Gui/UI/Framework/UIManager.h>
#include <ScriptBindings/ScriptManager.h>

DEFINE_SPADES_SETTING(cg_playerName, "Deuce");

// In a headset, the menus stand on a generated map instead of in a void.
DEFINE_SPADES_SETTING(vr_menuScene, "1");
// For tests: the menu's map read from this path (e.g. Maps/Babel.vxl in the user
// resources) instead of one of Maps/Menu's
DEFINE_SPADES_SETTING(vr_debugMenuMap, "");
SPADES_SETTING(vr_worldScale);
SPADES_SETTING(vr_snapTurn);
SPADES_SETTING(vr_smoothTurnSpeed);
SPADES_SETTING(vr_leftHanded);
SPADES_SETTING(vr_swapSticks);
SPADES_SETTING(cl_startGuard);
SPADES_SETTING(cl_startRescued);

namespace spades {
	namespace gui {
		MainScreen::MainScreen(Handle<client::IRenderer> _renderer,
		                       Handle<client::IAudioDevice> _audioDevice,
		                       Handle<client::FontManager> _fontManager,
		                       const std::string& openDocumentPath)
		    : renderer(std::move(_renderer)),
		      audioDevice(std::move(_audioDevice)),
		      fontManager(std::move(_fontManager)),
		      pendingDocumentPath(openDocumentPath) {
			SPADES_MARK_FUNCTION();
			if (!renderer)
				SPInvalidArgument("renderer");
			if (!audioDevice)
				SPInvalidArgument("audioDevice");
			if (!fontManager)
				SPInvalidArgument("fontManager");

			helper = new MainScreenHelper(this);

			// first call to RunFrame tends to have larger dt value.
			// so this value is set in the first call.
			timeToStartInitialization = 1000.0F;
		}

		MainScreen::~MainScreen() {
			SPADES_MARK_FUNCTION();
			helper->MainScreenDestroyed();
			TrainingHost::Get().Stop();
		}

		// Restores renderer's state (game map, fog color)
		// after returning from the game client.
		void MainScreen::RestoreRenderer() {
			if (ui) {
				ui->SetupRenderer();
				// A subview may have written files the visible tab lists (a saved
				// model, a recorded demo), so the listing is re-read here.
				ui->OnReturnedToMenu();
			}
			// The game that ended set its own map and then none.
			vrMenuMapSet = false;
			vrMenuAnchored = false;
		}

		std::string MainScreen::OpenEditor(const std::string& path, bool isNew,
		                                   SoftwareCursor* cursor) {
			const DocumentType* type = FindDocumentType(path);
			if (!type || !type->editable)
				return UnsupportedDocumentMessage();
			try {
				switch (type->kind) {
					case DocumentKind::Model:
						subview = Handle<KV6EditorView>::New(&*renderer, &*audioDevice,
						                                     &*fontManager, cursor, path, isNew)
						            .Cast<View>();
						return "";
					case DocumentKind::Scene:
					case DocumentKind::Map: break; // no editor for these yet
				}
			} catch (const std::exception& ex) {
				SPLog("[!] Error while opening the editor: %s", ex.what());
				return ex.what();
			}
			// A type marked editable with no editor behind it is a table mistake,
			// said as one rather than as the player's.
			SPLog("[!] No editor opens %s files", type->extension);
			return UnsupportedDocumentMessage();
		}

		void MainScreen::DrawVRMenuScene() {
			client::IVRSystem* vr = client::GetVRSystem();
			if (!vr || !(int)vr_menuScene)
				return;
			const client::VRFrameState& fs = vr->GetFrameState();
			if (!fs.running || !fs.shouldRender || !fs.head.valid)
				return;

			// The map: one of the real maps the flat menu flies round (Maps/Menu),
			// a different one each launch, read here and decoded off the frame
			// (a headset must not stall); a generated one if none is there. Until
			// it is ready, the void.
			if (!vrMenuMapStarted) {
				vrMenuMapStarted = true;
				static const char* const kMenuMaps[] = {"Maps/Menu/Normandie.vxl",
				                                        "Maps/Menu/GhostTown.vxl",
				                                        "Maps/Menu/Quarantine.vxl",
				                                        "Maps/Menu/MashesswoodIsland.vxl"};
				const int n = sizeof(kMenuMaps) / sizeof(kMenuMaps[0]);
				std::string bytes;
				// vr_debugMenuMap: a map of one's own instead, for tests (what a
				// map costs to draw, measured with no game running)
				const std::string own = vr_debugMenuMap;
				if (!own.empty() && FileManager::FileExists(own.c_str())) {
					try {
						bytes = FileManager::ReadAllBytes(own.c_str());
						SPLog("[VR] Menu scene: %s (vr_debugMenuMap)", own.c_str());
					} catch (const std::exception& ex) {
						SPLog("[VR] Menu scene: %s: %s", own.c_str(), ex.what());
					}
				}
				const int first = static_cast<int>(std::chrono::steady_clock::now()
				                                     .time_since_epoch()
				                                     .count() %
				                                   n);
				for (int k = 0; k < n && bytes.empty(); k++) {
					const char* path = kMenuMaps[(first + k) % n];
					if (!FileManager::FileExists(path))
						continue;
					try {
						bytes = FileManager::ReadAllBytes(path);
						SPLog("[VR] Menu scene: %s", path);
					} catch (const std::exception& ex) {
						SPLog("[VR] Menu scene: %s: %s", path, ex.what());
					}
				}
				vrMenuMapLoad = std::async(std::launch::async, [bytes]() -> client::GameMap* {
					if (!bytes.empty()) {
						MemoryStream stream(bytes.data(), bytes.size());
						return client::GameMap::Load(&stream);
					}
					host::VoxelMap m;
					host::generate_map(m, 0x5EED);
					const std::vector<uint8_t> gen = m.save_vxl();
					MemoryStream stream(reinterpret_cast<const char*>(gen.data()), gen.size());
					return client::GameMap::Load(&stream);
				});
			}
			if (!vrMenuMap) {
				if (!vrMenuMapLoad.valid() ||
				    vrMenuMapLoad.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
					return;
				try {
					vrMenuMap = Handle<client::GameMap>(vrMenuMapLoad.get(), false);
					SPLog("[VR] Menu scene: the map is ready");
				} catch (const std::exception& ex) {
					SPLog("[VR] Menu scene: the map did not load: %s", ex.what());
					return;
				}
				if (!vrMenuMap)
					return;
			}
			if (!vrMenuMapSet) {
				renderer->SetGameMap(vrMenuMap);
				renderer->SetFogColor(MakeVector3(128.0F, 232.0F, 255.0F) / 255.0F);
				renderer->SetFogDistance(128.0F);
				vrMenuMapSet = true;
			}

			// Standing at a player's eye height over the ground, at the middle of
			// the map: in among its buildings, as in a game. The head's own
			// movement moves the view, as it would anywhere (a view that ignores
			// the head is what makes people sick). The off hand's stick walks and
			// the other turns, as in a game; the menu panel stays with you.
			// Tracking -Z is the game's +X (VRTrackingToGameAxes).
			const float scale = std::max(0.5F, std::min((float)vr_worldScale, 4.0F));
			if (!vrMenuAnchored) {
				vrMenuAnchored = true;
				vrMenuAnchor = fs.head.position;
				const int x = vrMenuMap->Width() / 2, y = vrMenuMap->Height() / 2;
				vrMenuEye = MakeVector3((float)x + 0.5F, (float)y + 0.5F,
				                        (float)vrMenuMap->GetTop(x, y) - 2.5F);
				vrMenuYaw = 0.0F;
				vrMenuLastTime = 0.0;
			}
			MoveInVRMenu(fs, scale);
			const float cy = std::cos(vrMenuYaw), sy = std::sin(vrMenuYaw);
			auto axes = [&](const Vector3& t) {
				const Vector3 v = client::VRTrackingToGameAxes(t);
				return MakeVector3(v.x * cy - v.y * sy, v.x * sy + v.y * cy, v.z);
			};
			auto toWorld = [&](const Vector3& v) { return axes(v) * scale; };
			const Vector3 origin = vrMenuEye + toWorld(fs.head.position - vrMenuAnchor);

			client::SceneDefinition def;
			def.time = static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
			def.zNear = 0.05F;
			def.zFar = 160.0F;
			def.viewOrigin = origin;
			const Vector3 fwd = axes(-fs.head.Axis(2)).Normalize();
			const Vector3 up = axes(fs.head.Axis(1)).Normalize();
			def.viewAxis[2] = fwd;
			def.viewAxis[1] = -up;
			def.viewAxis[0] = Vector3::Cross(def.viewAxis[1], def.viewAxis[2]).Normalize();
			def.fovX = 2.0F * std::atan(fs.eyeTanX[0]);
			def.fovY = 2.0F * std::atan(fs.eyeTanY[0]);
			for (int i = 0; i < 2; i++) {
				auto& e = def.vrEyes[i];
				const client::VRPose& eye = fs.eyes[i];
				e.origin = origin + toWorld(eye.position - fs.head.position);
				e.axis[0] = axes(eye.Axis(0)).Normalize();
				e.axis[1] = axes(eye.Axis(1)).Normalize();
				e.axis[2] = axes(-eye.Axis(2)).Normalize();
				e.fovX = 2.0F * std::atan(fs.eyeTanX[i]);
				e.fovY = 2.0F * std::atan(fs.eyeTanY[i]);
				e.asymmetric = fs.eyeAsymmetric;
				e.tangents =
				  client::FovTangents{fs.eyeLeft[i], fs.eyeRight[i], fs.eyeDown[i], fs.eyeUp[i]};
			}
			def.numVREyes = 2;
			def.denyCameraBlur = true;
			def.radialBlur = 0.0F;
			def.blurVignette = 0.0F;
			def.depthOfFieldFocalLength = 0.0F;
			def.globalBlur = 0.0F;
			renderer->StartScene(def);
			renderer->EndScene();
		}

		void MainScreen::MoveInVRMenu(const client::VRFrameState& fs, float scale) {
			const double now = std::chrono::duration<double>(
			                     std::chrono::steady_clock::now().time_since_epoch())
			                     .count();
			const float dt = vrMenuLastTime > 0.0
			                   ? std::min(0.1F, static_cast<float>(now - vrMenuLastTime))
			                   : 0.0F;
			vrMenuLastTime = now;
			if (dt <= 0.0F || !vrMenuMap || !fs.head.valid)
				return;
			const int toolHand = (int)vr_leftHanded ? 0 : 1, offHand = 1 - toolHand;
			// A hand pointing at the panel: its stick scrolls (VRMenuPointer)
			auto onPanel = [&](int hand) {
				Vector2 uv;
				float distance;
				const float aspect =
				  renderer->ScreenHeight() / std::max(1.0F, renderer->ScreenWidth());
				return fs.hands[hand].active &&
				       client::VRRayHitsHudPanel(fs, hand, aspect, uv, distance);
			};
			auto rotate = [&](const Vector3& v) {
				const float c = std::cos(vrMenuYaw), s = std::sin(vrMenuYaw);
				return MakeVector3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
			};
			auto headOffset = [&]() {
				return rotate(client::VRTrackingToGameAxes(fs.head.position - vrMenuAnchor) *
				              scale);
			};

			// The hand a (bound) stick is on: with the sticks swapped, the other
			// one. A stick whose hand points at the panel scrolls it
			// (VRMenuPointer), and neither walks nor turns.
			const bool swapped = (bool)vr_swapSticks;
			auto stickHand = [&](int h) { return swapped ? 1 - h : h; };
			// Turning, about the head: snapped (vr_snapTurn degrees) or smooth.
			const float tx =
			  onPanel(stickHand(toolHand)) ? 0.0F : fs.hands[toolHand].thumbstick.x;
			float dyaw = 0.0F;
			const float snap = (float)vr_snapTurn;
			if (snap > 0.0F) {
				if (!vrMenuTurnLatched && std::fabs(tx) > 0.7F) {
					vrMenuTurnLatched = true;
					dyaw = (tx > 0.0F ? 1.0F : -1.0F) * snap * 0.0174532925F;
				} else if (std::fabs(tx) < 0.3F) {
					vrMenuTurnLatched = false;
				}
			} else if (std::fabs(tx) > 0.2F) {
				dyaw = tx * (float)vr_smoothTurnSpeed * 0.0174532925F * dt;
			}
			if (dyaw != 0.0F) {
				const Vector3 before = vrMenuEye + headOffset();
				vrMenuYaw += dyaw;
				vrMenuEye = before - headOffset();
			}

			// Walking, along the ground where the head faces, at a player's pace.
			// Walls stop it; one block up is a step.
			const client::GameMap& map = *vrMenuMap;
			auto solid = [&](float x, float y, float z) {
				const int ix = (int)std::floor(x), iy = (int)std::floor(y),
				          iz = (int)std::floor(z);
				if (ix < 0 || iy < 0 || ix >= map.Width() || iy >= map.Height())
					return true; // the map's edge
				if (iz < 0)
					return false;
				if (iz >= map.Depth())
					return true;
				return map.IsSolid(ix, iy, iz);
			};
			const Vector3 head = vrMenuEye + headOffset();
			Vector2 stick = fs.hands[offHand].thumbstick;
			if (onPanel(stickHand(offHand)))
				stick = MakeVector2(0.0F, 0.0F);
			const float len = stick.GetLength();
			Vector3 move = MakeVector3(0.0F, 0.0F, 0.0F);
			if (len > 0.2F) {
				Vector3 f = rotate(client::VRTrackingToGameAxes(fs.head.Axis(2) * -1.0F));
				f.z = 0.0F;
				if (f.GetLength() > 1.0E-3F) {
					f = f.Normalize();
					const Vector3 r = MakeVector3(-f.y, f.x, 0.0F);
					const float speed = 4.0F * std::min(1.0F, (len - 0.2F) / 0.8F) / len;
					move = (f * stick.y + r * stick.x) * (speed * dt);
				}
			}
			// A column is open if nothing is at the head or the body: the feet
			// are 2.5 under the eye, and the lowest block of that is a step.
			auto open = [&](float x, float y) {
				for (float dz = -0.4F; dz < 1.6F; dz += 0.5F)
					if (solid(x, y, head.z + dz))
						return false;
				return true;
			};
			Vector3 eye = vrMenuEye;
			if (move.x != 0.0F && open(head.x + move.x + (move.x > 0.0F ? 0.3F : -0.3F), head.y))
				eye.x += move.x;
			const float hx = head.x + (eye.x - vrMenuEye.x);
			if (move.y != 0.0F && open(hx, head.y + move.y + (move.y > 0.0F ? 0.3F : -0.3F)))
				eye.y += move.y;
			// The ground under the body (not under the head, which may lean into
			// a wall or past the map's edge: from there it climbed the wall, or
			// into the sky): stepped up onto by a block at most, or fallen down
			// to, eased
			for (int z = std::max(0, (int)std::floor(eye.z + 1.6F)); z < map.Depth(); z++) {
				if (solid(eye.x, eye.y, (float)z)) {
					const float want = (float)z - 2.5F;
					if (want >= eye.z - 1.1F)
						eye.z += (want - eye.z) * (1.0F - std::exp(-dt * 10.0F));
					break;
				}
			}
			vrMenuEye = eye;
		}

		void MainScreen::ClearStartGuard(float dt) {
			// Nine seconds of frames anywhere: in the menu, or a game joined at
			// once (cleared only in the menu, a player who went straight into a
			// game and left from it had their graphics put back on Performance at
			// the next start).
#if defined(__ANDROID__)
			if (startGuardTime >= 9.0F)
				return;
			startGuardTime += std::min(dt, 0.1F);
			if (startGuardTime >= 9.0F && (int)cl_startGuard) {
				cl_startGuard = 0;
				Settings::GetInstance()->Flush();
			}
#else
			(void)dt;
#endif
		}

		void MainScreen::CheckVRMenuSpeed(float dt) {
			// On a standalone headset, settings too heavy for it (the PC's Ultra
			// on a Quest) left it too slow to set them back, or ran it out of
			// memory before the menu came (testers: trapped). Once a run, nine
			// seconds into the menu: this start made it (cl_startGuard, which
			// Main sets as it starts: a start that never clears it puts the next
			// on the Performance level); and if the menu's scene ran under 40
			// frames a second, the Performance level is put back now. Either is
			// said so. Most of it takes effect at the next start.
#if defined(__ANDROID__)
			if (vrSpeedChecked || !ui || dt <= 0.0F)
				return;
			// Only the frames the headset shows: off a head, or asleep, the loop
			// runs slowly or not at all, and that is not the graphics
			// (counted, a game started with the headset on the table was put
			// back on Performance)
			client::IVRSystem* vr = client::GetVRSystem();
			if (!vr || !vr->GetFrameState().focused || !vr->GetFrameState().shouldRender)
				return;
			vrSpeedTime += std::min(dt, 0.1F);
			if (vrSpeedTime < 3.0F)
				return; // the map settling in
			vrSpeedFrames++;
			if (vrSpeedTime < 9.0F)
				return;
			vrSpeedChecked = true;
			const float fps = static_cast<float>(vrSpeedFrames) / (vrSpeedTime - 3.0F);
			const QualityPreset& performance = GetHeadsetQualityPresets().front();
			SPLog("[VR] Menu speed: %.0f FPS", fps);
			std::string message;
			if ((int)cl_startRescued) {
				cl_startRescued = 0;
				message = _Tr("MainScreen",
				              "The game did not start last time, so its graphics are back "
				              "on Performance (Settings, VR, Headset Quality).");
			} else if (client::GetVRSystem() && vrMenuMapSet && fps < 40.0F &&
			           !MatchesQualityPreset(performance)) {
				ApplyQualityPreset(performance);
				SPLog("[VR] Too slow at %.0f FPS: back to the headset Performance level", fps);
				message = _Tr("MainScreen",
				              "The game was running too slowly on this headset, so its "
				              "graphics are back on Performance (Settings, VR, Headset "
				              "Quality). Restart the game for all of it to take effect.");
			}
			Settings::GetInstance()->Flush();
			if (message.empty())
				return;
			ui::UIElement& root = ui->GetUIManager().GetRootElement();
			if (root.GetChildren().empty())
				return;
			Handle<AlertScreen> alert = Handle<AlertScreen>::New(
			  root.GetChildren().front().GetPointerOrNull(), message, 160.0F);
			alert->Run();
#else
			(void)dt;
#endif
		}

		bool MainScreen::NeedsAbsoluteMouseCoordinate() {
			SPADES_MARK_FUNCTION();
			if (subview)
				return subview->NeedsAbsoluteMouseCoordinate();

			return true;
		}

		void MainScreen::MouseEvent(float x, float y) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->MouseEvent(x, y);
				return;
			}
			if (!ui)
				return;

			ui->MouseEvent(x, y);
		}

		void MainScreen::WheelEvent(float x, float y) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->WheelEvent(x, y);
				return;
			}
			if (!ui)
				return;

			ui->WheelEvent(x, y);
		}

		void MainScreen::KeyEvent(const std::string& key, bool down) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->KeyEvent(key, down);
				return;
			}
			if (!ui)
				return;
			ui->KeyEvent(key, down);
		}

		void MainScreen::TextInputEvent(const std::string& ch) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->TextInputEvent(ch);
				return;
			}
			if (!ui)
				return;
			ui->TextInputEvent(ch);
		}

		void MainScreen::TextEditingEvent(const std::string& ch, int start, int len) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->TextEditingEvent(ch, start, len);
				return;
			}
			if (!ui)
				return;
			ui->TextEditingEvent(ch, start, len);
		}

		bool MainScreen::AcceptsTextInput() {
			SPADES_MARK_FUNCTION();
			if (subview)
				return subview->AcceptsTextInput();
			if (!ui)
				return false;
			return ui->AcceptsTextInput();
		}

		AABB2 MainScreen::GetTextInputRect() {
			SPADES_MARK_FUNCTION();
			if (subview)
				return subview->GetTextInputRect();
			if (!ui)
				return AABB2();
			return ui->GetTextInputRect();
		}

		bool MainScreen::WantsToBeClosed() {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return false;
			return ui->WantsToBeClosed();
		}

		void MainScreen::DrawStartupScreen() {
			SPADES_MARK_FUNCTION();
			Handle<client::IImage> img;
			Vector2 pos, size;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));

			img = renderer->RegisterImage("Gfx/Title/Logo.png");
			size = {img->GetWidth(), img->GetHeight()};
			size *= std::min(1.0F, sw / size.x);
			size *= std::min(1.0F, sh / size.y);
			pos = (MakeVector2(sw, sh) - size) * 0.5F;
			renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
			renderer->DrawImage(img, AABB2(pos.x, pos.y, size.x, size.y));

			std::string str = _Tr("MainScreen", "NOW LOADING");
			client::IFont& font = fontManager->GetGuiFont();
			size = font.Measure(str);
			pos = MakeVector2(sw - 16.0F, sh - 16.0F);
			pos -= size;
			font.DrawShadow(str, pos, 1.0F, MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.5));
		}

		void MainScreen::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			ClearStartGuard(dt);
			if (subview) {
				try {
					subview->RunFrame(dt);
					return;
				} catch (const std::exception& ex) {
					SPLog("[!] Error while running a game client: %s", ex.what());
					subview->Closing();
					subview = NULL;
					// The training course ends with the view that played it.
					TrainingHost::Get().Stop();
					RestoreRenderer();
					helper->errorMessage = ex.what();
				}
			}

			if (timeToStartInitialization > 100.0F)
				timeToStartInitialization = 0.2F;
			if (timeToStartInitialization > 0.0F) {
				DrawStartupScreen();

				timeToStartInitialization -= dt;
				if (timeToStartInitialization <= 0.0F) {
					DoInit(); // do init
					// Init may have gone straight to a subview (a model to open):
					// this frame is that view's, not the menu's, and drawing the menu
					// here would show it for a frame on the way past.
					if (subview)
						return;
				} else {
					return;
				}
			}

			helper->Update();

			// In a headset the menus stand on a map (vr_menuScene).
			try {
				DrawVRMenuScene();
			} catch (const std::exception& ex) {
				SPLog("[VR] Menu scene failed, left off: %s", ex.what());
				vr_menuScene = 0;
			}

			CheckVRMenuSpeed(dt);

			// `DoInit` either sets `ui` or throws, so this should not fire. Guard it
			// anyway, so that an init path which ever leaves `ui` unset stops here
			// instead of dereferencing null.
			if (!ui)
				return;

			ui->RunFrame(dt);
		}

		// Runs after the runner has presented the frame: this is where the game client
		// is torn down, so the renderer state it leaves behind is never swapped in.
		void MainScreen::RunFrameLate(float dt) {
			SPADES_MARK_FUNCTION();
			// Started to open a model: go straight to the editor, so opening one
			// from a terminal or a file manager lands where the file belongs rather
			// than in the menus. Once a first frame has been presented, as when
			// the editor is opened from the menus: an editor drawing the window's
			// very first frame drew into no framebuffer, which GL refused.
			if (ui && !subview && !pendingDocumentPath.empty()) {
				std::string path;
				path.swap(pendingDocumentPath); // opened once, not on every return here
				OpenDocumentFile(path);
				return;
			}
			if (!subview)
				return;

			try {
				subview->RunFrameLate(dt);
				if (!subview->WantsToBeClosed())
					return;

				subview->Closing();
				subview = NULL;
				// The training course ends with the view that played it.
				TrainingHost::Get().Stop();
				RestoreRenderer();
			} catch (const std::exception& ex) {
				SPLog("[!] Error while running a game client: %s", ex.what());
				subview->Closing();
				subview = NULL;
				// The training course ends with the view that played it.
				TrainingHost::Get().Stop();
				RestoreRenderer();
				helper->errorMessage = ex.what();
			}
		}

		void MainScreen::DoInit() {
			SPADES_MARK_FUNCTION();
			try {
				renderer->Init();

				ui = Handle<MainScreenUI>::New(renderer.GetPointerOrNull(),
				                               audioDevice.GetPointerOrNull(),
				                               fontManager.GetPointerOrNull(),
				                               helper.GetPointerOrNull());
			} catch (const std::exception& ex) {
				// Let the failure reach the top-level handler, but name the phase in
				// SystemMessages.log first: a failure here leaves the startup screen as
				// the last presented frame, which is otherwise hard to tell apart from
				// a hang.
				SPLog("[!] Failed to initialize the main screen UI: %s", ex.what());
				throw;
			}

		}

		void MainScreen::OpenDocumentFile(const std::string& path) {
			SPADES_MARK_FUNCTION();
			SPLog("Opening '%s' in its editor", path.c_str());
			std::string msg = OpenEditor(path, false, nullptr);
			if (!msg.empty()) {
				SPLog("[!] Could not open the editor: %s", msg.c_str());
				helper->errorMessage = msg;
			}
		}

		void MainScreen::Closing() {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->Closing();
				subview = NULL;
				// The training course ends with the view that played it.
				TrainingHost::Get().Stop();
			}

			if (!ui)
				return;

			ui->Closing();
		}

		bool MainScreen::ExecCommand(const Handle<ConsoleCommand>& cmd) {
			SPADES_MARK_FUNCTION();
			if (subview)
				return subview->ExecCommand(cmd);

			return View::ExecCommand(cmd);
		}

		Handle<ConsoleCommandCandidateIterator>
		MainScreen::AutocompleteCommandName(const std::string& name) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				return subview->AutocompleteCommandName(name);
			}
			return View::AutocompleteCommandName(name);
		}

		std::string MainScreen::Connect(const ServerAddress& host) {
			try {
				subview = Handle<client::Client>::New(&*renderer, &*audioDevice, host, fontManager)
				            .Cast<View>();
			} catch (const std::exception& ex) {
				SPLog("[!] Error while initializing a game client: %s", ex.what());
				return ex.what();
			}
			return "";
		}

		std::string MainScreen::PlayDemo(const std::string& demoPath) {
			try {
				subview = Handle<client::Client>::New(&*renderer, &*audioDevice,
				              ServerAddress(), fontManager, demoPath)
				            .Cast<View>();
			} catch (const std::exception& ex) {
				SPLog("[!] Error starting demo playback: %s", ex.what());
				return ex.what();
			}
			return "";
		}
	} // namespace gui
} // namespace spades
