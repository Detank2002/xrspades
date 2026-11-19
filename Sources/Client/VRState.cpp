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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "VRState.h"

namespace spades {
	namespace client {

		namespace {
			IVRSystem* g_vrSystem = nullptr;
		}

		IVRSystem* GetVRSystem() { return g_vrSystem; }

		const char* VRClientTag() {
			if (!g_vrSystem)
				return "";
#if defined(__ANDROID__)
			return "QuestVR";
#else
			return "PCVR";
#endif
		}
		void SetVRSystem(IVRSystem* s) { g_vrSystem = s; }

		VRQuat VRQuat::Normalized() const {
			float len = std::sqrt(x * x + y * y + z * z + w * w);
			if (!(len > 1.0E-8F))
				return VRQuat();
			float inv = 1.0F / len;
			return VRQuat(x * inv, y * inv, z * inv, w * inv);
		}

		Vector3 VRQuat::Rotate(const Vector3& v) const {
			// v' = v + 2w (u x v) + 2 u x (u x v), u = (x, y, z)
			Vector3 u{x, y, z};
			Vector3 t = Vector3::Cross(u, v) * 2.0F;
			return v + t * w + Vector3::Cross(u, t);
		}

		VRQuat VRQuat::FromAxes(const Vector3& ax, const Vector3& ay, const Vector3& az) {
			// Columns of the rotation matrix: m[row][col].
			float m00 = ax.x, m10 = ax.y, m20 = ax.z;
			float m01 = ay.x, m11 = ay.y, m21 = ay.z;
			float m02 = az.x, m12 = az.y, m22 = az.z;

			VRQuat q;
			float trace = m00 + m11 + m22;
			if (trace > 0.0F) {
				float s = std::sqrt(trace + 1.0F) * 2.0F;
				q.w = 0.25F * s;
				q.x = (m21 - m12) / s;
				q.y = (m02 - m20) / s;
				q.z = (m10 - m01) / s;
			} else if (m00 > m11 && m00 > m22) {
				float s = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
				q.w = (m21 - m12) / s;
				q.x = 0.25F * s;
				q.y = (m01 + m10) / s;
				q.z = (m02 + m20) / s;
			} else if (m11 > m22) {
				float s = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
				q.w = (m02 - m20) / s;
				q.x = (m01 + m10) / s;
				q.y = 0.25F * s;
				q.z = (m12 + m21) / s;
			} else {
				float s = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
				q.w = (m10 - m01) / s;
				q.x = (m02 + m20) / s;
				q.y = (m12 + m21) / s;
				q.z = 0.25F * s;
			}
			return q.Normalized();
		}

		bool VRRayHitsHudPanel(const VRFrameState& fs, int hand, float panelAspect, Vector2& uv,
		                       float& distance) {
			return VRRayHitsPanel(fs, hand, fs.hudPanel, fs.hudWidth, panelAspect, uv, distance);
		}

		VRPose VRPoseRelative(const VRPose& a, const VRPose& b) {
			VRPose r;
			const VRQuat inv = a.orientation.Conjugate();
			r.position = inv.Rotate(b.position - a.position);
			r.orientation = inv * b.orientation;
			r.valid = a.valid && b.valid;
			return r;
		}

		VRPose VRPoseCompose(const VRPose& a, const VRPose& rel) {
			VRPose r;
			r.position = a.position + a.orientation.Rotate(rel.position);
			r.orientation = a.orientation * rel.orientation;
			r.valid = a.valid && rel.valid;
			return r;
		}

		VRPose VRHeadYawFrame(const VRPose& head) {
			VRPose r;
			r.position = head.position;
			r.valid = head.valid;
			const Vector3 f = -head.Axis(2);
			// About +Y by `yaw` takes -Z to (-sin yaw, 0, -cos yaw)
			const float yaw = (std::fabs(f.x) + std::fabs(f.z) > 1.0E-5F)
			                    ? std::atan2(-f.x, -f.z)
			                    : 0.0F;
			r.orientation = VRQuat::FromAxisAngle(MakeVector3(0.0F, 1.0F, 0.0F), yaw);
			return r;
		}

		namespace {
			Vector3 TurnGame(const Vector3& g, float yaw) {
				const float c = std::cos(yaw), s = std::sin(yaw);
				return MakeVector3(g.x * c - g.y * s, g.x * s + g.y * c, g.z);
			}
		} // namespace

		VRBindings VRDefaultBindings() {
			VRBindings b;
			for (int r = 0; r < 2; r++)
				for (int i = 0; i < kVRInputCount; i++) {
					b.target[r][i].role = r;
					b.target[r][i].input = static_cast<VRInput>(i);
				}
			return b;
		}

		const char* VRInputName(VRInput i) {
			switch (i) {
				case VRInput::Trigger: return "trigger";
				case VRInput::Grip: return "grip";
				case VRInput::Primary: return "primary";
				case VRInput::Secondary: return "secondary";
				case VRInput::StickClick: return "stickclick";
				case VRInput::Menu: return "menu";
				default: return "";
			}
		}

		namespace {
			bool ParseBindEnd(const std::string& s, int& role, VRInput& input) {
				if (s == "none") {
					role = -1;
					return true;
				}
				const auto dot = s.find('.');
				if (dot == std::string::npos)
					return false;
				const std::string h = s.substr(0, dot), n = s.substr(dot + 1);
				if (h == "tool")
					role = VRRoleTool;
				else if (h == "off")
					role = VRRoleOff;
				else
					return false;
				for (int i = 0; i < kVRInputCount; i++)
					if (n == VRInputName(static_cast<VRInput>(i))) {
						input = static_cast<VRInput>(i);
						return true;
					}
				return false;
			}
			std::string TrimBind(const std::string& s) {
				const auto a = s.find_first_not_of(" \t");
				const auto b = s.find_last_not_of(" \t");
				return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
			}
			float GetBindInput(const VRControllerState& c, VRInput i) {
				switch (i) {
					case VRInput::Trigger: return c.trigger;
					case VRInput::Grip: return c.squeeze;
					case VRInput::Primary: return c.buttonPrimary ? 1.0F : 0.0F;
					case VRInput::Secondary: return c.buttonSecondary ? 1.0F : 0.0F;
					case VRInput::StickClick: return c.thumbstickClick ? 1.0F : 0.0F;
					case VRInput::Menu: return c.menu ? 1.0F : 0.0F;
					default: return 0.0F;
				}
			}
			void AddBindInput(VRControllerState& c, VRInput i, float v, bool analogSource) {
				// An analog source presses a button past half
				const bool pressed = analogSource ? v > 0.5F : v > 0.0F;
				switch (i) {
					case VRInput::Trigger: c.trigger = std::max(c.trigger, v); break;
					case VRInput::Grip: c.squeeze = std::max(c.squeeze, v); break;
					case VRInput::Primary: c.buttonPrimary = c.buttonPrimary || pressed; break;
					case VRInput::Secondary: c.buttonSecondary = c.buttonSecondary || pressed; break;
					case VRInput::StickClick:
						c.thumbstickClick = c.thumbstickClick || pressed;
						break;
					case VRInput::Menu: c.menu = c.menu || pressed; break;
					default: break;
				}
			}
		} // namespace

		VRBindings VRParseBindings(const std::string& text) {
			VRBindings b = VRDefaultBindings();
			std::size_t at = 0;
			while (at <= text.size()) {
				std::size_t comma = text.find(',', at);
				if (comma == std::string::npos)
					comma = text.size();
				const std::string entry = TrimBind(text.substr(at, comma - at));
				at = comma + 1;
				const auto eq = entry.find('=');
				if (eq == std::string::npos)
					continue;
				int fromRole = -1, toRole = -1;
				VRInput from = VRInput::Trigger, to = VRInput::Trigger;
				if (!ParseBindEnd(TrimBind(entry.substr(0, eq)), fromRole, from) || fromRole < 0 ||
				    !ParseBindEnd(TrimBind(entry.substr(eq + 1)), toRole, to))
					continue;
				VRBindTarget& t = b.target[fromRole][static_cast<int>(from)];
				t.role = toRole;
				t.input = to;
			}
			return b;
		}

		std::string VRFormatBindings(const VRBindings& b) {
			const VRBindings d = VRDefaultBindings();
			std::string out;
			for (int r = 0; r < 2; r++)
				for (int i = 0; i < kVRInputCount; i++) {
					const VRBindTarget& t = b.target[r][i];
					if (t == d.target[r][i])
						continue;
					if (!out.empty())
						out += ",";
					out += std::string(r == VRRoleTool ? "tool." : "off.") +
					       VRInputName(static_cast<VRInput>(i)) + "=";
					if (t.role < 0)
						out += "none";
					else
						out += std::string(t.role == VRRoleTool ? "tool." : "off.") +
						       VRInputName(t.input);
				}
			return out;
		}

		void VRApplyBindings(const VRControllerState in[2], VRControllerState out[2], int toolHand,
		                     const VRBindings& b, bool swapSticks) {
			const int handOf[2] = {toolHand, 1 - toolHand}; // by role
			for (int h = 0; h < 2; h++) {
				out[h] = in[h];
				out[h].trigger = out[h].squeeze = 0.0F;
				out[h].buttonPrimary = out[h].buttonSecondary = false;
				out[h].thumbstickClick = out[h].menu = false;
			}
			for (int r = 0; r < 2; r++)
				for (int i = 0; i < kVRInputCount; i++) {
					const VRBindTarget& t = b.target[r][i];
					if (t.role < 0)
						continue;
					const VRInput from = static_cast<VRInput>(i);
					const float v = GetBindInput(in[handOf[r]], from);
					const bool analog = from == VRInput::Trigger || from == VRInput::Grip;
					AddBindInput(out[handOf[t.role]], t.input, v, analog);
				}
			if (swapSticks)
				std::swap(out[0].thumbstick, out[1].thumbstick);
		}

		bool VRMagazineAtWell(const Vector3& gunHand, const Vector3& gunForward,
		                      const Vector3& offHand, float scale) {
			if (gunForward.GetLength() < 1.0E-4F || scale <= 0.0F)
				return false;
			const Vector3 f = gunForward.Normalize();
			const Vector3 rel = (offHand - gunHand) / scale; // metres
			// Along the gun: from just ahead of the grip to 30cm out
			const float along = Vector3::Dot(rel, f);
			if (along < -0.02F || along > 0.30F)
				return false;
			// Off the gun's line, and below it (z is down)
			const Vector3 off = rel - f * along;
			if (off.GetLength() > 0.17F)
				return false;
			return off.z > -0.03F; // under the gun, or level with it
		}

		Vector3 VRTableToWorld(const VRTablePose& t, const Vector3& anchor, const Vector3& p) {
			return t.centre + TurnGame(VRTrackingToGameAxes(p - anchor), t.yaw) * t.scale;
		}

		void VRTableHoldOne(VRTablePose& t, const Vector3& anchor, const Vector3& a,
		                    const Vector3& p) {
			t.centre = a - TurnGame(VRTrackingToGameAxes(p - anchor), t.yaw) * t.scale;
		}

		void VRTableHoldTwo(VRTablePose& t, const Vector3& anchor, const Vector3& a, const Vector3& b,
		                    const Vector3& pa, const Vector3& pb, float minScale, float maxScale) {
			// Across: the hands' line, in the game's ground plane, against the held
			// points'.
			const Vector3 d = VRTrackingToGameAxes(pb - pa);
			const Vector3 w = b - a;
			const float dl = std::sqrt(d.x * d.x + d.y * d.y);
			const float wl = std::sqrt(w.x * w.x + w.y * w.y);
			if (dl > 0.02F && wl > 1.0E-3F) {
				t.scale = std::max(minScale, std::min(maxScale, wl / dl));
				t.yaw = std::atan2(w.y, w.x) - std::atan2(d.y, d.x);
			}
			// The middle of the held points under the middle of the hands
			VRTableHoldOne(t, anchor, (a + b) * 0.5F, (pa + pb) * 0.5F);
		}

		VRPose VRDefaultKeyboardPlace() {
			VRPose r;
			r.valid = true;
			// 42cm ahead, 40cm under the eyes: where hands rest typing
			r.position = MakeVector3(0.0F, -0.40F, -0.42F);
			// Its face turned up 40 degrees from facing you: the far edge back
			r.orientation =
			  VRQuat::FromAxisAngle(MakeVector3(1.0F, 0.0F, 0.0F), -40.0F * 3.14159265F / 180.0F);
			return r;
		}

		bool VRRayHitsPanel(const VRFrameState& fs, int hand, const VRPose& panel, float width,
		                    float aspect, Vector2& uv, float& distance) {
			const VRControllerState& c = fs.hands[hand];
			if (!fs.head.valid || !c.aim.valid)
				return false;
			const float panelAspect = aspect;

			// Into the panel's own space (it faces +Z, at z = 0), wherever it is:
			// before the head, or standing in the room.
			VRQuat toPanel = panel.orientation.Conjugate();
			Vector3 origin = toPanel.Rotate(c.aim.position - panel.position);
			Vector3 dir = toPanel.Rotate(-c.aim.Axis(2)); // OpenXR aims down -Z

			if (std::fabs(dir.z) < 1.0E-5F)
				return false;
			float t = -origin.z / dir.z;
			if (t <= 0.0F)
				return false;
			Vector3 p = origin + dir * t;

			float w = width, h = width * panelAspect;
			uv = MakeVector2(p.x / w + 0.5F, 0.5F - p.y / h);
			distance = t;
			return uv.x >= 0.0F && uv.x <= 1.0F && uv.y >= 0.0F && uv.y <= 1.0F;
		}

		bool VRPlaceWristPanel(const VRFrameState& fs, int hand, bool wasShown, VRPose& panel,
		                       bool inUse) {
			const VRControllerState& c = fs.hands[hand];
			if (!fs.head.valid || !c.active || !c.grip.valid)
				return false;

			// OpenXR's grip pose: +X is the palm's normal and points to the user's
			// right on both hands (out of the left palm, into the right one), so
			// the back of the hand is -X on the left and +X on the right. +Z runs
			// from the thumb towards the little finger, down the handle towards
			// the wrist.
			const Vector3 back = c.grip.Axis(0) * (hand == VRHandLeft ? -1.0F : 1.0F);
			const Vector3 pos = c.grip.position + c.grip.Axis(2) * 0.08F + back * 0.04F;

			Vector3 toEyes = fs.head.position - pos;
			const float dist = toEyes.GetLength();
			if (dist < 0.05F)
				return false;
			toEyes = toEyes * (1.0F / dist);

			// The wrist turned towards the eyes. Holding a gun two-handed puts the
			// off hand in view too, but with its back to the side, not the face.
			const float facing = Vector3::Dot(back, toEyes);
			// Within the view: the angle off the head's forward (-Z).
			const float inView = Vector3::Dot(fs.head.Axis(2), toEyes);
			// In use (the other hand at it), poking and pointing turn the wrist
			// about: it stays up much more loosely.
			const float minFacing = inUse ? 0.1F : wasShown ? 0.3F : 0.5F;
			const float minInView = inUse ? 0.34F : wasShown ? 0.57F : 0.77F; // 70, 55, 40 deg
			const float maxDist = inUse ? 1.0F : wasShown ? 0.85F : 0.7F;
			if (facing < minFacing || inView < minInView || dist > maxDist)
				return false;

			// Face the eyes, with the head's up as the panel's up so text is level
			// to them whatever the wrist's roll.
			Vector3 x = Vector3::Cross(fs.head.Axis(1), toEyes);
			if (x.GetLength() < 1.0E-3F)
				x = fs.head.Axis(0);
			x = x.Normalize();
			const Vector3 y = Vector3::Cross(toEyes, x);

			panel.position = pos;
			panel.orientation = VRQuat::FromAxes(x, y, toEyes);
			panel.valid = true;
			return true;
		}

		bool VRPlaceHeldMap(const VRFrameState& fs, int hand, VRPose& panel) {
			const VRControllerState& c = fs.hands[hand];
			if (!c.grip.valid || !fs.head.valid)
				return false;
			const float height = kVRHeldMapWidth * (float)kVRWristCanvasHeight /
			                     (float)kVRWristCanvasWidth;
			// Its centre half its height (and 2cm) above the hand, towards the eyes
			// by 3cm so the controller is not in front of it.
			const Vector3 up = fs.head.Axis(1);
			Vector3 pos = c.grip.position + up * (height * 0.5F + 0.02F);
			Vector3 toEyes = fs.head.position - pos;
			if (toEyes.GetLength() < 1.0E-3F)
				return false;
			toEyes = toEyes.Normalize();
			pos += toEyes * 0.03F;
			Vector3 x = Vector3::Cross(up, toEyes);
			if (x.GetLength() < 1.0E-3F)
				x = fs.head.Axis(0);
			x = x.Normalize();
			const Vector3 y = Vector3::Cross(toEyes, x);
			panel.position = pos;
			panel.orientation = VRQuat::FromAxes(x, y, toEyes);
			panel.valid = true;
			return true;
		}

		bool VRWristReached(const VRFrameState& fs, int hand, bool wasReached) {
			const VRControllerState& c = fs.hands[hand];
			if (!fs.wristShown || !c.grip.valid)
				return false;
			const float d = (c.grip.position - fs.wristPanel.position).GetLength();
			return d < (wasReached ? 0.40F : 0.22F);
		}

		bool VRRayHitsWristPanel(const VRFrameState& fs, int hand, Vector2& uv, float& distance) {
			const VRControllerState& c = fs.hands[hand];
			if (!fs.wristShown || !c.aim.valid)
				return false;
			// Into the panel's own frame: +X right, +Y up, +Z towards the eyes.
			const VRQuat toPanel = fs.wristPanel.orientation.Conjugate();
			const Vector3 origin = toPanel.Rotate(c.aim.position - fs.wristPanel.position);
			const Vector3 dir = toPanel.Rotate(-c.aim.Axis(2));
			// From the front only: the ray has to come at the panel's face.
			if (dir.z > -1.0E-4F || origin.z < -0.03F)
				return false;
			const float t = std::max(0.0F, -origin.z / dir.z);
			const Vector3 p = origin + dir * t;
			const float w = fs.wristWidth;
			const float h = w * static_cast<float>(kVRWristCanvasHeight) /
			                static_cast<float>(kVRWristCanvasWidth);
			uv = MakeVector2(p.x / w + 0.5F, 0.5F - p.y / h);
			distance = origin.z > 0.0F ? t : 0.0F;
			return uv.x >= 0.0F && uv.x <= 1.0F && uv.y >= 0.0F && uv.y <= 1.0F;
		}

		Vector3 VRAimFilter::Step(const Vector3& raw, float dt, float amount) {
			amount = std::min(1.0F, std::max(0.0F, amount));
			if (amount <= 0.0F || dt <= 0.0F) {
				active = false;
				return raw;
			}
			if (!active) {
				value = raw;
				speed = 0.0F;
				active = true;
				return value;
			}
			const float twoPi = 6.2831853F;
			const float dot = std::min(1.0F, std::max(-1.0F, Vector3::Dot(raw, value)));
			// The speed is itself smoothed at 1 Hz, the usual one-euro choice:
			// tracking shake flips direction every frame, and a faster estimate
			// reads that as motion and opens the filter to the very shake it is
			// there to remove.
			const float rate = std::acos(dot) / dt;
			speed += (rate - speed) * (1.0F - std::exp(-dt * twoPi * 1.0F));
			// 10 Hz down to 0.5 Hz for a still hand as `amount` goes 0 to 1; each
			// radian per second of turning adds 2 Hz.
			const float minCutoff = 10.0F + (0.5F - 10.0F) * amount;
			const float cutoff = minCutoff + 2.0F * speed;
			const float alpha = 1.0F - std::exp(-dt * twoPi * cutoff);
			value = (value + (raw - value) * alpha).Normalize();
			return value;
		}

		void VRRecoil::Kick(float pitch, float yaw) {
			const float shot[2] = {pitch, yaw};
			for (int i = 0; i < 2; i++) {
				kick[i] += shot[i] * kKickShare;
				climb[i] += shot[i] * (1.0F - kKickShare);
				climb[i] = std::max(-kClimbMax, std::min(kClimbMax, climb[i]));
			}
			sinceShot = 0.0F;
		}

		namespace {
			// One critically damped spring towards 0, stepped exactly: x and v
			// after dt for x'' = -2w x' - w^2 x. Exact, so a long frame cannot
			// make it ring or blow up.
			void StepSpring(float& x, float& v, float w, float dt) {
				const float e = std::exp(-w * dt);
				const float c = v + w * x;
				const float nx = (x + c * dt) * e;
				const float nv = (v - w * c * dt) * e;
				x = nx;
				v = nv;
			}
		} // namespace

		void VRRecoil::Step(float dt) {
			if (dt <= 0.0F)
				return;
			dt = std::min(dt, 0.25F);
			sinceShot += dt;
			const bool climbReturns = sinceShot >= kClimbHold;
			for (int i = 0; i < 2; i++) {
				StepSpring(kick[i], kickVel[i], kKickRate, dt);
				if (climbReturns)
					StepSpring(climb[i], climbVel[i], kClimbRate, dt);
			}
		}

		float VRVignetteTarget(float groundSpeed, float fallSpeed, float turnRate) {
			auto ramp = [](float v, float from, float to) {
				return std::max(0.0F, std::min(1.0F, (v - from) / (to - from)));
			};
			// Full at a walk (about 4 blocks/s), at a fall past a jump's own
			// speed, and at smooth turning's default 150 degrees a second.
			return std::max(ramp(groundSpeed, 0.5F, 4.0F),
			                std::max(ramp(fallSpeed, 4.0F, 12.0F),
			                         ramp(std::fabs(turnRate), 0.3F, 2.6F)));
		}

		float VRVignetteStep(float current, float target, float dt) {
			const float rate = target > current ? 1.0F / 0.15F : 1.0F / 0.5F;
			const float step = rate * std::max(0.0F, dt);
			if (target > current)
				return std::min(target, current + step);
			return std::max(target, current - step);
		}

		float VRVignetteClearAngle(float amount, float strength) {
			const float a = std::max(0.0F, std::min(1.0F, amount)) *
			                std::max(0.0F, std::min(1.0F, strength));
			const float deg = 60.0F - 40.0F * a;
			return deg * 3.14159265F / 180.0F;
		}

		VRHurtRumble VRHurtRumbleFor(int damage, float side, bool known) {
			// 10 damage is a light tap, 50 and over (a headshot, a big fall)
			// as hard as the motors go.
			const float d = std::max(0.0F, std::min(1.0F, (damage - 5) / 45.0F));
			VRHurtRumble r;
			r.seconds = 0.08F + 0.17F * d;
			const float amp = 0.45F + 0.55F * d;
			if (!known) {
				r.amplitude[VRHandLeft] = r.amplitude[VRHandRight] = amp;
				return r;
			}
			const float s = std::max(-1.0F, std::min(1.0F, side));
			// Straight ahead or behind: both alike. Fully to one side: the far
			// hand at a quarter.
			const float far = amp * (1.0F - 0.75F * std::fabs(s));
			r.amplitude[VRHandRight] = s >= 0.0F ? amp : far;
			r.amplitude[VRHandLeft] = s >= 0.0F ? far : amp;
			return r;
		}

		float VRHurtEdgeAmount(int damage, float age) {
			if (damage <= 0 || age < 0.0F)
				return 0.0F;
			const float d = std::max(0.0F, std::min(1.0F, damage / 50.0F));
			const float peak = 0.35F + 0.65F * d;
			const float fade = 0.5F + 0.5F * d;
			if (age >= fade)
				return 0.0F;
			const float t = 1.0F - age / fade;
			return peak * t * t;
		}

		float VRLowHealthEdge(int health, float time) {
			if (health <= 0 || health > 30)
				return 0.0F;
			const float low = 1.0F - (health - 1) / 30.0F; // ~0 at 30, 1 at 1
			// A heartbeat: two quick beats a second and a bit apart, faster when lower
			const float period = 1.2F - 0.4F * low;
			const float ph = std::fmod(std::max(0.0F, time), period) / period;
			auto beat = [](float x, float at) {
				const float u = (x - at) / 0.08F;
				return std::exp(-u * u);
			};
			const float pulse = std::max(beat(ph, 0.1F), 0.7F * beat(ph, 0.3F));
			return (0.12F + 0.28F * low) * (0.4F + 0.6F * pulse);
		}

		namespace {
			// The trace's fields, in order, as floats: a pose is position,
			// orientation, valid; a controller is active, grip, aim, velocity and
			// its valid, trigger, squeeze, stick, stick click, A/X, B/Y, menu.
			constexpr int kPoseFields = 8;
			// active, grip, aim, velocity + valid, trigger/squeeze/stick, 4 buttons
			constexpr int kHandFields = 1 + kPoseFields * 2 + 4 + 4 + 4;
			constexpr int kTraceFields = kPoseFields * 3 + kHandFields * 2;

			template <class F> void VisitPose(VRPose& p, F&& f) {
				f(p.position.x), f(p.position.y), f(p.position.z);
				f(p.orientation.x), f(p.orientation.y), f(p.orientation.z), f(p.orientation.w);
				float v = p.valid ? 1.0F : 0.0F;
				f(v);
				p.valid = v > 0.5F;
			}
			template <class F> void VisitFlag(bool& b, F&& f) {
				float v = b ? 1.0F : 0.0F;
				f(v);
				b = v > 0.5F;
			}
			template <class F> void VisitFrame(VRFrameState& fs, F&& f) {
				VisitPose(fs.head, f);
				VisitPose(fs.eyes[0], f);
				VisitPose(fs.eyes[1], f);
				for (auto& c : fs.hands) {
					VisitFlag(c.active, f);
					VisitPose(c.grip, f);
					VisitPose(c.aim, f);
					f(c.linearVelocity.x), f(c.linearVelocity.y), f(c.linearVelocity.z);
					VisitFlag(c.velocityValid, f);
					f(c.trigger), f(c.squeeze), f(c.thumbstick.x), f(c.thumbstick.y);
					VisitFlag(c.thumbstickClick, f);
					VisitFlag(c.buttonPrimary, f);
					VisitFlag(c.buttonSecondary, f);
					VisitFlag(c.menu, f);
				}
			}
		} // namespace

		std::string VRTraceFormat(const VRFrameState& fs) {
			VRFrameState copy = fs; // the visitor writes flags back
			std::string out;
			char buf[32];
			VisitFrame(copy, [&](float& v) {
				std::snprintf(buf, sizeof(buf), out.empty() ? "%.6g" : " %.6g", v);
				out += buf;
			});
			return out;
		}

		bool VRTraceParse(const std::string& line, VRFrameState& fs) {
			float values[kTraceFields];
			const char* p = line.c_str();
			for (int i = 0; i < kTraceFields; i++) {
				char* end = nullptr;
				values[i] = std::strtof(p, &end);
				if (end == p)
					return false;
				p = end;
			}
			int i = 0;
			VisitFrame(fs, [&](float& v) { v = values[i++]; });
			return true;
		}

		int VRHapticEnvelope(VRHapticShape shape, float amplitude, float seconds, float* out,
		                     int maxSamples) {
			if (maxSamples < 2 || !out)
				return 0;
			const float a = std::max(0.0F, std::min(1.0F, amplitude));
			const int n = std::max(2, std::min(maxSamples, (int)std::lround(seconds * 500.0F)));
			for (int i = 0; i < n; i++) {
				const float t = (float)i / (float)(n - 1); // 0 to 1 over the rumble
				float v = 1.0F;
				switch (shape) {
					case VRHapticShape::Flat: v = 1.0F; break;
					// Down to 5% by the end.
					case VRHapticShape::Kick: v = std::exp(-3.0F * t); break;
					// On for the first and last 30%, nothing between.
					case VRHapticShape::DoubleTap: v = (t < 0.3F || t > 0.7F) ? 1.0F : 0.0F; break;
				}
				out[i] = a * v;
			}
			return n;
		}

		float VRRecoilHoldScale(bool braced, bool walking, bool sightsUp) {
			return braced && walking && !sightsUp ? 0.5F : 1.0F;
		}

		bool VRStockUpdate(VRStockState& s, bool allowed, float distance, float angle, float dt) {
			const float deg = 0.0174532925F;
			const bool was = s.on;
			if (!allowed)
				s.on = false;
			else if (was)
				s.on = distance < 0.22F && angle < 18.0F * deg; // more room to leave
			else
				s.on = distance < 0.15F && angle < 12.0F * deg;
			const float target = s.on ? 1.0F : 0.0F;
			const float step = std::max(0.0F, dt) / 0.2F;
			s.blend = s.blend < target ? std::min(target, s.blend + step)
			                           : std::max(target, s.blend - step);
			return s.on && !was;
		}

		int VRGripUpdate(VRGripState& s, bool grip, int slot) {
			const bool pressed = grip && !s.prev;
			s.prev = grip;
			int take = VRHolsterNone;
			if (pressed) {
				if (slot >= 0) {
					take = slot;
					s.fromHolster = true;
				} else {
					s.action = true;
				}
			}
			if (!grip)
				s.fromHolster = s.action = false;
			return take;
		}

		int VRHolsterAt(const VRFrameState& fs, int hand, int hover) {
			const VRControllerState& c = fs.hands[hand];
			if (!fs.head.valid || !c.grip.valid)
				return VRHolsterNone;
			// Tracking space: +X right, +Y up, -Z forward.
			Vector3 f = fs.head.Axis(2) * -1.0F;
			f.y = 0.0F;
			if (f.GetLength() < 0.1F)
				return VRHolsterNone; // looking straight up or down: no heading
			f = f.Normalize();
			const Vector3 right = MakeVector3(-f.z, 0.0F, f.x);
			const Vector3 up = MakeVector3(0.0F, 1.0F, 0.0F);
			const Vector3 h = fs.head.position;
			const float side = hand == VRHandRight ? 1.0F : -1.0F; // the hand's own side

			// Metres from the eyes. Seated, the hips are beside the seat.
			struct Slot {
				Vector3 at;
				float radius;
			};
			const Slot slots[4] = {
			  {h + right * (0.20F * side) - f * 0.15F - up * 0.10F, 0.18F}, // gun: own shoulder
			  {h - right * (0.20F * side) - f * 0.15F - up * 0.10F, 0.18F}, // spade: other shoulder
			  {h + right * (0.22F * side) - up * 0.60F, 0.20F},             // blocks: own hip
			  {h - right * (0.22F * side) - up * 0.60F, 0.20F},             // grenades: other hip
			};
			int best = VRHolsterNone;
			float bestDistance = 1.0E9F;
			for (int i = 0; i < 4; i++) {
				float d = (c.grip.position - slots[i].at).GetLength();
				float radius = slots[i].radius * (i == hover ? 1.25F : 1.0F);
				if (d < radius && d < bestDistance) {
					best = i;
					bestDistance = d;
				}
			}
			return best;
		}

		VRQuat VRQuat::FromAxisAngle(const Vector3& axis, float angle) {
			float s = std::sin(angle * 0.5F);
			return VRQuat(axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5F));
		}

		int VRWalkDither(float angle, float dt, float& lean, float& hold, bool& second) {
			// The eighth below the angle and the one above, and how much of the
			// second: not the fraction of the 45 degrees, since the mix of two unit
			// steps a quarter of the way along points less than a quarter round.
			// The server walks a diagonal at the same speed as a straight line.
			const float eighth = 0.78539816F;
			float t = angle / eighth;
			int first = static_cast<int>(std::floor(t));
			const float tanFrac = std::tan((t - static_cast<float>(first)) * eighth);
			const float s = std::sin(eighth), c = std::cos(eighth);
			const float w = Clamp(tanFrac / (s + tanFrac * (1.0F - c)), 0.0F, 1.0F);
			first = ((first % 8) + 8) % 8;

			// Each choice held 1/20s: every frame would be an input packet a frame.
			hold -= dt;
			if (hold <= 0.0F) {
				hold = std::max(hold + 0.05F, 0.0F);
				lean += w;
				second = lean >= 1.0F;
				if (second)
					lean -= 1.0F;
			}
			return second ? (first + 1) % 8 : first;
		}
	} // namespace client
} // namespace spades
