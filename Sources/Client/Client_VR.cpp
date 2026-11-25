/*
 Copyright (c) 2026 ZeroSpades VR contributors

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

// Playing with a headset. Two jobs:
//
//  - Before the world update, UpdateVR reads the controllers and turns them
//    into the input a keyboard would have produced (through KeyEvent, with the
//    player's own key bindings, so every action goes down the same path it
//    always did), sets the facing the server sees, and walks the body wherever
//    the player physically walked.
//  - When the scene is set up, ApplyVRToSceneDefinition puts the camera in the
//    head and hands the renderer one view per eye.
//
// The facing the server sees follows the head, and follows the gun hand while
// a tool is in use. The server checks every hit against that facing, and AoS
// scales walking speed by it and refuses to climb while looking steeply down;
// a hand held low would otherwise slow and pin the player. See VR.md.

#include <algorithm>
#include <chrono>
#include <cmath>

#include "Client.h"
#include <Training/TrainingModes.h>

#include <Core/Bitmap.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Core/VoxelModel.h>

#include "ChatWindow.h"
#include "ClientPlayer.h"
#include "ClientUI.h"
#include "LimboView.h"
#include "MapView.h"
#include "Fonts.h"
#include "IFont.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "NetClient.h"
#include "Weapon.h"
#include "GameMap.h"
#include "World.h"
#include <Gui/TrainingHost.h>

// World blocks per real metre. 1.4 puts a 1.6m eye close to the game's 2.25.
DEFINE_SPADES_SETTING(vr_worldScale, "1.4");
// Degrees per flick of the turn stick; 0 turns smoothly instead.
DEFINE_SPADES_SETTING(vr_snapTurn, "45");
DEFINE_SPADES_SETTING(vr_smoothTurnSpeed, "150");
// How far below the standing height the head has to go to crouch, metres.
// 0.27: testers found 0.35 (as vr_crouchDepth, renamed so the new default
// reaches configs that saved the old one) too deep a duck.
DEFINE_SPADES_SETTING(vr_crouchAt, "0.27");
// Playing sitting down: the head dropping never crouches (leaning forward in a
// chair would), only the crouch button does.
DEFINE_SPADES_SETTING(vr_seated, "0");
// Walking in the room moves the body. Off, the view stays at the body's eye
// whatever the head does (it turns, it does not move: no peeking, see VR.md).
DEFINE_SPADES_SETTING(vr_roomScale, "1");
// Hand speed, metres per second, that throws a grenade as far as a normal throw.
DEFINE_SPADES_SETTING(vr_throwSpeed, "5");
// Hold the tools in the left hand.
DEFINE_SPADES_SETTING(vr_leftHanded, "0");
// The player HUD (health, ammo, tools, minimap) on a panel over the off hand's
// wrist, shown when you look at it; 0 puts it on the front panel instead.
DEFINE_SPADES_SETTING(vr_wristHud, "1");
DEFINE_SPADES_SETTING(vr_wristWidth, "0.24");
// Degrees the tools point below the controller's own aim: held naturally, a
// Touch controller's aim ray points higher than a gun in that hand would.
DEFINE_SPADES_SETTING(vr_aimTilt, "15");
// Squeeze the off hand's grip on a gun's barrel to hold it in both hands.
DEFINE_SPADES_SETTING(vr_twoHandedGrip, "1");
// The off hand's trigger, held on a gun's barrel, braces it on the shoulder.
DEFINE_SPADES_SETTING(vr_shoulderStock, "1");
// 0 to 1: how much the gun's aim is steadied when the hand is nearly still.
// Off by default: testers wanted the gun and its laser rigid in the hand
// (renamed from vr_aimSmooth, 0.5, so saved configs take the new default).
DEFINE_SPADES_SETTING(vr_aimSmooth, "0");
// Swinging the spade faster than this (m/s) hits, as the trigger does; 0: off.
DEFINE_SPADES_SETTING(vr_swingSpeed, "2.5");
// The spade is solid and hits where its tip strikes a block (testers: swinging
// in the air tired the arm in seconds, and the hit was not where the spade
// was). Off: swinging it anywhere faster than vr_swingSpeed hits, along it.
DEFINE_SPADES_SETTING(vr_spadeContact, "1");
// Tools on the body: squeeze the gun hand's grip over a shoulder or at a hip.
DEFINE_SPADES_SETTING(vr_holsters, "1");
// The hips (blocks and grenades) this many centimetres higher (or, negative,
// lower) than a share of the eye height puts them (VRHolsterFrameUpdate)
DEFINE_SPADES_SETTING(vr_hipHeight, "0");
// The body mirror: 0 never, 1 while a menu is up in a game, 2 always
DEFINE_SPADES_SETTING(vr_bodyMirror, "1");
// For tests: the menu's Setup screen opened once the local player is alive in a
// game, as if chosen (its cost measured without hands to open it)
DEFINE_SPADES_SETTING(cg_debugOpenSetup, "0");
// How far round from the menu panel it stands, in degrees (0: in front)
DEFINE_SPADES_SETTING(vr_bodyMirrorAngle, "50");
// 1: the holsters turn with every turn of the head, as before 0.4.0. 0: they face
// the body's way, which lets the head look aside (VRHolsterFrame).
DEFINE_SPADES_SETTING(vr_holsterFollow, "0");
// Spectating or watching a demo in a headset starts in the table view (the off
// hand's stick click still turns it off). Not when merely dead between lives.
DEFINE_SPADES_SETTING(vr_tableWhenSpectating, "1");
// The table view's map: how many blocks each way go to one of its voxels (1,
// 2, 4 or 8). The whole map at full detail was too many triangles for a
// standalone headset (a Quest 3 at 22 FPS, binning its 28 ms a frame).
DEFINE_SPADES_SETTING(vr_tableDetail, "4");
// The game's frame rate on the wrist.
DEFINE_SPADES_SETTING(vr_showFps, "1");
// Dead or following someone: the view holds still and moves in blinks.
DEFINE_SPADES_SETTING(vr_comfortCamera, "1");
// Walking, falling or turning smoothly close in the edges of the view; how far,
// 0 (off) to 1. Room-scale walking and snap turns never do.
DEFINE_SPADES_SETTING(vr_vignette, "0.5");
// The big map is held in the off hand like a tablet, not hung in front of you.
DEFINE_SPADES_SETTING(vr_heldMap, "1");
// B drops the magazine; the off hand takes one from the belt and puts it in.
// Off: B reloads, as R does. (Was vr_physicalReload, on by default: renamed
// so the new default reaches configs that saved the old one. Testers found two
// actions for every reload too many.)
DEFINE_SPADES_SETTING(vr_reloadGesture, "0");
// The off hand's X and Y step through the tools, for when the holsters are out
// of reach (seated). Off: X grabs a block's colour and Y opens the big map.
DEFINE_SPADES_SETTING(vr_toolButtons, "0");
// Sprint while the walking stick is held in, instead of a click toggling it.
DEFINE_SPADES_SETTING(vr_sprintHold, "0");
// A magnifying scope on the rifle, seen when an eye looks down its iron sights.
// Off by default since 0.4.0: testers aimed better with the open sights; the
// Escape menu's Weapon page turns it on.
DEFINE_SPADES_SETTING(vr_scope, "0");
// Held tools do not go through blocks: they stop at them, and slide back.
// Off by default for now (was vr_solidTools, on; renamed so the new default
// reaches configs that saved the old one): aiming round cover, the gun
// stopped at the corner and slid back, and the aim went with it.
DEFINE_SPADES_SETTING(vr_toolCollision, "0");
// Taking damage: a red edge round the view, thicker on the side the hit came
// from, and a heartbeat at low health. 0: off (the rumble stays).
DEFINE_SPADES_SETTING(vr_hurtEdge, "1");
SPADES_SETTING(cg_hurtScreenEffects);

SPADES_SETTING(cg_keyAttack);
SPADES_SETTING(cg_keyAltAttack);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keySprint);
SPADES_SETTING(cg_keyReloadWeapon);
SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyScoreboard);
SPADES_SETTING(cg_keyToggleMapZoom);
SPADES_SETTING(cg_keyPaletteLeft);
SPADES_SETTING(cg_keyPaletteRight);
SPADES_SETTING(cg_mouseSensitivity);

namespace spades {
	namespace client {

		namespace {
			/** Trigger hysteresis, as for the menu pointer. */
			bool TriggerState(float value, bool wasDown) {
				return wasDown ? value > 0.55F : value > 0.75F;
			}

			/** The name KeyEvent receives for a key setting. The space bar is bound
			 * by name ("Space") but arrives as " ". */
			std::string KeyEventName(const std::string& setting) {
				if (EqualsIgnoringCase(setting, "space") || EqualsIgnoringCase(setting, "spacebar") ||
				    EqualsIgnoringCase(setting, "spacekey"))
					return " ";
				return setting;
			}

			float Yaw(const Vector3& v) { return std::atan2(v.y, v.x); }
		} // namespace

		int VRToolHand() { return (int)vr_leftHanded ? VRHandLeft : VRHandRight; }

		Vector3 Client::VRToWorld(const Vector3& v) const {
			Vector3 g = VRTrackingToGameAxes(v) * vrRig.scale;
			float c = std::cos(vrRig.bodyYaw), s = std::sin(vrRig.bodyYaw);
			return MakeVector3(g.x * c - g.y * s, g.x * s + g.y * c, g.z);
		}

		void Client::VRSetKey(const std::string& setting, bool down) {
			if (setting.empty())
				return;
			std::string key = KeyEventName(setting);
			bool& held = vrHeldKeys[key];
			if (held == down)
				return;
			held = down;
			KeyEvent(key, down);
		}

		void Client::VRReleaseAllKeys() {
			// Copy first: KeyEvent may open a menu that changes nothing here, but
			// the map must not be walked while it is written to.
			std::vector<std::string> held;
			for (const auto& k : vrHeldKeys)
				if (k.second)
					held.push_back(k.first);
			vrHeldKeys.clear();
			for (const auto& k : held)
				KeyEvent(k, false);
		}

		void Client::RecenterVR(const VRFrameState& fs, bool deliberate) {
			// A recentre the player did not ask for (the app getting focus, the
			// runtime's own recentre) can come while they are bent over or the
			// headset is off their head. It may raise the standing height but not
			// drop it by a crouch or more. Holding both sticks in is believed.
			const float height = fs.head.position.y;
			const bool keepHeight =
			  vrRig.calibrated && !deliberate &&
			  height < vrRig.standingHeight - std::max(0.1F, (float)vr_crouchAt);
			if (!keepHeight)
				vrRig.standingHeight = height;
			vrRig.anchorX = fs.head.position.x;
			vrRig.anchorZ = fs.head.position.z;

			// The first time, turn the play space so the way the player is looking
			// is the way their character faces. Later recentres only move the
			// floor and the body, never the heading, so nothing swings round.
			if (!vrRig.calibrated) {
				float headYaw = Yaw(VRTrackingToGameAxes(-fs.head.Axis(2)));
				float facing = 0.0F;
				if (world) {
					auto p = world->GetLocalPlayer();
					if (p && p->GetFront().GetLength2D() > 0.01F)
						facing = Yaw(p->GetFront());
				}
				vrRig.bodyYaw = facing - headYaw;
			}
			vrRig.calibrated = true;
			SPLog("[VR] Play space recentred (%s; eye height %.2fm%s)",
			      deliberate ? "sticks held" : "automatic", vrRig.standingHeight,
			      keepHeight ? ", kept: the head was lower than a crouch" : "");
		}

		void Client::UpdateVRRigTransforms(const VRFrameState& fs, const Vector3& origin) {
			vrRig.camera = origin;
			vrRig.headAxes[0] = VRToWorld(-fs.head.Axis(2)).Normalize();
			vrRig.headAxes[1] = VRToWorld(fs.head.Axis(0)).Normalize();
			vrRig.headAxes[2] = VRToWorld(-fs.head.Axis(1)).Normalize();

			// The hands keep their real offset from the eyes, scaled with the
			// world, whatever the body did: that is what makes them feel like
			// your own hands.
			for (int h = 0; h < 2; h++) {
				const VRControllerState& c = fs.hands[h];
				vrRig.handValid[h] = c.grip.valid;
				if (!c.grip.valid)
					continue;
				const VRPose& orient = c.aim.valid ? c.aim : c.grip;
				vrRig.handPos[h] = origin + VRToWorld(c.grip.position - fs.head.position);
				vrRig.aimPos[h] = origin + VRToWorld((c.aim.valid ? c.aim.position
				                                                  : c.grip.position) -
				                                     fs.head.position);
				// Turned down about the hand's right by vr_aimTilt. Everything held
				// follows these axes (the gun you see, where it shoots, what others
				// see), so they agree with each other at any tilt.
				Vector3 fwd = VRToWorld(-orient.Axis(2)).Normalize();
				Vector3 down = VRToWorld(-orient.Axis(1)).Normalize();
				const float tilt = DEG2RAD(Clamp((float)vr_aimTilt, -45.0F, 45.0F));
				const float ct = std::cos(tilt), st = std::sin(tilt);
				vrRig.handAxes[h][0] = (fwd * ct + down * st).Normalize();
				vrRig.ownForward[h] = vrRig.handAxes[h][0];
				vrRig.handAxes[h][1] = VRToWorld(orient.Axis(0)).Normalize();
				vrRig.handAxes[h][2] = (down * ct - fwd * st).Normalize();
				vrRig.handVelocity[h] =
				  c.velocityValid ? VRToWorld(c.linearVelocity) : MakeVector3(0, 0, 0);
			}
			// The holsters, where the tool hand takes from them
			for (int i = 0; i < 4; i++) {
				Vector3 at, forward, right;
				float radius;
				vrRig.holsterValid[i] =
				  VRHolsterPlace(fs, VRToolHand(), i, at, radius, forward, right,
				                 &vrRig.holsterFrame);
				if (!vrRig.holsterValid[i])
					continue;
				vrRig.holsterPos[i] = origin + VRToWorld(at - fs.head.position);
				vrRig.holsterForward = VRToWorld(forward).Normalize();
				vrRig.holsterRight = VRToWorld(right).Normalize();
			}

			// Again when the scene is built (ApplyVRToSceneDefinition), so the gun
			// drawn is the gun aimed. The filter's state moves once a frame
			// (UpdateVRAimAssist); here its result is only put back.
			if (vrRig.twoHanded)
				ApplyVRTwoHandedAim();
			if (vrRig.aimFilter.IsActive())
				SetVRToolForward(vrRig.aimFilter.Get());
			ApplyVRRecoil();
			ApplyVRToolCollision();
			if ((int)cg_debugOpenSetup && !debugSetupOpened && world) {
				auto p = world->GetLocalPlayer();
				if (p && p->IsAlive() && !p->IsSpectator()) {
					debugSetupOpened = true;
					SPLog("[VR] cg_debugOpenSetup: the Setup screen opened");
					scriptedUI->EnterSetup();
				}
			}
			UpdateVRMirror(fs);
		}

		void Client::UpdateVRMirror(const VRFrameState& fs) {
			// Your body reflected in a plane beside the menu panel, on the side of
			// the hand that does not point: raise a hand, try the Hip Height, and
			// see it there. Only while a menu is up (or always, 2), in a game
			// with you alive in it; not on the table view.
			vrMirror.shown = false;
			const int mode = (int)vr_bodyMirror;
			if (mode <= 0 || !world || vrTable.active || !fs.head.valid)
				return;
			auto p = world->GetLocalPlayer();
			if (!p || !p->IsAlive() || p->IsSpectator())
				return;
			if (mode == 1 && !(scriptedUI->NeedsInput() && !scriptedUI->IsChatPanelOpen()))
				return;

			// Towards the panel, level (else the way the head faces)
			Vector3 fwd = fs.hudPanel.valid ? VRToWorld(fs.hudPanel.position - fs.head.position)
			                                : vrRig.headAxes[0];
			fwd.z = 0.0F;
			if (fwd.GetLength() < 1.0E-3F)
				fwd = vrRig.headAxes[0], fwd.z = 0.0F;
			if (fwd.GetLength() < 1.0E-3F)
				return;
			fwd = fwd.Normalize();
			const Vector3 right = MakeVector3(-fwd.y, fwd.x, 0.0F);
			// Past the panel's edge (it is about 56 degrees across), on the
			// pointing hand's other side
			const float side = VRToolHand() == VRHandRight ? -1.0F : 1.0F;
			const float a = DEG2RAD(Clamp((float)vr_bodyMirrorAngle, 0.0F, 120.0F));
			const Vector3 n = (fwd * std::cos(a) + right * (side * std::sin(a))).Normalize();
			// The plane 1.2 m away: the reflection stands 2.4 m off, whole in view.
			// What it costs is the screen it covers (Quest 3: 0.8 ms at 1.8 m,
			// looked straight at; nothing looking away), so not nearer.
			const Vector3 at = vrRig.camera + n * (1.2F * vrRig.scale);
			// x - 2 n (n . (x - at)): columns of I - 2 n n^T, then 2 (n . at) n
			const float d = Vector3::Dot(n, at);
			auto col = [&](const Vector3& e) { return e - n * (2.0F * Vector3::Dot(n, e)); };
			vrMirror.matrix = Matrix4::FromAxis(col(MakeVector3(1, 0, 0)), col(MakeVector3(0, 1, 0)),
			                                    col(MakeVector3(0, 0, 1)), n * (2.0F * d));
			vrMirror.shown = true;
			// In a menu the panel trails the head; turning to the mirror must not
			// take the panel (and the mirror, placed from it) round too.
			IVRSystem* vr = GetVRSystem();
			if (vr && scriptedUI->NeedsInput() && !scriptedUI->IsChatPanelOpen())
				vr->SetHudLookAside(-side * a);
		}

		float Client::VRToolReach(Player& p) const {
			// Read off the voxel models, grip to tip, in blocks.
			switch (p.GetTool()) {
				case Player::ToolWeapon:
					switch (p.GetWeaponType()) {
						case SMG_WEAPON: return 0.6F;
						case SHOTGUN_WEAPON: return 0.8F;
						default: return 0.85F;
					}
				case Player::ToolSpade: return 0.85F;
				case Player::ToolBlock: return 0.2F;
				case Player::ToolGrenade: return 0.1F;
			}
			return 0.0F;
		}

		void Client::ApplyVRToolCollision() {
			// A hand in a headset can go anywhere the arm reaches, walls
			// included; the game's own tools stopped at the view. Here the
			// hands stop at blocks, and so does whatever the tool hand holds:
			// pushed at a wall, a gun slides back through the hands, as in Boneworks
			// or Pavlov. The ray the tool acts on starts at the moved hand, so
			// nothing is shot, dug or built from inside a wall. Players are not
			// solid: they would be in the way all the time.
			vrRig.toolBlocked = false;
			// The spade is solid whatever vr_toolCollision says, while it strikes
			// (vr_spadeContact)
			bool spadeSolid = false;
			if ((int)vr_spadeContact && world)
				if (auto lp = world->GetLocalPlayer())
					spadeSolid = lp->IsToolSpade();
			if ((!(int)vr_toolCollision && !spadeSolid) || !vrRig.solidHands || !world ||
			    !world->GetMap())
				return;
			const GameMap& map = *world->GetMap();
			auto solid = [&](const Vector3& v) { return map.ClipBox(v.x, v.y, v.z); };
			if (solid(vrRig.camera))
				return; // the eyes are in something already: no clear side to push to
			const int th = VRToolHand(), oh = 1 - th;
			const float margin = 0.03F;
			if (vrRig.handValid[th]) {
				Vector3 push = VRToolPushback(vrRig.camera, vrRig.handPos[th], vrRig.handAxes[th][0],
				                              vrRig.toolReach, margin, solid);
				if (push.GetLength() > 1.0E-4F) {
					vrRig.handPos[th] += push;
					vrRig.aimPos[th] += push;
					vrRig.toolBlocked = true;
					// The hand on the barrel goes with the gun.
					if (vrRig.twoHanded && vrRig.handValid[oh]) {
						vrRig.handPos[oh] += push;
						vrRig.aimPos[oh] += push;
					}
				}
			}
			if (vrRig.handValid[oh]) {
				Vector3 push = VRToolPushback(vrRig.camera, vrRig.handPos[oh], vrRig.handAxes[oh][0],
				                              0.0F, margin, solid);
				vrRig.handPos[oh] += push;
				vrRig.aimPos[oh] += push;
			}
		}

		void Client::SetVRToolForward(const Vector3& forward) {
			const int th = VRToolHand();
			if (!vrRig.handValid[th] || forward.GetLength() < 1.0E-4F)
				return;
			const Vector3 f = forward.Normalize();
			// The hand keeps its roll: its down, squared to the new forward.
			Vector3 down = vrRig.handAxes[th][2];
			down = down - f * Vector3::Dot(down, f);
			if (down.GetLength() < 1.0E-3F)
				down = MakeVector3(0.0F, 0.0F, 1.0F) - f * f.z;
			down = down.Normalize();
			vrRig.handAxes[th][0] = f;
			vrRig.handAxes[th][1] = Vector3::Cross(down, f).Normalize();
			vrRig.handAxes[th][2] = down;
		}

		Vector3 Client::VRTwoHandedForward() const {
			// The hands' line, turned towards the stock's as far as it is eased in.
			const int th = VRToolHand(), oh = 1 - th;
			Vector3 hands = vrRig.handPos[oh] - vrRig.handPos[th];
			hands = hands.GetLength() > 1.0E-4F ? hands.Normalize() : vrRig.ownForward[th];
			if (vrRig.stock.blend <= 0.0F)
				return hands;
			Vector3 stock = vrRig.handPos[oh] - VRShoulderPoint();
			stock = stock.GetLength() > 1.0E-4F ? stock.Normalize() : hands;
			const Vector3 v = hands * (1.0F - vrRig.stock.blend) + stock * vrRig.stock.blend;
			return v.GetLength() > 1.0E-4F ? v.Normalize() : hands;
		}

		Vector3 Client::VRShoulderPoint() const {
			// Where a stock sits: below the neck on the gun hand's side, in a
			// frame that turns with the head but stays level. From the neck, not
			// the eyes: looking steeply down (shooting off a height) the eyes go
			// some 10 cm forward and down as the head tips, and a shoulder hung
			// from them went too, swinging the gun's line by about as many
			// degrees (testers: unplayable firing down). The neck is where the
			// head turns about, so it stays put. The level heading comes from
			// the face's, or, looking far up or down, where it has little, from
			// the top of the head, which then points the way the body faces.
			const Vector3 eyeFwd = vrRig.headAxes[0];
			const Vector3 headDown = vrRig.headAxes[2];
			const float s = vrRig.scale;
			// Only looking steeply up or down (over 45 degrees): level, a head
			// tipped sideways to sight down the gun put its down vector to the
			// side, and the heading and the shoulder turned with it.
			const float steep = Clamp((std::fabs(eyeFwd.z) - 0.7F) / 0.25F, 0.0F, 1.0F);
			Vector3 fwd = eyeFwd - headDown * ((eyeFwd.z > 0.0F ? 1.0F : -1.0F) * steep);
			fwd.z = 0.0F;
			fwd = fwd.GetLength() > 0.01F ? fwd.Normalize() : MakeVector3(1, 0, 0);
			const Vector3 right = MakeVector3(-fwd.y, fwd.x, 0.0F);
			const float side = VRToolHand() == VRHandRight ? 1.0F : -1.0F;
			// The neck: 10 cm under the eyes and 8 cm back, in the head's frame
			const Vector3 neck = vrRig.camera + headDown * (0.10F * s) - eyeFwd * (0.08F * s);
			// Level, that is the old point (20 cm under the eyes, 4 cm back)
			return neck + MakeVector3(0.0F, 0.0F, 0.10F * s) + right * (0.13F * side * s) +
			       fwd * (0.04F * s);
		}

		void Client::UpdateVRAimAssist(Player& p, float dt) {
			const int th = VRToolHand(), oh = 1 - th;

			// The stock. Pavlov, H3VR and Onward all have one: held in both hands
			// with the gun hand at the shoulder, the gun aims from the shoulder
			// through the front hand. The body sways much less than two wrists,
			// so the line is steadier; it is what a real stock does. Any gun, while
			// the off hand's trigger is held on its barrel (testers asked for a
			// stock weld on every gun), eased in (VRStockUpdate). Until 0.3 the
			// rifle also took the stock by itself, with its hand brought to the
			// shoulder along the stock's line, and took it again, unasked, when the
			// gun came back that way after a brace (testers' report): gone.
			const bool brace = (int)vr_shoulderStock && vrRig.twoHanded && p.IsToolWeapon() &&
			                   vrRig.handValid[th] && vrRig.handValid[oh] && vrRig.braceHeld;
			if (VRStockUpdate(vrRig.stock, brace, brace ? 0.0F : 1.0E3F, brace ? 0.0F : 3.2F, dt))
				VRPulse(th, 0.15F, 0.03F);
			vrRig.shouldered = vrRig.stock.on;

			// The aim the hands give this frame: from the shoulder through the
			// front hand, from hand to hand, or the gun hand's own. Worked out
			// afresh, never read back off the hand's axes: those already carry
			// last frame's smoothed aim, and a filter fed its own output holds
			// still (0.1.7's frozen rifle).
			Vector3 raw = vrRig.ownForward[th];
			if (vrRig.twoHanded && vrRig.handValid[oh])
				raw = VRTwoHandedForward();

			// Adaptive smoothing (VRAimFilter), as VR shooters' aim smoothing
			// does it. Guns only: a lagging spade or block would feel wrong.
			const float amount = p.IsToolWeapon() && vrRig.handValid[th] ? (float)vr_aimSmooth
			                                                             : 0.0F;
			SetVRToolForward(vrRig.aimFilter.Step(raw, dt, amount));

			// Recoil after the smoothing: the filter is fed the hands' aim, never
			// the kick, or it would smooth the kick away.
			vrRig.recoilOn = p.IsToolWeapon() && vrRig.handValid[th];
			if (vrRig.recoilOn)
				vrRig.recoil.Step(dt);
			else
				vrRig.recoil.Reset();
			ApplyVRRecoil();
		}

		void Client::ApplyVRRecoil() {
			if (!vrRig.recoilOn)
				return;
			const float pitch = vrRig.recoil.Pitch(), yaw = vrRig.recoil.Yaw();
			if (pitch == 0.0F && yaw == 0.0F)
				return;
			const Vector3 f = vrRig.handAxes[VRToolHand()][0];
			// Up (the map's -z) squared to the aim: the muzzle lifts towards the
			// sky whatever the wrist's roll, as the view does on a monitor.
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F) + f * f.z;
			if (up.GetLength() < 1.0E-3F)
				up = vrRig.handAxes[VRToolHand()][2] * -1.0F;
			up = up.Normalize();
			Vector3 g = f * std::cos(pitch) + up * std::sin(pitch);
			// Sideways about the vertical, the way Player::Turn turns the view.
			const float c = std::cos(yaw), sn = std::sin(yaw);
			g = MakeVector3(g.x * c - g.y * sn, g.x * sn + g.y * c, g.z);
			SetVRToolForward(g);
		}

		void Client::PlayerRecoiledInVR(Player& p, float yaw, float pitch) {
			if (!p.IsLocalPlayer() || !IsVRFrameActive())
				return;
			// Exactly a monitor's recoil, with bracing as its aiming down the sights
			// (VRRecoilHoldScale). There is no setting for it: softer recoil than
			// a monitor's would be a cheat.
			const float scale =
			  VRRecoilHoldScale(vrRig.twoHanded || vrRig.shouldered, p.IsWalking(),
			                    p.GetWeaponInput().secondary);
			vrRig.recoil.Kick(pitch * scale, yaw * scale);
		}

		namespace {
			// Quick chat on the wrist menu: what gets said most, one touch each.
			const char* const kQuickPhrases[8] = {"Help!",      "Enemy spotted!", "Need blocks",
			                                      "On my way",  "Follow me",      "Defend here",
			                                      "Thanks!",    "gg"};
		} // namespace

		const char* Client::VRQuickPhrase(int i) { return kQuickPhrases[i & 7]; }

		std::vector<Client::VRWristButton> Client::VRWristMenuLayout() const {
			// The wrist canvas (960x600): chat on the left, colours on the right.
			std::vector<VRWristButton> b;
			const float m = 16.0F, gap = 10.0F;
			const float lw = 460.0F, bw = (lw - gap) * 0.5F;
			b.push_back({VRWristButton::Chat, 0, AABB2(m, m, bw, 84.0F), {}});
			b.push_back({VRWristButton::TeamChat, 0, AABB2(m + bw + gap, m, bw, 84.0F), {}});
			const float py = m + 84.0F + 18.0F, ph = 70.0F;
			for (int i = 0; i < 8; i++) {
				const float x = m + static_cast<float>(i % 2) * (bw + gap);
				const float y = py + static_cast<float>(i / 2) * (ph + gap);
				b.push_back({VRWristButton::Phrase, i, AABB2(x, y, bw, ph), {}});
			}
			const float qy = py + 4.0F * (ph + gap) + 6.0F;
			b.push_back({VRWristButton::QuickTarget, 0, AABB2(m, qy, lw, 64.0F), {}});
			// The game's menu and the team and weapon screen: the headset's own
			// menu buttons belong to the runtime (Quest, SteamVR).
			const float tw = (lw - gap * 2.0F) / 3.0F;
			b.push_back({VRWristButton::Menu, 0, AABB2(m, qy + 74.0F, tw, 66.0F), {}});
			// The headlamp; a squeeze of the off hand at the head switches it too.
			b.push_back({VRWristButton::Light, 0, AABB2(m + tw + gap, qy + 74.0F, tw, 66.0F), {}});
			b.push_back(
			  {VRWristButton::Team, 0, AABB2(m + (tw + gap) * 2.0F, qy + 74.0F, tw, 66.0F), {}});

			// The flat game's palette (PaletteView, 8 colours): each hue from
			// dark to light along a row. On the wrist menu, which only opens with
			// the other hand at the wrist: off the watch (testers found it clutter
			// there), but the one way left to pick a colour rather than copy one
			// from a block (a tester could not choose any).
			static const IntVector3 hues[8] = {{128, 128, 128}, {256, 0, 0},   {256, 128, 0},
			                                   {256, 256, 0},   {0, 256, 0},   {0, 256, 256},
			                                   {0, 0, 256},     {256, 0, 256}};
			const float cx = 500.0F;
			const float cs = (static_cast<float>(kVRWristCanvasWidth) - m - cx) / 8.0F;
			for (int r = 0; r < 8; r++) {
				const IntVector3 col = hues[r];
				const IntVector3 white = MakeIntVector3(256, 256, 256);
				for (int j = 0; j < 8; j++) {
					const int k = (j % 4) * 2 + 1;
					IntVector3 c = j < 4 ? (col * k) / 8 - 1 : col + ((white - col) * k) / 8 - 1;
					c.x = std::max(0, c.x);
					c.y = std::max(0, c.y);
					c.z = std::max(0, c.z);
					b.push_back({VRWristButton::Colour, r * 8 + j,
					             AABB2(cx + static_cast<float>(j) * cs,
					                   m + static_cast<float>(r) * cs, cs, cs),
					             c});
				}
			}
			return b;
		}

		void Client::UpdateVRWrist(const VRFrameState& fs, float dt, bool menu) {
			// The other hand at the wrist grows the panel into a menu
			// (OpenXRSystem); here the tool hand uses it. Point and pull the
			// trigger, or just touch a button with the controller's tip.
			const int th = VRToolHand();
			vrRig.wristSendCooldown = std::max(0.0F, vrRig.wristSendCooldown - dt);
			const bool trig = TriggerState(fs.hands[th].trigger, vrRig.wristTriggerHeld);
			const bool trigPressed = trig && !vrRig.wristTriggerHeld;
			vrRig.wristTriggerHeld = trig;
			if (!trig)
				vrRig.wristTriggerOwned = false;

			vrRig.wristMenu = !menu && world && fs.wristShown && fs.wristExpanded;
			if (!menu && world && fs.wristShown)
				MarkTrainingVRStep(training::VRStepWrist);
			vrRig.wristPointing = false;
			int hover = -1;
			Vector2 uv;
			float dist = 1.0F;
			if (vrRig.wristMenu && VRRayHitsWristPanel(fs, th, uv, dist) && dist < 0.6F) {
				vrRig.wristPointing = true;
				vrRig.wristPointDistance = dist;
				vrRig.wristCursor =
				  MakeVector2(uv.x * static_cast<float>(kVRWristCanvasWidth),
				              uv.y * static_cast<float>(kVRWristCanvasHeight));
				const std::vector<VRWristButton> buttons = VRWristMenuLayout();
				for (std::size_t i = 0; i < buttons.size(); i++)
					if (buttons[i].rect.Contains(vrRig.wristCursor))
						hover = static_cast<int>(i);
				if (hover != vrRig.wristHover && hover >= 0)
					VRPulse(th, 0.15F, 0.015F);
				// A touch: the tip comes to the panel (2cm), and has to leave it
				// (5cm) before it can touch again.
				const bool poke = dist < 0.02F && vrRig.wristPokeArmed;
				if (poke)
					vrRig.wristPokeArmed = false;
				if (trigPressed)
					vrRig.wristTriggerOwned = true;
				if ((trigPressed || poke) && hover >= 0) {
					VRPulse(th, 0.5F, 0.04F);
					PressVRWristButton(buttons[static_cast<std::size_t>(hover)]);
				}
			}
			if (dist > 0.05F)
				vrRig.wristPokeArmed = true;
			vrRig.wristHover = hover;
		}

		void Client::PressVRWristButton(const VRWristButton& b) {
			switch (b.kind) {
				case VRWristButton::Chat:
					// The chat field opens in front, with the keyboard under it.
					scriptedUI->EnterGlobalChatWindow();
					break;
				case VRWristButton::TeamChat: scriptedUI->EnterTeamChatWindow(); break;
				case VRWristButton::Phrase:
					// Once a second at most: a hand resting on the panel should not
					// flood the chat.
					if (vrRig.wristSendCooldown <= 0.0F && activeNet) {
						activeNet->SendChat(_Tr("Client", kQuickPhrases[b.index]),
						                    !vrRig.wristToTeam);
						vrRig.wristSendCooldown = 1.0F;
					}
					break;
				case VRWristButton::QuickTarget: vrRig.wristToTeam = !vrRig.wristToTeam; break;
				case VRWristButton::Colour: SetBlockColor(b.color); break;
				case VRWristButton::Menu: scriptedUI->EnterClientMenu(); break;
				case VRWristButton::Light:
					// Not in a body (spectating, dead, a demo): the table view
					if (VRTableAllowed())
						SetVRTable(!vrTable.active);
					else
						ToggleLocalFlashlight();
					break;
				case VRWristButton::Team:
					// As the limbo key (cg_keyLimbo) does.
					inGameLimbo = true;
					limboMenu->Open();
					break;
			}
		}


		bool Client::UpdateVRReload(Player& p, const VRFrameState& fs) {
			// Pressing B is a reload in the game; here it is where one begins. B
			// drops the magazine; the off hand's grip takes a new one from the
			// belt (anywhere at the waist); bringing it to the magazine well, just
			// under the front of the gun, puts it in, and that is the game's reload,
			// with its usual time to finish. B again at any point just reloads.
			// Nothing here reaches the server: it sees the ordinary reload.
			const int toolHand = VRToolHand(), offHand = 1 - toolHand;
			const VRControllerState& T = fs.hands[toolHand];
			const VRControllerState& O = fs.hands[offHand];
			const bool bPressed = T.buttonSecondary && !vrRig.prevSecondary[toolHand];
			const bool offPressed = O.squeeze > 0.6F && !vrRig.prevOffSqueeze;
			vrRig.prevOffSqueeze = O.squeeze > 0.6F;

			if (!(int)vr_reloadGesture) {
				vrRig.reloadStage = 0;
				return T.buttonSecondary;
			}
			Weapon& w = p.GetWeapon();
			if (!p.IsToolWeapon() || w.IsReloading()) {
				vrRig.reloadStage = 0;
				return false;
			}

			if (bPressed) {
				if (vrRig.reloadStage != 0) {
					vrRig.reloadStage = 0;
					return true;
				}
				if (w.GetStock() > 0 && w.GetAmmo() < w.GetClipSize()) {
					vrRig.reloadStage = 1;
					VRPulse(toolHand, 0.4F, 0.05F);
				}
				return false;
			}

			if (vrRig.reloadStage == 1 && offPressed && O.grip.valid && fs.head.valid) {
				// At the waist: well below the eyes, and close to the body.
				Vector3 d = O.grip.position - fs.head.position;
				const bool atBelt = d.y < -0.35F && std::sqrt(d.x * d.x + d.z * d.z) < 0.45F;
				if (atBelt) {
					vrRig.reloadStage = 2;
					VRPulse(offHand, 0.5F, 0.05F);
				}
			} else if (vrRig.reloadStage == 2) {
				if (O.squeeze < 0.3F) {
					vrRig.reloadStage = 1; // dropped it: fetch another
				} else if (vrRig.handValid[0] && vrRig.handValid[1]) {
					if (VRMagazineAtWell(vrRig.handPos[toolHand], vrRig.handAxes[toolHand][0],
					                     vrRig.handPos[offHand], vrRig.scale)) {
						vrRig.reloadStage = 0;
						VRPulse(-1, 0.7F, 0.08F, VRHapticShape::DoubleTap);
						return true;
					}
				}
			}
			return false;
		}

		bool Client::IsVRComfortMode(ClientCameraMode mode) {
			// Free flight is steered by your own stick, like walking; these are not.
			return mode == ClientCameraMode::ThirdPersonLocal ||
			       mode == ClientCameraMode::ThirdPersonFollow ||
			       mode == ClientCameraMode::FirstPersonFollow;
		}

		void Client::UpdateVRComfortCamera(float dt) {
			// Moving the view when nothing of yours moved it is what makes people
			// sick in VR: the eyes see motion the inner ear does not feel. The
			// flat game's death and spectator cameras do exactly that, gliding
			// after a corpse or another player. In a headset the view instead
			// stays where it is, and catches up in a cut: to black at once, the
			// jump, and back in over 0.3s. Cuts are fine; motion is not.
			const ClientCameraMode mode = vrRig.cameraMode;
			const Vector3 target = vrRig.flatCameraOrigin;
			const bool comfort = (int)vr_comfortCamera && IsVRComfortMode(mode);

			// Any change of view (dying, respawning, switching whom you follow) is a
			// cut already: blink it.
			if (mode != vrRig.lastCameraMode) {
				vrRig.lastCameraMode = mode;
				vrRig.fade = 1.0F;
				vrRig.fadingOut = false;
				vrRig.comfortAnchor = target;
				vrRig.comfortAnchored = comfort;
				vrRig.comfortAwayTime = 0.0F;
			}

			if (!comfort) {
				vrRig.comfortAnchored = false;
			} else {
				if (!vrRig.comfortAnchored) {
					vrRig.comfortAnchor = target;
					vrRig.comfortAnchored = true;
				}
				// Catch up when what you are watching is far off, or has been a
				// little off for a while.
				const float away = (target - vrRig.comfortAnchor).GetLength();
				vrRig.comfortAwayTime = away > 1.5F ? vrRig.comfortAwayTime + dt : 0.0F;
				if (!vrRig.fadingOut && vrRig.fade <= 0.0F &&
				    (away > 6.0F || vrRig.comfortAwayTime > 2.0F))
					vrRig.fadingOut = true;
			}

			if (vrRig.fadingOut) {
				vrRig.fade += dt / 0.12F;
				if (vrRig.fade >= 1.0F) {
					vrRig.fade = 1.0F;
					vrRig.fadingOut = false;
					vrRig.comfortAnchor = target;
					vrRig.comfortAwayTime = 0.0F;
				}
			} else if (vrRig.fade > 0.0F) {
				vrRig.fade = std::max(0.0F, vrRig.fade - dt / 0.3F);
			}
		}

		void Client::UpdateVRPresenceNotice() {
			if (vrPresenceCheckAt < 0.0F || time < vrPresenceCheckAt)
				return;
			vrPresenceCheckAt = -1.0F;
			if (!net)
				return;
			if (net->IsVRPresenceAvailable()) {
				SPLog("[VR] The server runs vr_presence");
				return;
			}
			// The training course runs on the game's own server, where the only
			// other players are its targets: who sees your arms does not matter
			// there, and the notice was only in the way.
			if (gui::TrainingHost::Get().IsActive()) {
				SPLog("[VR] The training course: no vr_presence notice");
				return;
			}
			chatWindow->AddMessage(ChatWindow::ColoredMessage(
			  _Tr("Client",
			      "VR: this server does not run vr_presence. Other players see you as a "
			      "normal player, without your arms or the gun in your hand, so a gun or "
			      "tool your eye cannot see along acts from your eye, as a monitor "
			      "player's does."),
			  MsgColorSysInfo));
			// And in front of you for ten seconds, as the training tips are.
			vrPresenceNoticeUntil = time + 10.0F;
			SPLog("[VR] The server does not run vr_presence: told the player");
		}

		void Client::UpdateVRTipAnchor() {
			// The box floats in the world 1.6m out, a little below the view, and
			// trails it: it stays put while you look round it, and once more than
			// 20 degrees off it eases back (testers: glued to the face it was too
			// big and in the way). It moves with you as you walk.
			const SceneDefinition& d = lastSceneDef; // the head's
			const float dt = vrTipValid ? Clamp(time - vrTipTime, 0.0F, 0.1F) : 0.0F;
			vrTipTime = time;
			const float down = 15.0F * 0.0174532925F;
			const Vector3 target =
			  (d.viewAxis[2] * std::cos(down) - d.viewAxis[1] * std::sin(down)).Normalize();
			if (!vrTipValid) {
				vrTipDir = target;
				vrTipValid = true;
				vrTipMoving = false;
			}
			const float off = std::acos(Clamp(Vector3::Dot(vrTipDir, target), -1.0F, 1.0F));
			if (off > 20.0F * 0.0174532925F)
				vrTipMoving = true;
			if (vrTipMoving) {
				const Vector3 next = vrTipDir + (target - vrTipDir) * (1.0F - std::exp(-dt * 4.0F));
				if (next.GetLength() > 1.0E-3F)
					vrTipDir = next.Normalize();
				if (off < 2.0F * 0.0174532925F)
					vrTipMoving = false;
			}
			const float scale = vrTable.active ? vrTable.pose.scale : vrRig.scale;
			vrTipAt = d.viewOrigin + vrTipDir * (1.6F * scale);
		}

		void Client::DrawVRTutorial() {
			// Called per eye: the box where UpdateVRTipAnchor put it, projected for
			// this eye, so it has its own depth and is not pinned to infinity. The
			// no-vr_presence notice for its ten seconds, otherwise the training
			// course's tip for where you are; nothing elsewhere.
			std::string title, tip;
			Vector4 accent = MakeVector4(1, 0.85F, 0.3F, 1);
			if (time < vrPresenceNoticeUntil) {
				title = _Tr("Client", "No VR presence on this server");
				tip = _Tr("Client", "Other players see you as a normal player, without your arms "
				                    "or the gun in your hand. So a gun or tool your eye cannot "
				                    "see along acts from your eye, as a monitor player's does.");
				accent = MakeVector4(0.45F, 0.8F, 1.0F, 1);
			} else if (!TrainingTip(true, title, tip)) {
				return;
			}

			const SceneDefinition& d = lastSceneDef;
			const float s = vrRig.scale;
			Vector2 c, e;
			if (!vrTipValid)
				return;
			const Vector3 at = vrTipAt;
			// Level, square to the way it lies from you
			Vector3 side = Vector3::Cross(vrTipDir, MakeVector3(0.0F, 0.0F, -1.0F));
			if (side.GetLength() < 1.0E-3F)
				side = d.viewAxis[0];
			side = side.Normalize();
			if (Vector3::Dot(side, d.viewAxis[0]) < 0.0F)
				side = side * -1.0F;
			if (Vector3::Dot(at - d.viewOrigin, d.viewAxis[2]) <= 0.0F)
				return; // behind you
			const float ts = vrTable.active ? vrTable.pose.scale : s;
			if (!Project(at, c) || !Project(at + side * (0.3F * ts), e))
				return;
			// 0.6m wide at 1.6m (about 21 degrees), whatever the eye's resolution.
			const float halfW = (e - c).GetLength();
			if (halfW < 40.0F)
				return;
			IFont& font = fontManager->GetGuiFont();
			const float k = halfW * 2.0F / 420.0F; // the text is laid out 420 wide
			const float pad = 12.0F * k;

			// Words to lines of at most 396 layout pixels.
			std::vector<std::string> lines = WrapTipText(font, tip, 396.0F);
			const float lh = font.Measure("Ag").y;
			const float boxH = (lh * (float)lines.size() + 24.0F + lh) * k;
			const AABB2 box(c.x - halfW, c.y - boxH * 0.5F, halfW * 2.0F, boxH);
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.75F));
			renderer->DrawImage(nullptr, box);
			renderer->SetColorAlphaPremultiplied(accent);
			renderer->DrawImage(nullptr, AABB2(box.GetMinX(), box.GetMinY(), 4.0F * k, boxH));
			const std::string& header = title;
			float y = box.GetMinY() + pad;
			font.Draw(header, MakeVector2(box.GetMinX() + pad, y), k, accent);
			y += lh * k;
			for (const auto& l : lines) {
				font.Draw(l, MakeVector2(box.GetMinX() + pad, y), k, MakeVector4(1, 1, 1, 1));
				y += lh * k;
			}
		}

		void Client::VRHurtFeedback(int damage) {
			if (!IsVRActive() || damage <= 0)
				return;
			// Where it came from, across the head (LocalPlayerHurt comes with the
			// health packet, just before)
			const bool known = vrHurtSourceKnown && time - vrHurtSourceTime < 0.5F;
			float side = 0.0F, behind = 0.0F;
			if (known) {
				Vector3 rel = vrHurtSource - vrRig.camera;
				rel.z = 0.0F;
				if (rel.GetSquaredLength() > 1.0E-4F) {
					rel = rel.Normalize();
					Vector3 fw = vrRig.headAxes[0], rt = vrRig.headAxes[1];
					fw.z = rt.z = 0.0F;
					if (fw.GetSquaredLength() > 1.0E-4F && rt.GetSquaredLength() > 1.0E-4F) {
						side = Vector3::Dot(rel, rt.Normalize());
						behind = std::max(0.0F, -Vector3::Dot(rel, fw.Normalize()));
					}
				}
			}
			vrHurtSide = side;
			vrHurtBehind = behind;
			const VRHurtRumble r = VRHurtRumbleFor(damage, side, known);
			for (int hand = 0; hand < 2; hand++)
				if (r.amplitude[hand] > 0.0F)
					VRPulse(hand, r.amplitude[hand], r.seconds, VRHapticShape::Kick);
		}

		bool Client::VRTableAllowed() {
			if (!vrRig.active || !world || !world->GetMap())
				return false;
			if (IsDemoMode())
				return true;
			auto p = world->GetLocalPlayer();
			// Spectators and demos only: a dead player on a team would see the
			// whole map's enemies on it while waiting to respawn
			return !p || p->IsSpectator();
		}

		void Client::AddVRTableHandsToScene() {
			// The forearms and hands as in play (ClientPlayer::AddVRBody's, forearm
			// only), but at the table's scale, so the hands holding the map are
			// seen holding it. vrRig's hands are in the table's world already.
			if (!vrTable.active || !vrRig.active || !IsVRFrameActive())
				return;
			const float play = Clamp((float)vr_worldScale, 0.5F, 4.0F);
			// The table's scale (vrRig.scale is the body's again by the time the
			// scene is drawn: ApplyVRToSceneDefinition puts it back)
			const float k = vrTable.pose.scale / play; // play-sized blocks to the table's
			Handle<IModel> arm = renderer->RegisterModel("Models/Player/Arm.kv6");
			ModelRenderParam param;
			param.customColor = MakeVector3(0.55F, 0.55F, 0.55F);
			if (world)
				if (auto p = world->GetLocalPlayer())
					if (!p->IsSpectator())
						param.customColor = ConvertColorRGB(p->GetColor());
			const float thick = 0.05F * k, foreLen = 0.55F * k;
			for (int h = 0; h < 2; h++) {
				if (!vrRig.handValid[h])
					continue;
				const Vector3* ax = vrRig.handAxes[h];
				const Vector3 wrist = vrRig.handPos[h] - ax[0] * (0.08F * k);
				const Vector3 back = (ax[0] * -0.9F + ax[2] * 0.35F).Normalize();
				const Vector3 elbow = wrist + back * foreLen;
				Vector3 a2 = (wrist - elbow).Normalize();
				Vector3 a1 = Vector3::Cross(a2, MakeVector3(0, 0, 1));
				if (a1.GetLength() < 1.0E-3F)
					a1 = Vector3::Cross(a2, MakeVector3(1, 0, 0));
				a1 = a1.Normalize();
				const Vector3 a0 = Vector3::Cross(a1, a2).Normalize();
				Matrix4 m = Matrix4::FromAxis(a0, a1, a2, elbow) * Matrix4::Scale(thick, thick, thick);
				if (h == VRHandLeft)
					m = m * Matrix4::Translate(0, -1, 0) * Matrix4::Scale(1, -1, 1);
				param.matrix = m;
				renderer->RenderModel(*arm, param);
			}
		}

		void Client::UpdateVRSettle(float dt, float framePeriod) {
			// For the first seconds of a map the frames come slowly (the map is
			// built, its pieces meshed), and a headset judders with them
			// (testers: hard on those who feel it). Until a second of frames has
			// come at the headset's rate, give or take, the world is left out:
			// an empty space, the menus over it. Never more than eight seconds,
			// whatever the settings.
			if (!vrSettling)
				return;
			vrSettleTime += dt;
			const float budget = framePeriod > 0.0F ? framePeriod * 1.3F : 0.02F;
			const bool onTime = dt > 0.0F && dt <= budget;
			vrSettleSteady = onTime ? vrSettleSteady + dt : 0.0F;
			vrSettleFrames++;
			vrSettleSlow += onTime ? 0 : 1;
			vrSettleWorst = std::max(vrSettleWorst, dt);
			if ((vrSettleSteady >= 1.0F && vrSettleTime >= 1.0F) || vrSettleTime >= 8.0F) {
				vrSettling = false;
				SPLog("[VR] The map is shown after %.1f s%s: %d frames, %d over %.1f ms, the "
				      "slowest %.0f ms",
				      vrSettleTime, vrSettleTime >= 8.0F ? " (frames still slow)" : "",
				      vrSettleFrames, vrSettleSlow, budget * 1000.0F, vrSettleWorst * 1000.0F);
			}
		}

		void Client::AddVRTableMapToScene() {
			// The map seen on the table is a coarse copy of it: a voxel for every
			// vr_tableDetail blocks each way, solid if any block in it is, in the
			// colour of the topmost blocks in it. On a 90 cm table a block is
			// about two pixels, so little is lost, and the triangles to draw go
			// down by the square of it (the map itself, skipped there, was
			// 28 ms of a Quest 3's GPU a frame). Built from the map's columns
			// (64 heights a word), every two seconds; given to the renderer
			// again only if a block changed it.
			if (!vrTable.active || !vrRig.active || !IsVRFrameActive() || !map)
				return;
			int f = (int)vr_tableDetail;
			f = f >= 8 ? 8 : f >= 4 ? 4 : f >= 2 ? 2 : 1;
			GameMap& m = *map;
			const int w = m.Width() / f, h = m.Height() / f, d = m.Depth() / f;
			if (w <= 0 || h <= 0 || d <= 0)
				return;
			const bool stale = !vrTableModel || f != vrTableFactor ||
			                   vrTableBuiltFor != map.GetPointerOrNull();
			if (stale || time - vrTableBuiltAt > 2.0F) {
				vrTableBuiltAt = time;
				std::vector<uint32_t> cells(static_cast<size_t>(w) * h * d, 0);
				const uint64_t window = (f >= 64) ? ~0ULL : ((1ULL << f) - 1ULL);
				for (int cx = 0; cx < w; cx++)
					for (int cy = 0; cy < h; cy++)
						for (int cz = 0; cz < d; cz++) {
							uint32_t r = 0, g = 0, b = 0, n = 0;
							for (int dx = 0; dx < f; dx++)
								for (int dy = 0; dy < f; dy++) {
									const int x = cx * f + dx, y = cy * f + dy;
									const uint64_t bits =
									  (m.GetSolidMapWrapped(x, y) >> (cz * f)) & window;
									if (!bits)
										continue;
									int z = cz * f;
									for (uint64_t t = bits; !(t & 1ULL); t >>= 1)
										z++;
									const uint32_t c = m.GetColor(x, y, z);
									r += c & 0xFF;
									g += (c >> 8) & 0xFF;
									b += (c >> 16) & 0xFF;
									n++;
								}
							if (n == 0)
								continue;
							uint32_t c = (r / n) | ((g / n) << 8) | ((b / n) << 16);
							if (c == 0)
								c = 0x010101; // black takes the custom colour
							cells[(static_cast<size_t>(cz) * h + cy) * w + cx] = c;
						}
				if (stale || cells != vrTableCells) {
					auto vm = Handle<VoxelModel>::New(w, h, d);
					for (int cz = 0; cz < d; cz++)
						for (int cy = 0; cy < h; cy++)
							for (int cx = 0; cx < w; cx++) {
								const uint32_t c = cells[(static_cast<size_t>(cz) * h + cy) * w + cx];
								if (c)
									vm->SetSolid(cx, cy, cz, c);
							}
					// Voxel (0,0,0) from 0 to 1: scaled by f, on the map's own grid
					vm->SetOrigin(MakeVector3(0.5F, 0.5F, 0.5F));
					vrTableModelPrev = vrTableModel; // a frame in flight may draw it
					vrTableModel = renderer->CreateModel(*vm);
					vrTableCells = std::move(cells);
					vrTableFactor = f;
					vrTableBuiltFor = map.GetPointerOrNull();
				}
			}
			if (!vrTableModel)
				return;
			ModelRenderParam param;
			param.matrix = Matrix4::Scale(static_cast<float>(f));
			renderer->RenderModel(*vrTableModel, param);
		}

		void Client::SetVRTable(bool on) {
			IVRSystem* vr = GetVRSystem();
			if (on && (!vr || !VRTableAllowed()))
				return;
			if (on == vrTable.active)
				return;
			vrTable.active = on;
			vrTable.held[0] = vrTable.held[1] = false;
			if (!on) {
				VRPulse(-1, 0.3F, 0.05F);
				return;
			}
			const VRFrameState& fs = vr->GetFrameState();
			// On a table in front of you: 55cm ahead and half a metre under the
			// eyes, the whole map about 90cm across, turned the way you face.
			Vector3 ahead = -fs.head.Axis(2);
			ahead.y = 0.0F;
			ahead = ahead.GetLength() > 1.0E-3F ? ahead.Normalize() : MakeVector3(0, 0, -1);
			vrTable.anchor = fs.head.position + ahead * 0.55F + MakeVector3(0.0F, -0.5F, 0.0F);
			const GameMap& map = *world->GetMap();
			const float size = static_cast<float>(std::max(map.Width(), map.Height()));
			vrTable.pose.centre = MakeVector3(map.Width() * 0.5F, map.Height() * 0.5F, 58.0F);
			vrTable.pose.scale = size / 0.9F;
			vrTable.pose.yaw = vrRig.bodyYaw;
			VRPulse(-1, 0.4F, 0.06F);
			ShowAlert(_Tr("Client", "Table view: grip to hold the map, both grips to turn and "
			                        "size it, stick to zoom"),
			          AlertType::Notice, 6.0F);
		}

		void Client::UpdateVRTable(const VRFrameState& fs, float dt) {
			if (!world || !world->GetMap())
				return;
			const GameMap& map = *world->GetMap();
			const float size = static_cast<float>(std::max(map.Width(), map.Height()));
			// From 3m across down to 15cm a map's width... and a single block 2cm
			const float minScale = size / 3.0F, maxScale = std::max(size / 0.15F, 50.0F);

			// A grip holds the point of the model it closed on
			for (int h = 0; h < 2; h++) {
				const VRControllerState& c = fs.hands[h];
				const bool down =
				  c.grip.valid && (vrTable.held[h] ? c.squeeze > 0.4F : c.squeeze > 0.7F);
				if (down && !vrTable.held[h]) {
					vrTable.heldWorld[h] =
					  VRTableToWorld(vrTable.pose, vrTable.anchor, c.grip.position);
					VRPulse(h, 0.3F, 0.03F);
				}
				vrTable.held[h] = down;
			}
			if (vrTable.held[0] && vrTable.held[1]) {
				VRTableHoldTwo(vrTable.pose, vrTable.anchor, vrTable.heldWorld[0],
				               vrTable.heldWorld[1], fs.hands[0].grip.position,
				               fs.hands[1].grip.position, minScale, maxScale);
			} else if (vrTable.held[0] || vrTable.held[1]) {
				const int h = vrTable.held[0] ? 0 : 1;
				VRTableHoldOne(vrTable.pose, vrTable.anchor, vrTable.heldWorld[h],
				               fs.hands[h].grip.position);
			} else {
				// The off hand's stick: up zooms in, sideways turns it, about the
				// middle of the table.
				const Vector2 st = fs.hands[1 - VRToolHand()].thumbstick;
				if (std::fabs(st.y) > 0.2F)
					vrTable.pose.scale =
					  Clamp(vrTable.pose.scale * std::exp(-st.y * 1.2F * dt), minScale, maxScale);
				if (std::fabs(st.x) > 0.2F)
					vrTable.pose.yaw -= st.x * 1.2F * dt;
			}
		}

		void Client::DrawVRHurtEdge() {
			// Per eye, as the vignette: a red ring, its clear middle pushed away
			// from the hit's side so that edge is thicker.
			if (!(bool)vr_hurtEdge || !(bool)cg_hurtScreenEffects)
				return;
			if (!world)
				return;
			stmp::optional<Player&> p = world->GetLocalPlayer();
			if (!p || !p->IsAlive() || p->IsSpectator())
				return;
			const float hit = VRHurtEdgeAmount(damageTaken, time - lastHurtTime);
			const float low = VRLowHealthEdge(lastHealth, time);
			if (hit <= 0.001F && low <= 0.001F)
				return;
			constexpr float kInner = 0.55F;
			if (!vrHurtEdgeImage) {
				const int n = 256;
				auto bmp = Handle<Bitmap>::New(n, n);
				uint32_t* px = bmp->GetPixels();
				for (int y = 0; y < n; y++)
					for (int x = 0; x < n; x++) {
						const float dx = (x + 0.5F) / n * 2.0F - 1.0F;
						const float dy = (y + 0.5F) / n * 2.0F - 1.0F;
						float t = (std::sqrt(dx * dx + dy * dy) - kInner) / (1.0F - kInner);
						t = Clamp(t, 0.0F, 1.0F);
						t = t * t;
						px[y * n + x] = static_cast<uint32_t>(t * 255.0F + 0.5F) << 24;
					}
				vrHurtEdgeImage = renderer->CreateImage(*bmp);
			}
			const SceneDefinition& d = lastSceneDef;
			// The ring's clear part reaches 40 degrees out; the eye sees ~50
			const float clear = 40.0F * 3.14159265F / 180.0F;
			Vector2 c, e;
			if (!Project(d.viewOrigin + d.viewAxis[2], c) ||
			    !Project(d.viewOrigin + d.viewAxis[2] + d.viewAxis[0] * std::tan(clear), e))
				return;
			const float r = (e - c).GetLength() / kInner;
			const float w = renderer->ScreenWidth(), h = renderer->ScreenHeight();
			// The hit's side: the middle moves away from it, so that side
			// closes in (a hit from behind closes in all round, a little more)
			const float shift = hit > low ? vrHurtSide : 0.0F;
			const float grow = hit > low ? 1.0F - 0.15F * vrHurtBehind : 1.0F;
			const Vector2 cc = MakeVector2(c.x - shift * r * 0.35F, c.y);
			const float rr = r * grow;
			const float a = std::min(0.85F, std::max(hit, low));
			// Premultiplied: a dark red
			renderer->SetColorAlphaPremultiplied(MakeVector4(0.55F * a, 0.0F, 0.0F, a));
			renderer->DrawImage(vrHurtEdgeImage, AABB2(cc.x - rr, cc.y - rr, rr * 2.0F, rr * 2.0F));
			auto rect = [&](float x0, float y0, float x1, float y1) {
				x0 = std::max(x0, 0.0F), y0 = std::max(y0, 0.0F);
				x1 = std::min(x1, w), y1 = std::min(y1, h);
				if (x1 > x0 && y1 > y0)
					renderer->DrawImage(nullptr, AABB2(x0, y0, x1 - x0, y1 - y0));
			};
			rect(0.0F, 0.0F, w, cc.y - rr);
			rect(0.0F, cc.y + rr, w, h);
			rect(0.0F, cc.y - rr, cc.x - rr, cc.y + rr);
			rect(cc.x + rr, cc.y - rr, w, cc.y + rr);
		}

		void Client::DrawVRVignette() {
			// Called per eye, with Project set to that eye: the clear circle is
			// centred on where the eye looks straight ahead, which is not the
			// middle of an off-centre eye's picture.
			const float strength = Clamp((float)vr_vignette, 0.0F, 1.0F);
			const float amount = vrRig.vignette;
			if (strength <= 0.0F || amount <= 0.001F)
				return;
			// Clear inside 65% of the image's radius, black at its edge.
			constexpr float kInner = 0.65F;
			if (!vrVignetteImage) {
				const int n = 256;
				auto bmp = Handle<Bitmap>::New(n, n);
				uint32_t* px = bmp->GetPixels();
				for (int y = 0; y < n; y++)
					for (int x = 0; x < n; x++) {
						const float dx = (x + 0.5F) / n * 2.0F - 1.0F;
						const float dy = (y + 0.5F) / n * 2.0F - 1.0F;
						float t = (std::sqrt(dx * dx + dy * dy) - kInner) / (1.0F - kInner);
						t = Clamp(t, 0.0F, 1.0F);
						t = t * t * (3.0F - 2.0F * t);
						px[y * n + x] = static_cast<uint32_t>(t * 255.0F + 0.5F) << 24;
					}
				vrVignetteImage = renderer->CreateImage(*bmp);
			}
			const SceneDefinition& d = lastSceneDef;
			const float clear = VRVignetteClearAngle(amount, strength);
			Vector2 c, e;
			if (!Project(d.viewOrigin + d.viewAxis[2], c) ||
			    !Project(d.viewOrigin + d.viewAxis[2] + d.viewAxis[0] * std::tan(clear), e))
				return;
			const float r = (e - c).GetLength() / kInner;
			const float w = renderer->ScreenWidth(), h = renderer->ScreenHeight();
			// Fades in over the first quarter, so it never pops in at the edge.
			renderer->SetColorAlphaPremultiplied(
			  MakeVector4(0, 0, 0, std::min(1.0F, amount * 4.0F)));
			renderer->DrawImage(vrVignetteImage, AABB2(c.x - r, c.y - r, r * 2.0F, r * 2.0F));
			// Black beyond the image, all round.
			auto rect = [&](float x0, float y0, float x1, float y1) {
				x0 = std::max(x0, 0.0F), y0 = std::max(y0, 0.0F);
				x1 = std::min(x1, w), y1 = std::min(y1, h);
				if (x1 > x0 && y1 > y0)
					renderer->DrawImage(nullptr, AABB2(x0, y0, x1 - x0, y1 - y0));
			};
			rect(0.0F, 0.0F, w, c.y - r);
			rect(0.0F, c.y + r, w, h);
			rect(0.0F, c.y - r, c.x - r, c.y + r);
			rect(c.x + r, c.y - r, w, c.y + r);
		}

		float Client::VRHeadClearance(const Vector3& eye, const Vector3& offset) const {
			if (!world || !world->GetMap())
				return 1.0F;
			const GameMap& map = *world->GetMap();
			// The eye keeps this far (blocks) from any solid block: the near plane,
			// and a little more so a wall does not fill the view.
			const float r = 0.12F;
			const Vector3 probes[7] = {{0, 0, 0},  {r, 0, 0},  {-r, 0, 0}, {0, r, 0},
			                           {0, -r, 0}, {0, 0, r}, {0, 0, -r}};
			auto clear = [&](const Vector3& at) {
				for (const Vector3& d : probes)
					if (map.ClipBox(at.x + d.x, at.y + d.y, at.z + d.z))
						return false;
				return true;
			};
			if (clear(eye + offset))
				return 1.0F;
			// Already in something at the body's own eye (a tight crawlspace): no
			// better place to put it.
			if (!clear(eye))
				return 1.0F;
			float lo = 0.0F, hi = 1.0F;
			for (int i = 0; i < 8; i++) {
				float mid = (lo + hi) * 0.5F;
				if (clear(eye + offset * mid))
					lo = mid;
				else
					hi = mid;
			}
			return lo;
		}

		void Client::ApplyVRTwoHandedAim() {
			const int th = VRToolHand(), oh = 1 - th;
			if (!vrRig.handValid[0] || !vrRig.handValid[1])
				return;
			SetVRToolForward(VRTwoHandedForward());
		}

		void Client::UpdateVRTwoHandedGrip(Player& p, const VRFrameState& fs) {
			// A rifle held by its grip alone wobbles with the wrist; held at the
			// barrel as well, it points where the two hands say, which is steadier
			// and what every VR shooter does. The off hand's grip takes the gun
			// when that hand is on the barrel: ahead of the gun hand along its aim
			// and close to that line. Letting go, or pulling the hands apart, hands
			// it back to the one hand.
			const int th = VRToolHand(), oh = 1 - th;
			const float squeeze = fs.hands[oh].squeeze;
			const bool wasHeld = vrRig.offGripHeld;
			vrRig.offGripHeld = squeeze > (wasHeld ? 0.35F : 0.6F);
			const bool pressed = vrRig.offGripHeld && !wasHeld;
			const bool was = vrRig.twoHanded;
			const float s = vrRig.scale; // blocks per metre

			bool hold = was;
			if (!(int)vr_twoHandedGrip || !p.IsToolWeapon() || !vrRig.handValid[0] ||
			    !vrRig.handValid[1] || !vrRig.offGripHeld) {
				hold = false;
			} else {
				Vector3 d = vrRig.handPos[oh] - vrRig.handPos[th];
				if (!was && pressed) {
					const Vector3& fwd = vrRig.handAxes[th][0];
					float along = Vector3::Dot(d, fwd);
					float off = (d - fwd * along).GetLength();
					hold = along > 0.08F * s && along < 0.7F * s && off < 0.18F * s;
				}
				if (hold && d.GetLength() > 0.9F * s)
					hold = false;
			}

			vrRig.twoHanded = hold;
			if (hold)
				MarkTrainingVRStep(training::VRStepTwoHands);
			if (hold && !was) {
				ApplyVRTwoHandedAim();
				VRPulse(oh, 0.5F, 0.05F);
			} else if (!hold && was) {
				// Back to the gun hand's own aim.
				UpdateVRRigTransforms(fs, vrRig.camera);
			}
		}

		bool Client::GetLocalVRHand(int hand, Vector3& position, Vector3 axes[3]) const {
			if (!vrRig.active || hand < 0 || hand > 1 || !vrRig.handValid[hand])
				return false;
			position = vrRig.handPos[hand];
			for (int i = 0; i < 3; i++)
				axes[i] = vrRig.handAxes[hand][i];
			return true;
		}

		void Client::UpdateVR(float dt) {
			SPADES_MARK_FUNCTION();

			IVRSystem* vr = GetVRSystem();
			if (!vr || !vr->GetFrameState().running) {
				if (vrTable.fogFar) {
					renderer->SetFogDistance(128.0F);
					vrTable.fogFar = false;
				}
				vrTable.active = false;
				if (vrRig.active) {
					// Back on the monitor: let go of everything the controllers held.
					VRReleaseAllKeys();
					if (world) {
						auto p = world->GetLocalPlayer();
						if (p)
							p->ClearVRToolRay();
					}
					vrRig.active = false;
				}
				return;
			}

			const VRFrameState& fs = vr->GetFrameState();
			vrRig.active = true;
			UpdateVRSettle(dt, fs.displayPeriod);

			// The headset taken off and put back on: set down on a table, the
			// tracked height goes with it, and back on the head the body was
			// out by that much (testers: shots far over the sights, running at
			// full speed crouched, until the height was reset by hand). So the
			// height is reset as holding both sticks in does, a second and a
			// half after it is back on, once the head has settled. Off is the
			// game losing focus for over three seconds (or not running for
			// twenty: a headset may stop the app outright; a map loading stalls
			// for less), and the head then reading 12 cm higher or lower, or
			// half a metre away: the system menu opened for a while leaves the
			// head where it was, and a reset then, taken crouched, would have
			// made the crouch the standing height.
			{
				const double now = std::chrono::duration<double>(
				                     std::chrono::steady_clock::now().time_since_epoch())
				                     .count();
				if (!fs.focused) {
					vrRig.lostFocus = true;
				} else {
					const double away = vrRig.lastFocusedAt > 0.0 ? now - vrRig.lastFocusedAt : 0.0;
					const Vector3 moved = fs.head.position - vrRig.lastFocusedHead;
					const bool shifted =
					  fs.head.valid && (std::fabs(moved.y) > 0.12F ||
					                    std::sqrt(moved.x * moved.x + moved.z * moved.z) > 0.5F);
					if (vrRig.calibrated && shifted &&
					    ((vrRig.lostFocus && away > 3.0) || away > 20.0)) {
						vrRig.putOnResetAt = now + 1.5;
						SPLog("[VR] The headset is back on after %.0f s: the height is reset "
						      "shortly", away);
					}
					vrRig.lostFocus = false;
					vrRig.lastFocusedAt = now;
					if (fs.head.valid)
						vrRig.lastFocusedHead = fs.head.position;
					if (vrRig.putOnResetAt > 0.0 && now >= vrRig.putOnResetAt) {
						// Not while looking well down (picking the controllers up,
						// bent over): a moment later, upright
						if (fs.head.valid && -fs.head.Axis(2).y > 0.5F) {
							vrRig.putOnResetAt = now + 0.5;
						} else {
							vrRig.putOnResetAt = 0.0;
							vrRig.heightResetRequested = true;
						}
					}
				}
			}
			vr->SetHeldMap((int)vr_heldMap && (int)vr_wristHud && largeMapView &&
			               largeMapView->IsZoomed());
			if (dt > 0.0F)
				vrRig.fps = vrRig.fps <= 0.0F ? 1.0F / dt : Mix(vrRig.fps, 1.0F / dt, 0.05F);
			UpdateVRComfortCamera(dt);
			UpdateVRPresenceNotice();
			vrRig.scopeActive = false; // asked for again while the scene is built
			// No sneak in a headset: the keyboard's V is not read (see
			// Client::KeyEvent), and the off hand's Y is the map now.
			playerInput.sneak = false;
			vrRig.scale = Clamp((float)vr_worldScale, 0.5F, 4.0F);
			vrRig.poseSendTimer -= dt;
			if (!fs.head.valid)
				return;

			const int toolHand = VRToolHand();
			const int offHand = 1 - toolHand;
			const VRControllerState& T = fs.hands[toolHand];
			const VRControllerState& O = fs.hands[offHand];

			// Both sticks held in for a second recentre, as does the runtime's own
			// recentre and the app first getting focus. A second, because both
			// sticks go in together easily by accident (they are crouch and
			// sprint), and a recentre taken bent over puts the floor in the wrong
			// place: the body then hangs below the head.
			bool bothClicks = fs.hands[0].thumbstickClick && fs.hands[1].thumbstickClick;
			bool heldRecenter = false;
			if (bothClicks) {
				vrRig.stickChord = true;
				vrRig.recenterHold += dt;
				if (vrRig.recenterHold >= 1.0F && !vrRig.recenterHoldFired) {
					vrRig.recenterHoldFired = true;
					heldRecenter = true;
				}
			} else {
				vrRig.recenterHold = 0.0F;
				vrRig.recenterHoldFired = false;
			}
			if (heldRecenter || vrRig.heightResetRequested) {
				vrRig.heightResetRequested = false;
				RecenterVR(fs, true);
				VRPulse(-1, 0.6F, 0.12F);
			} else if (vr->ConsumeRecenterRequest() || (!vrRig.calibrated && fs.focused)) {
				RecenterVR(fs, false);
			}

			// Typing (chat, on the pointed-at keyboard) is a menu too: the
			// controllers are typing, not playing.
			const bool menu = NeedsAbsoluteMouseCoordinate() || AcceptsTextInput();
			// Playing, the front panel follows the head: its overlays are drawn
			// through it onto the world. A menu stands in the room instead.
			if (!menu)
				vr->SetHudFollowsHead();

			// Turning, on the tool hand's stick.
			vrRig.turnRate = 0.0F;
			if (!menu) {
				float x = T.thumbstick.x;
				float snap = (float)vr_snapTurn;
				if (snap > 0.0F) {
					if (!vrRig.snapLatched && std::fabs(x) > 0.7F) {
						vrRig.bodyYaw += std::copysign(DEG2RAD(snap), x);
						vrRig.snapLatched = true;
					} else if (std::fabs(x) < 0.3F) {
						vrRig.snapLatched = false;
					}
				} else if (std::fabs(x) > 0.15F) {
					vrRig.turnRate = x * DEG2RAD((float)vr_smoothTurnSpeed);
					vrRig.bodyYaw += vrRig.turnRate * dt;
				}
			}

			stmp::optional<Player&> maybePlayer;
			if (world)
				maybePlayer = world->GetLocalPlayer();
			ClientCameraMode mode = GetCameraMode();
			bool embodied = mode == ClientCameraMode::FirstPersonLocal && maybePlayer &&
			                maybePlayer->IsAlive();

			// The comfort vignette: the body's own artificial motion. Velocity is
			// blocks per 1/32s (Player::MovePlayer); +Z is down.
			{
				float target = 0.0F;
				if (embodied && (float)vr_vignette > 0.0F) {
					const Vector3 v = maybePlayer->GetVelocity() * 32.0F;
					target = VRVignetteTarget(std::sqrt(v.x * v.x + v.y * v.y),
					                          std::max(0.0F, v.z), vrRig.turnRate);
				}
				vrRig.vignette = VRVignetteStep(vrRig.vignette, target, dt);
			}

			if (embodied) {
				Player& p = maybePlayer.value();
				const float s = vrRig.scale;

				// Room-scale: the head's offset from where the body stands is walked
				// into the body, as far as the walls allow.
				Vector3 offset = MakeVector3(fs.head.position.x - vrRig.anchorX, 0.0F,
				                             fs.head.position.z - vrRig.anchorZ);
				// Not in aim training, which is shot from where you stand (a lean
				// still moves the view, not the body).
				if ((int)vr_roomScale && !menu && !gui::TrainingHost::Get().IsAim()) {
					Vector3 moved = p.VRPushBody(VRToWorld(offset));
					// Back into tracking space: undo the yaw, the axes and the scale.
					float c = std::cos(-vrRig.bodyYaw), sn = std::sin(-vrRig.bodyYaw);
					Vector3 g = MakeVector3(moved.x * c - moved.y * sn, moved.x * sn + moved.y * c,
					                        moved.z) /
					            s;
					vrRig.anchorX += g.y;  // game +y is tracking +X
					vrRig.anchorZ += -g.x; // game +x is tracking -Z
				}
				// Whatever is left is the head leaning past the body: where a wall
				// stopped the body. It is not kept. The view is where the body's eye
				// is, so a headset player sees nothing their hitbox is not out in:
				// no peeking round a corner the body is hidden behind, which no
				// monitor player can do (fair play, see VR.md). Against a wall the
				// view stops with the body.
				float rx = fs.head.position.x - vrRig.anchorX;
				float rz = fs.head.position.z - vrRig.anchorZ;
				float len = std::sqrt(rx * rx + rz * rz);
				const float maxLean = 0.0F;
				if (len > maxLean) {
					float k = 1.0F - maxLean / len;
					vrRig.anchorX += rx * k;
					vrRig.anchorZ += rz * k;
					rx *= maxLean / len;
					rz *= maxLean / len;
				}
				vrRig.residual = VRToWorld(MakeVector3(rx, 0.0F, rz));

				// Crouching for real crouches the character, and the eye is then
				// measured from a crouched height so the view does not drop twice.
				float depth = vrRig.standingHeight - fs.head.position.y;
				float crouchAt = std::max(0.1F, (float)vr_crouchAt);
				if ((int)vr_seated)
					vrRig.physicalCrouch = false;
				else if (!vrRig.physicalCrouch && depth > crouchAt)
					vrRig.physicalCrouch = true;
				else if (vrRig.physicalCrouch && depth < crouchAt - 0.1F)
					vrRig.physicalCrouch = false;
				float reference =
				  vrRig.standingHeight - (p.GetInput().crouch ? 0.9F / s : 0.0F);
				// Never above the game's eye: a head higher than the body's eye would
				// see over cover the hitbox is behind. Lower is fine (and crouches).
				vrRig.lift = Clamp(fs.head.position.y - reference, -0.5F, 0.0F) * s;

				// The head does not go into walls either. The body already stops at
				// them, but the head leans past it by up to 30cm, and a head in a
				// block sees through the map. Its offset from the body's eye stops
				// short of the first block instead, as a head against a wall does.
				{
					Vector3 want = vrRig.residual + MakeVector3(0.0F, 0.0F, -vrRig.lift);
					float t = VRHeadClearance(p.GetEye(), want);
					if (t < 1.0F) {
						vrRig.residual = vrRig.residual * t;
						vrRig.lift *= t;
					}
				}

				Vector3 camera = p.GetEye() + vrRig.residual + MakeVector3(0.0F, 0.0F, -vrRig.lift);
				vrRig.solidHands = true;
				vrRig.toolReach = VRToolReach(p);
				const bool wasBlocked = vrRig.toolBlocked;
				UpdateVRRigTransforms(fs, camera);
				if (menu) {
					if (vrRig.twoHanded) {
						vrRig.twoHanded = false;
						UpdateVRRigTransforms(fs, camera);
					}
				} else {
					UpdateVRTwoHandedGrip(p, fs);
					UpdateVRAimAssist(p, dt);
					// Again, for the aim just settled. It starts from the hand the
					// first pass already stopped, so it finds nothing in the way: the
					// tool is still against the block (it cleared this, and the spade
					// never struck, testers: swinging did nothing).
					const bool stopped = vrRig.toolBlocked;
					ApplyVRToolCollision();
					vrRig.toolBlocked = vrRig.toolBlocked || stopped;
				}
				// A light knock as the tool meets a wall.
				if (vrRig.toolBlocked && !wasBlocked)
					VRPulse(toolHand, 0.3F, 0.03F);
				UpdateVRWrist(fs, dt, menu);

				if (menu) {
					VRReleaseAllKeys();
				} else {
					UpdateVRPlayerInput(p, fs, dt);
				}

				// The facing the server sees: the head, or the tool hand while a
				// tool is in use (see the top of this file).
				bool usingTool = T.trigger > 0.25F || p.IsCookingGrenade() ||
				                 (T.squeeze > 0.7F && !p.IsToolWeapon());
				if (usingTool)
					vrRig.lastToolUseTime = time;
				// Without vr_presence, others see only this facing, and the head
				// looking about while the gun pointed elsewhere looked like a
				// cheat's snapping aim (a tester's report): there, while a gun is
				// held, it is always where the gun points. (Not the spade or
				// blocks: held low, they would slow walking and stop climbing.)
				// Walking, though, it is the head's, unless the gun is in use: the
				// server walks relative to the facing, and from the gun's, walking
				// where the head looks took a zigzag (UpdateVRPlayerInput).
				const bool presence = net && net->IsVRPresenceAvailable();
				const Vector2 walkStick = fs.hands[1 - toolHand].thumbstick;
				const bool walking = walkStick.x * walkStick.x + walkStick.y * walkStick.y >
				                     0.25F * 0.25F;
				bool aimFacing = vrRig.handValid[toolHand] &&
				                 ((!presence && p.IsToolWeapon() && !walking) ||
				                  time - vrRig.lastToolUseTime < 0.4F);
				Vector3 facing = aimFacing ? vrRig.handAxes[toolHand][0] : vrRig.headAxes[0];

				vrRig.gunLaser = false;
				if (vrRig.handValid[toolHand]) {
					Vector3 toolOffset = vrRig.aimPos[toolHand] - p.GetEye();
					Vector3 toolDir = vrRig.handAxes[toolHand][0];
					if (p.IsToolWeapon()) {
						// A gun fires from the eye, along the gun (VRGunShot), so
						// whatever stands in the eye's way takes the shot. Fired from
						// the hand, a gun held
						// out past a corner hit what the eye could not see, with the
						// body behind the wall; the eye is where a monitor player's
						// shot starts. Through the scope, the ray is the scope's, so the
						// shot lands on the reticle and not 0.23 blocks under it, where
						// the barrel is. See VR.md, "Fair play".
						Vector3 impact;
						bool hit = false;
						toolDir = VRGunShot(p, impact, hit);
						toolOffset = MakeVector3(0.0F, 0.0F, 0.0F);
						if (aimFacing)
							facing = toolDir;
						// The laser itself is worked out again where it is drawn
						// (ClientPlayer), from the hand the gun is drawn in.
						vrRig.gunLaser = true;
					} else {
						// Digs and blocks leave the tool in the hand. A hand can be out
						// round a corner the eye cannot see past, so unless the server
						// runs vr_presence (where everyone sees the VR player's arms),
						// a tool the eye has no line of sight to acts from the eye,
						// along the tool's direction, as a monitor player's does.
						// (Past a wall the far end can be clear: the line to it is not.)
						// A spade strike digs the block its tip struck, along the
						// strike, for as long as the press is held
						if (p.IsToolSpade() && time - vrRig.spadeStrikeTime < 0.25F) {
							toolOffset = vrRig.spadeStrikeOrigin - p.GetEye();
							toolDir = vrRig.spadeStrikeDir;
						}
						if (!presence && (VRHeadClearance(p.GetEye(), toolOffset) < 1.0F ||
						                  !VRLineOfSight(p.GetEye(), p.GetEye() + toolOffset)))
							toolOffset = MakeVector3(0.0F, 0.0F, 0.0F);
					}
					p.SetVRToolRay(toolOffset, toolDir);
				}
				else
					p.ClearVRToolRay();

				// Straight up or down has no heading, and walking is relative to
				// the heading: keep a sliver of one.
				if (facing.GetLength2D() < 0.02F)
					facing += MakeVector3(std::cos(vrRig.bodyYaw), std::sin(vrRig.bodyYaw), 0.0F) *
					          0.05F;
				// Not while a menu is up, the game's (the menu button) or the
				// headset's own (the game out of focus): the body turned to wherever
				// you looked in it (testers).
				if (!menu && fs.focused)
					p.SetOrientation(facing.Normalize());
				vrTable.wasSpectating = false; // playing: the next spectate brings the table
			} else {
				if (maybePlayer)
					maybePlayer->ClearVRToolRay();
				vrRig.solidHands = false;
				vrRig.toolBlocked = false;
				vrRig.twoHanded = false;
				vrRig.shouldered = false;
				vrRig.stock = VRStockState();
				vrRig.aimFilter.Reset();
				vrRig.recoil.Reset();
				vrRig.recoilOn = false;
				vrRig.residual = MakeVector3(0, 0, 0);
				vrRig.lift = 0.0F;
				UpdateVRRigTransforms(fs, lastSceneDef.viewOrigin);
				UpdateVRWrist(fs, dt, menu);

				// The table view: the off hand's stick click alone (both together
				// recentre) turns it on and off.
				const bool tableClick = O.thumbstickClick && !T.thumbstickClick;
				if (!menu && tableClick && !vrTable.toggleLatched)
					SetVRTable(!vrTable.active);
				vrTable.toggleLatched = O.thumbstickClick;
				{
					auto lp = world ? world->GetLocalPlayer() : stmp::optional<Player&>();
					const bool spectating = IsDemoMode() || !lp || lp->IsSpectator();
					// Until the table can open (on joining, the map is not there
					// yet), the change is kept for the next frame: used up on that
					// first frame, spectating never brought the table (testers).
					if (spectating && !vrTable.wasSpectating && (int)vr_tableWhenSpectating) {
						if (VRTableAllowed()) {
							SetVRTable(true);
							vrTable.wasSpectating = true;
						}
					} else {
						vrTable.wasSpectating = spectating;
					}
				}

				if (menu) {
					VRReleaseAllKeys();
				} else if (vrTable.active) {
					// The hands hold the model; nothing else.
					VRReleaseAllKeys();
					UpdateVRTable(fs, dt);
				} else {
					// Dead or spectating: the triggers cycle whom to follow and A
					// switches the follow camera, as the mouse and space bar do.
					bool fire = TriggerState(T.trigger, vrRig.triggerHeld[toolHand]);
					vrRig.triggerHeld[toolHand] = fire;
					VRSetKey(cg_keyAttack,
					         fire && !vrRig.wristPointing && !vrRig.wristTriggerOwned);
					VRSetKey(cg_keyAltAttack, T.squeeze > 0.7F);
					VRSetKey(cg_keyJump, T.buttonPrimary);
					VRSetKey(cg_keyReloadWeapon, T.buttonSecondary);
					VRSetKey(cg_keyScoreboard, O.squeeze > 0.7F);

					if (mode == ClientCameraMode::Free) {
						// The free camera flies where the head looks.
						Vector3 f = vrRig.headAxes[0];
						followAndFreeCameraState.yaw = std::atan2(-f.y, -f.x);
						followAndFreeCameraState.pitch = std::asin(Clamp(f.z, -1.0F, 1.0F));
						Vector2 st = O.thumbstick;
						VRSetKey(cg_keyMoveForward, st.y > 0.4F);
						VRSetKey(cg_keyMoveBackward, st.y < -0.4F);
						VRSetKey(cg_keyMoveRight, st.x > 0.4F);
						VRSetKey(cg_keyMoveLeft, st.x < -0.4F);
						VRSetKey(cg_keySprint, O.thumbstickClick);
					}
				}
			}

			for (int h = 0; h < 2; h++) {
				vrRig.prevPrimary[h] = fs.hands[h].buttonPrimary;
				vrRig.prevSecondary[h] = fs.hands[h].buttonSecondary;
				vrRig.prevStickClick[h] = fs.hands[h].thumbstickClick;
			}
			if (!fs.hands[0].thumbstickClick && !fs.hands[1].thumbstickClick)
				vrRig.stickChord = false;
		}

		bool Client::VRToolHasReach(Player& p) {
			// As the laser's green dot: the block the tool's ray meets is in reach
			// by the game's measure for the tool, from the eye.
			GameMap* m = map.GetPointerOrNull();
			if (!m)
				return false;
			GameMap::RayCastResult r = m->CastRay2(p.GetToolOrigin(), p.GetToolDirection(), 32);
			if (!r.hit || r.startSolid)
				return false;
			if (p.IsToolBlock())
				return p.InBuildRange(r.hitBlock + r.normal);
			return (MakeVector3(r.hitBlock) + 0.5F - p.GetEye()).GetChebyshevLength() <
			       static_cast<float>(MAX_DIG_DISTANCE);
		}

		bool Client::VRLineOfSight(const Vector3& from, const Vector3& to) const {
			if (!world || !world->GetMap())
				return true;
			Vector3 d = to - from;
			const float len = d.GetLength();
			if (len < 1.0E-4F)
				return true;
			GameMap::RayCastResult r = world->GetMap()->CastRay2(from, d / len, (int)len + 3);
			// Already in a block at `from`: no better answer (as VRHeadClearance).
			if (!r.hit || r.startSolid)
				return true;
			return (r.hitPos - from).GetLength() >= len;
		}

		Vector3 Client::VRGunShot(Player& p, Vector3& impact, bool& hit) {
			const int toolHand = VRToolHand();
			const Vector3 eye = p.GetEye();
			// The ray that finds what the gun aims at leaves the eye, along the
			// gun: the shot leaves the eye too, so they are one line. From the
			// gun (and later 0.2m over it), it cleared a ledge or a corner the
			// eye's line met, and the dot jumped back onto the cover; and held at
			// the chest, a window or low cover in front of the feet stopped it
			// where the eye saw over (testers' reports). Up close the dot is the
			// eye-to-hand offset from the barrel's own line; it is always where
			// the shot lands. Through the scope, the scope's line.
			// The ray that finds the target leaves your real eye (the head the
			// headset tracks), not the body's eye the game keeps: the two are
			// apart by the head's lean and by your height against the game's eye
			// height, often 10 to 30 cm, mostly up and down. From the body's eye
			// the target was that far off the sights at every range, and the
			// laser climbed to it the more steeply the closer it was (testers:
			// shots missing what the sights and the laser were on). Down the
			// sights the real eye is on their line, so the shot lands on them.
			// The shot itself still leaves the body's eye, towards that target,
			// as the server expects.
			Vector3 from = vrRig.camera;
			Vector3 dir = vrRig.handAxes[toolHand][0];
			if (vrRig.scopeAimValid) {
				from = vrRig.scopeAimOrigin;
				dir = vrRig.scopeAimDir;
			}
			// A gun pushed through a wall (tool collision is off) shoots the wall.
			// Its ray, from beyond, found whoever stood behind it, and the shot
			// and the laser swung to them: a tester's report, an ESP. Measured to
			// the muzzle, as the laser is: to the hand, a rifle held over a wall
			// two blocks high, its grip inside the top block and its barrel clear,
			// shot the wall (testers' report).
			Vector3 muzzle = vrRig.aimPos[toolHand];
			if (p.GetId() < clientPlayers.size() && clientPlayers[p.GetId()])
				muzzle = clientPlayers[p.GetId()]->VRGunLaserOrigin(vrRig.handPos[toolHand],
				                                                     vrRig.handAxes[toolHand]);
			if (GameMap* m = map.GetPointerOrNull()) {
				Vector3 toGun = muzzle - eye;
				const float len = toGun.GetLength();
				if (len > 1.0E-3F) {
					toGun = toGun / len;
					GameMap::RayCastResult r = m->CastRay2(eye, toGun, (int)len + 3);
					if (r.hit && !r.startSolid && (r.hitPos - eye).GetLength() < len) {
						impact = r.hitPos;
						hit = true;
						return toGun;
					}
				}
			}
			const Vector3 target = VRGunAimPoint(p, from, dir, hit);
			Vector3 shot = target - eye;
			const float toTarget = shot.GetLength();
			if (toTarget > 0.05F)
				shot = shot / toTarget;
			else
				shot = dir;
			// Short of the target when the eye cannot see it: what is in between.
			impact = target;
			if (GameMap* m = map.GetPointerOrNull()) {
				GameMap::RayCastResult r = m->CastRay2(eye, shot, 256);
				if (r.hit && !r.startSolid && (r.hitPos - eye).GetLength() < toTarget - 0.05F) {
					impact = r.hitPos;
					hit = true;
				}
			}
			return shot;
		}

		Vector3 Client::VRGunAimPoint(Player& p, const Vector3& from, const Vector3& dir,
		                              bool& hit) {
			// The first block or player hitbox on the ray, or 128 blocks out (the
			// fog) if none. Players count, or a shot at someone close, from an eye
			// an arm's length off the ray, would pass beside them.
			float best = 128.0F;
			hit = false;
			if (GameMap* m = map.GetPointerOrNull()) {
				GameMap::RayCastResult r = m->CastRay2(from, dir, 256);
				if (r.hit && !r.startSolid) {
					const float d = (r.hitPos - from).GetLength();
					if (d < best) {
						best = d;
						hit = true;
					}
				}
			}
			if (world) {
				for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
					auto other = world->GetPlayer(static_cast<unsigned int>(i));
					if (!other || &other.value() == &p || !other->IsAlive() || other->IsSpectator())
						continue;
					if (!other->RayCastApprox(from, dir))
						continue;
					Player::HitBoxes hb = other->GetHitBoxes(true);
					OBB3* boxes[5] = {&hb.head, &hb.torso, &hb.limbs[0], &hb.limbs[1],
					                        &hb.limbs[2]};
					for (OBB3* box : boxes) {
						Vector3 at;
						if (box->RayCast(from, dir, &at)) {
							const float d = (at - from).GetLength();
							if (d < best) {
								best = d;
								hit = true;
							}
						}
					}
				}
			}
			return from + dir * best;
		}

		void Client::UpdateVRPlayerInput(Player& p, const VRFrameState& fs, float dt) {
			const int toolHand = VRToolHand();
			const int offHand = 1 - toolHand;
			const VRControllerState& T = fs.hands[toolHand];
			const VRControllerState& O = fs.hands[offHand];

			// Walking, on the off hand's stick, relative to where the head faces.
			// The server only knows eight directions relative to the facing it was
			// sent. While walking that facing is the head's (UpdateVR), so the
			// stick is rounded to the nearest of the eight, as keys are, and
			// straight ahead is straight ahead. While a gun is in use the facing is
			// the gun's: rounded, the walk went up to 22.5 degrees off where the
			// head looked (off a tower's ledge, a tester's report), so there it
			// alternates between the two directions either side, held 1/20s at a
			// time, in the mix that averages to the wanted one (VRWalkDither).
			// That alternation, used for all walking until 0.3, zigzagged the view
			// (testers' report).
			Vector2 stick = O.thumbstick;
			float mag = std::sqrt(stick.x * stick.x + stick.y * stick.y);
			bool fwd = false, back = false, left = false, right = false;
			// A stick click counts when it is let go, and not at all if the other
			// stick went in meanwhile: that is the recentre chord.
			if ((int)vr_sprintHold)
				vrRig.sprintToggle = O.thumbstickClick && !vrRig.stickChord;
			else if (!O.thumbstickClick && vrRig.prevStickClick[offHand] && !vrRig.stickChord)
				vrRig.sprintToggle = !vrRig.sprintToggle;
			if (mag > 0.25F) {
				Vector3 hf = vrRig.headAxes[0];
				hf.z = 0.0F;
				Vector3 hr = vrRig.headAxes[1];
				hr.z = 0.0F;
				if (hf.GetLength() > 0.01F && hr.GetLength() > 0.01F) {
					Vector3 want = (hf.Normalize() * stick.y + hr.Normalize() * stick.x).Normalize();
					Vector3 f2 = p.GetFront2D();
					Vector3 r2 = p.GetRight();
					float a = Vector3::Dot(want, f2), b = Vector3::Dot(want, r2);
					const float angle = std::atan2(b, a);
					int dir;
					if (Vector3::Dot(f2, hf.Normalize()) > 0.999F) { // facing the head's
						dir = static_cast<int>(std::lround(angle / kQuarterPi));
						dir = (dir % 8 + 8) % 8;
						vrRig.walkLean = 0.0F;
						vrRig.walkHold = 0.0F;
					} else {
						dir = VRWalkDither(angle, dt, vrRig.walkLean, vrRig.walkHold,
						                   vrRig.walkSecond);
					}
					// 0 forward, then clockwise (to the right) in eighths.
					fwd = dir == 7 || dir == 0 || dir == 1;
					right = dir >= 1 && dir <= 3;
					back = dir >= 3 && dir <= 5;
					left = dir >= 5 && dir <= 7;
				}
			} else {
				if (!(int)vr_sprintHold)
					vrRig.sprintToggle = false;
				vrRig.walkLean = 0.0F;
				vrRig.walkHold = 0.0F;
			}
			VRSetKey(cg_keyMoveForward, fwd);
			VRSetKey(cg_keyMoveBackward, back);
			VRSetKey(cg_keyMoveLeft, left);
			VRSetKey(cg_keyMoveRight, right);
			VRSetKey(cg_keySprint, vrRig.sprintToggle && mag > 0.25F);

			// Tool hand: trigger uses the tool, grip is its second action (dig,
			// build a line). A gun has no second action in VR: aiming is holding
			// it up to the eye.
			bool fire = TriggerState(T.trigger, vrRig.triggerHeld[toolHand]);
			vrRig.triggerHeld[toolHand] = fire;
			// Not while the trigger is working the wrist menu.
			if (vrRig.wristPointing || vrRig.wristTriggerOwned)
				fire = false;

			// Swinging the spade hits, as the trigger does. One swing is one hit:
			// latched until the hand slows to under half the speed. The press is
			// held a moment, so a swing that comes as the spade is still recovering
			// from the last one lands as soon as it is ready.
			{
				const float swingAt = (float)vr_swingSpeed;
				vrRig.swingHold = std::max(0.0F, vrRig.swingHold - dt);
				const bool contact = (int)vr_spadeContact != 0;
				if (p.IsToolSpade() && contact && vrRig.handValid[toolHand]) {
					// A strike: the spade comes to rest on a block (it is solid:
					// ApplyVRToolCollision stopped it) with the hand still moving
					// into it at a brisk pace, not the arm's full swing. One strike,
					// one hit: pull it off the block to strike again.
					const float speed = (vrRig.handVelocity[toolHand] / vrRig.scale).GetLength();
					const bool touching = vrRig.toolBlocked;
					if (touching && !vrRig.spadeTouching && speed > 0.7F) {
						const Vector3 dir = vrRig.handAxes[toolHand][0];
						const Vector3 tip = vrRig.handPos[toolHand] + dir * vrRig.toolReach;
						vrRig.spadeStrikeTime = time;
						vrRig.spadeStrikeOrigin = tip - dir * 0.4F;
						vrRig.spadeStrikeDir = dir;
						vrRig.swingHold = 0.15F;
						VRPulse(toolHand, Clamp(0.4F + speed * 0.2F, 0.4F, 1.0F), 0.06F,
						        VRHapticShape::Kick);
					}
					vrRig.spadeTouching = touching;
					// Players are not solid, so a strike never comes against one:
					// a swing at the arm's pace with the tip by somebody hits, as
					// it did before the spade was made solid
					bool byPlayer = false;
					if (world && swingAt > 0.0F) {
						const Vector3 tip = vrRig.handPos[toolHand] +
						                    vrRig.handAxes[toolHand][0] * vrRig.toolReach;
						for (size_t i = 0; i < world->GetNumPlayerSlots() && !byPlayer; i++) {
							auto other = world->GetPlayer(static_cast<unsigned int>(i));
							if (!other || &other.value() == &p || !other->IsAlive() ||
							    other->IsSpectator())
								continue;
							const Vector3 body = other->GetEye() + MakeVector3(0.0F, 0.0F, 1.0F);
							byPlayer = (body - tip).GetLength() < 1.6F;
						}
					}
					if (byPlayer && !vrRig.swingLatched && speed > swingAt) {
						vrRig.swingLatched = true;
						vrRig.swingHold = 0.15F;
					} else if (speed < swingAt * 0.5F) {
						vrRig.swingLatched = false;
					}
				} else if (p.IsToolSpade() && swingAt > 0.0F && vrRig.handValid[toolHand]) {
					float speed = (vrRig.handVelocity[toolHand] / vrRig.scale).GetLength();
					if (!vrRig.swingLatched && speed > swingAt) {
						vrRig.swingLatched = true;
						vrRig.swingHold = 0.15F;
					} else if (speed < swingAt * 0.5F) {
						vrRig.swingLatched = false;
					}
				} else {
					vrRig.swingLatched = false;
					vrRig.swingHold = 0.0F;
				}
			}

			// A grenade leaves with the hand's speed at release. Keep the fastest
			// recent sample: the hand is already slowing when the trigger opens.
			if (p.IsToolGrenade() && p.IsCookingGrenade() && vrRig.handValid[toolHand]) {
				Vector3 v = vrRig.handVelocity[toolHand] / vrRig.scale; // m/s
				float speed = v.GetLength();
				float full = std::max(1.0F, (float)vr_throwSpeed);
				float power = Clamp(speed / full, 0.15F, 1.0F);
				Vector3 dir = speed > 1.0F ? vrRig.handVelocity[toolHand].Normalize()
				                           : vrRig.handAxes[toolHand][0];
				vrRig.throwPeakAge += dt;
				if (power >= vrRig.throwPeakPower || vrRig.throwPeakAge > 0.12F) {
					vrRig.throwPeakPower = power;
					vrRig.throwPeakDir = dir;
					vrRig.throwPeakAge = 0.0F;
				}
				p.SetVRThrowVelocity(vrRig.throwPeakDir * vrRig.throwPeakPower);
			} else {
				vrRig.throwPeakPower = 0.0F;
			}
			VRSetKey(cg_keyAttack, fire || vrRig.swingHold > 0.0F);

			// Holsters: the gun hand's grip at a slot on the body takes that tool.
			// A light buzz says the hand is at one. The grip that took a tool is not
			// also the tool's second action (a dig, a line of blocks) until let go.
			//
			// A holster only counts for a grip that begins there. Testers found
			// the grip taking blocks when they meant to dig or build: a hand hanging
			// loose at the hip while a wall was drawn crossed the blocks' holster
			// and the line stopped, and the spade pointed at the ground beside the
			// feet put the hand in it. So a grip that began as the tool's action
			// stays that until let go, wherever the hand goes; no holster counts
			// while the gun is held in both hands, nor the one the held tool came
			// from, nor any while the spade or blocks have a block in reach.
			static const Player::ToolType holstered[4] = {
			  Player::ToolWeapon, Player::ToolSpade, Player::ToolBlock, Player::ToolGrenade};
			int slot = VRHolsterNone;
			// Only with the tool pointed down, at the ground by the feet where
			// the hand is at the hip: in a tunnel a block is always in reach, and
			// no holster ever counted there (a tester's report). Pointed almost
			// straight down it counts as digging whether or not a block is in
			// reach: dug three deep, the floor of the hole was out of it, and the
			// grip took blocks from the hip (testers).
			const float down = p.GetToolDirection().z;
			const bool reach = (p.IsToolSpade() || p.IsToolBlock()) && down > 0.5F &&
			                   (down > 0.8F || VRToolHasReach(p));
			// The holsters face the body's way, not the eyes' (VRHolsterFrame)
			VRHolsterFrameUpdate(vrRig.holsterFrame, fs, dt, vrRig.standingHeight,
			                     (int)vr_holsterFollow != 0);
			vrRig.holsterFrame.hipDrop =
			  Clamp(vrRig.holsterFrame.hipDrop - Clamp((float)vr_hipHeight, -30.0F, 30.0F) * 0.01F,
			        0.25F, 1.2F);
			// The grip's grace (a squeeze a moment before or after the hand is at
			// a holster still takes from it) only near one: anywhere else it held
			// the grip's own action back 0.2 s, and a line of blocks drawn
			// quickly began where the hand had got to by then (testers).
			bool nearHolster = false;
			if (fs.hands[toolHand].grip.valid) {
				for (int i = 0; i < 4 && !nearHolster; i++) {
					Vector3 at, hf, hr;
					float radius;
					if (VRHolsterPlace(fs, toolHand, i, at, radius, hf, hr, &vrRig.holsterFrame))
						nearHolster =
						  (fs.hands[toolHand].grip.position - at).GetLength() < radius + 0.15F;
				}
			}
			const bool mayHolster =
			  (int)vr_holsters && !vrRig.twoHanded && !reach && nearHolster;
			if ((int)vr_holsters && VRGripMayHolster(vrRig.grip, vrRig.twoHanded, reach)) {
				slot = VRHolsterAt(fs, toolHand, vrRig.holsterHover, &vrRig.holsterFrame);
				if (slot >= 0 && p.GetTool() == holstered[slot])
					slot = VRHolsterNone;
			}
			if (slot != vrRig.holsterHover) {
				if (slot >= 0)
					VRPulse(toolHand, 0.25F, 0.02F);
				vrRig.holsterHover = slot;
			}
			// A squeeze a moment early or late still takes (kVRGripGrace)
			const int take = VRGripUpdate(vrRig.grip, T.squeeze > 0.7F, slot, dt, mayHolster);
			if (take >= 0 && p.IsToolSelectable(holstered[take])) {
				SetSelectedTool(holstered[take]);
				MarkTrainingVRStep(training::VRStepHolster);
				VRPulse(toolHand, 0.6F, 0.06F);
			}
			VRSetKey(cg_keyAltAttack, VRGripActionArmed(vrRig.grip) && !p.IsToolWeapon());

			VRSetKey(cg_keyJump, T.buttonPrimary);
			VRSetKey(cg_keyReloadWeapon, UpdateVRReload(p, fs));
			// A reload that finishes says so in the gun hand, as the laser turns
			// back from amber (ClientPlayer): the sound alone was not enough.
			{
				const bool reloading = p.IsToolWeapon() && p.GetWeapon().IsReloading();
				if (vrRig.wasReloading && !reloading && p.IsToolWeapon())
					MarkTrainingVRStep(training::VRStepReload);
				if (vrRig.wasReloading && !reloading && p.IsToolWeapon())
					VRPulse(toolHand, 0.5F, 0.06F);
				vrRig.wasReloading = reloading;
			}
			if (!T.thumbstickClick && vrRig.prevStickClick[toolHand] && !vrRig.stickChord)
				vrRig.crouchToggle = !vrRig.crouchToggle;
			VRSetKey(cg_keyCrouch, vrRig.crouchToggle || vrRig.physicalCrouch);

			// The palette is on the wrist menu. The turn stick's up/down walked it
			// too, and a stick pushed by accident changed the colour (testers).
			VRSetKey(cg_keyPaletteRight, false);
			VRSetKey(cg_keyPaletteLeft, false);

			// Off hand: X/Y (or the second hand's A/B) step through the tools with
			// vr_toolButtons. Without it (the default: the holsters take the tools)
			// X grabs a block's colour and Y opens and closes the big map (it was
			// sneak, which testers took for nothing: walking at half speed).
			// Not on a grip: it would be pressed by every hold.
			const bool toolButtons = (int)vr_toolButtons != 0;
			auto toggleMap = [&]() {
				std::string key = KeyEventName(cg_keyToggleMapZoom);
				KeyEvent(key, true);
				KeyEvent(key, false);
			};
			if (!toolButtons && p.IsToolBlock() && O.buttonPrimary && !vrRig.prevPrimary[offHand])
				CaptureColor();
			// Not with B: B and Y together are the menu
			if (!toolButtons && O.buttonSecondary && !vrRig.prevSecondary[offHand] &&
			    !T.buttonSecondary)
				toggleMap();
			auto cycleTool = [&](int step) {
				Player::ToolType t = p.GetTool();
				for (int i = 0; i < 4; i++) {
					int n = (static_cast<int>(t) + step + 4) % 4;
					t = static_cast<Player::ToolType>(n);
					if (p.IsToolSelectable(t))
						break;
				}
				SetSelectedTool(t);
				VRPulse(offHand, 0.3F, 0.03F);
			};
			if (toolButtons && O.buttonPrimary && !vrRig.prevPrimary[offHand])
				cycleTool(-1);
			if (toolButtons && O.buttonSecondary && !vrRig.prevSecondary[offHand] &&
			    !T.buttonSecondary)
				cycleTool(+1);

			// The headlamp's switch is at the head, where a real one is: the off
			// hand brought up beside the headset and squeezed. While it is there,
			// its grip is the switch and not the scores.
			{
				const bool squeezed = O.squeeze > 0.7F;
				bool atHead = false;
				if (O.grip.valid && fs.head.valid)
					atHead = (O.grip.position - fs.head.position).GetLength() < 0.18F;
				if (squeezed && !vrRig.prevLampSqueeze && atHead && !vrRig.twoHanded &&
				    vrRig.reloadStage == 0) {
					ToggleLocalFlashlight();
					VRPulse(offHand, 0.4F, 0.04F);
				}
				vrRig.prevLampSqueeze = squeezed;
				// The hand counts as at the head until the grip lets go.
				vrRig.lampHandAtHead = squeezed ? (vrRig.lampHandAtHead || atHead) : false;
			}

			// Off hand grip shows the scores (unless it is holding the gun or a
			// magazine, or switching the headlamp). Its trigger, held while it holds
			// the gun's barrel, braces the gun on the shoulder (UpdateVRAimAssist);
			// otherwise it opens and closes the big map, as Y does (Vive and WMR
			// controllers have no Y). (It used to pick a
			// block's colour while blocks were held, so a map opened before taking
			// blocks could not be closed: a tester could not find the way out of
			// it. The colour is X's now.)
			VRSetKey(cg_keyScoreboard, O.squeeze > 0.7F && !vrRig.twoHanded &&
			                             vrRig.reloadStage == 0 && !vrRig.lampHandAtHead);
			bool offTrigger = TriggerState(O.trigger, vrRig.triggerHeld[offHand]);
			const bool onBarrel = p.IsToolWeapon() && vrRig.twoHanded;
			vrRig.braceHeld = offTrigger && onBarrel;
			if (offTrigger && !vrRig.triggerHeld[offHand] && !onBarrel) {
				if (toolButtons && p.IsToolBlock() && !(largeMapView && largeMapView->IsZoomed()))
					CaptureColor(); // X steps tools here: the trigger keeps the colour
				else
					toggleMap();
			}
			vrRig.triggerHeld[offHand] = offTrigger;
		}

		void Client::ApplyVRToSceneDefinition(SceneDefinition& def, ClientCameraMode mode) {
			if (!vrRig.active)
				return;
			IVRSystem* vr = GetVRSystem();
			if (!vr)
				return;
			const VRFrameState& fs = vr->GetFrameState();
			if (!fs.head.valid)
				return;

			Vector3 origin = def.viewOrigin;
			vrRig.flatCameraOrigin = origin;
			vrRig.cameraMode = mode;
			if (mode == ClientCameraMode::FirstPersonLocal && world) {
				auto p = world->GetLocalPlayer();
				if (p && p->IsAlive())
					origin = p->GetEye() + vrRig.residual + MakeVector3(0.0F, 0.0F, -vrRig.lift);
			} else if ((int)vr_comfortCamera && IsVRComfortMode(mode) && vrRig.comfortAnchored) {
				origin = vrRig.comfortAnchor;
			}
			// The table view: the head is outside the world, looking at it as a
			// model. Seen through the same eyes, at the table's scale and turn.
			const bool table = vrTable.active && VRTableAllowed();
			if (!table)
				vrTable.active = false;
			const float bodyYaw = vrRig.bodyYaw, bodyScale = vrRig.scale;
			if (table) {
				origin = VRTableToWorld(vrTable.pose, vrTable.anchor, fs.head.position);
				vrRig.bodyYaw = vrTable.pose.yaw;
				vrRig.scale = vrTable.pose.scale;
			}
			if (table != vrTable.fogFar) {
				// From outside, the fog would hide it all
				renderer->SetFogDistance(table ? 8192.0F : 128.0F);
				vrTable.fogFar = table;
			}
			UpdateVRRigTransforms(fs, origin);

			// The single view is the head. Its field of view is the HUD panel's, so
			// anything the 2D layer pins to a point in the world (names, pings,
			// markers) lands on that point as seen through the panel.
			def.viewOrigin = origin;
			def.viewAxis[0] = vrRig.headAxes[1];
			def.viewAxis[1] = -vrRig.headAxes[2];
			def.viewAxis[2] = vrRig.headAxes[0];
			float aspect = renderer->ScreenHeight() / std::max(1.0F, renderer->ScreenWidth());
			def.fovX = 2.0F * std::atan(fs.hudWidth * 0.5F / fs.hudDistance);
			def.fovY = 2.0F * std::atan(fs.hudWidth * aspect * 0.5F / fs.hudDistance);

			for (int i = 0; i < 2; i++) {
				auto& e = def.vrEyes[i];
				const VRPose& eye = fs.eyes[i];
				e.origin = origin + VRToWorld(eye.position - fs.head.position);
				e.axis[0] = VRToWorld(eye.Axis(0)).Normalize();
				e.axis[1] = VRToWorld(eye.Axis(1)).Normalize();
				e.axis[2] = VRToWorld(-eye.Axis(2)).Normalize();
				e.fovX = 2.0F * std::atan(fs.eyeTanX[i]);
				e.fovY = 2.0F * std::atan(fs.eyeTanY[i]);
				e.asymmetric = fs.eyeAsymmetric;
				e.tangents = FovTangents{fs.eyeLeft[i], fs.eyeRight[i], fs.eyeDown[i], fs.eyeUp[i]};
			}
			def.numVREyes = 2;
			if (table) {
				vrRig.bodyYaw = bodyYaw;
				vrRig.scale = bodyScale;
				// Not wholeMap: the map itself is not drawn (skipWorld), and with
				// it every chunk of it was meshed in the frame the table opened
				// No water: its sheet is 128 blocks round the eye, so on the table
				// it covered a patch under you, and went past the map's edges
				// (testers). The sea floor shows instead.
				def.skipWater = true;
				// The map itself is drawn coarse, as a model (AddVRTableMapToScene)
				def.skipWorld = true;
				def.zNear = 0.5F;
				def.zFar = 8192.0F;
			}

			// Nothing that moves the picture without the head moving: that is
			// what makes people sick. Shake, roll and zoom are already gone with
			// the view axes and field of view above.
			def.denyCameraBlur = true;
			def.radialBlur = 0.0F;
			def.blurVignette = 0.0F;
			def.depthOfFieldFocalLength = 0.0F;
			def.globalBlur = 0.0F;

			if (mode == ClientCameraMode::FirstPersonLocal && vrRig.poseSendTimer <= 0.0F) {
				vrRig.poseSendTimer = vrPoseInterval;
				SendVRPose();
			}
		}

		void Client::SendVRPose() {
			if (!net || !net->IsVRPresenceAvailable() || !world)
				return;
			auto p = world->GetLocalPlayer();
			if (!p || !p->IsAlive())
				return;
			IVRSystem* vr = GetVRSystem();
			if (!vr)
				return;
			const VRFrameState& fs = vr->GetFrameState();

			Vector3 eye = p->GetEye();
			VRNetPose pose;
			pose.headOffset = vrRig.camera - eye;
			pose.headRotation =
			  VRQuat::FromAxes(vrRig.headAxes[0], vrRig.headAxes[1], vrRig.headAxes[2]);
			for (int h = 0; h < 2; h++) {
				if (!vrRig.handValid[h])
					continue;
				pose.flags |= h == VRHandLeft ? VRPoseLeftHand : VRPoseRightHand;
				pose.handOffset[h] = vrRig.handPos[h] - eye;
				pose.handRotation[h] = VRQuat::FromAxes(vrRig.handAxes[h][0], vrRig.handAxes[h][1],
				                                        vrRig.handAxes[h][2]);
			}
			if (VRToolHand() == VRHandLeft)
				pose.flags |= VRPoseToolInLeftHand;
			// The body, so a server that allows it can follow room-scale walking
			// faster than PositionData's once a second.
			pose.flags |= VRPoseBody;
			pose.body = p->GetPosition();
			if (fs.hands[VRHandRight].trigger > 0.5F)
				pose.buttons |= VRButtonRightTrigger;
			if (fs.hands[VRHandLeft].trigger > 0.5F)
				pose.buttons |= VRButtonLeftTrigger;
			if (fs.hands[VRHandRight].squeeze > 0.5F)
				pose.buttons |= VRButtonRightGrip;
			if (fs.hands[VRHandLeft].squeeze > 0.5F)
				pose.buttons |= VRButtonLeftGrip;
			net->SendVRPose(pose);
		}

		void Client::VRPulse(int hand, float amplitude, float seconds, VRHapticShape shape) {
			IVRSystem* vr = GetVRSystem();
			if (!vrRig.active || !vr)
				return;
			if (hand < 0) {
				vr->Vibrate(VRHandLeft, shape, amplitude, seconds);
				vr->Vibrate(VRHandRight, shape, amplitude, seconds);
			} else {
				vr->Vibrate(hand, shape, amplitude, seconds);
			}
		}

		void Client::VRPoseReceived(int playerId, const VRNetPose& pose) {
			RemoteVRPose& r = vrRemotePoses[playerId];
			r.pose = pose;
			r.receivedTime = time;
		}

		const VRNetPose* Client::GetRemoteVRPose(int playerId) const {
			auto it = vrRemotePoses.find(playerId);
			if (it == vrRemotePoses.end())
				return nullptr;
			// A player who took the headset off, or stopped sending, goes back to
			// the ordinary animation after a second.
			if (time - it->second.receivedTime > 1.0F || time < it->second.receivedTime)
				return nullptr;
			return &it->second.pose;
		}

		void Client::VRPresenceConfigured(uint8_t flags, uint8_t maxRate) {
			vrServerFlags = flags;
			float rate = Clamp(static_cast<float>(maxRate), 5.0F, 60.0F);
			vrPoseInterval = 1.0F / rate;
			SPLog("[VR] Server relays VR poses at up to %.0fHz%s", rate,
			      (flags & VRConfigBodySync) ? ", with room-scale body sync" : "");
		}
	} // namespace client
} // namespace spades
