/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.
 Distributed under the GNU General Public License, version 3 or later.
 See <http://www.gnu.org/licenses/>.
 */

#pragma once

namespace spades {
	namespace gui {
		namespace {
			// Deliberately NOT the game's bindings: these are local script objects.
			// Registering a model records its name only; AddModel records a draw.
			// Unused reload/ADS models, sounds and images are never loaded. This
			// compatibility surface cannot reach the game or write its settings.
			constexpr const char kSkinPreviewScriptApi[] = R"AS(
namespace spades {
    interface IToolSkin {}
    interface IViewToolSkin {}
    interface IViewToolSkin2 {}
    interface IThirdPersonToolSkin {}
    interface IWeaponSkin {}
    interface IWeaponSkin2 {}
    interface IWeaponSkin3 {}
    interface IWeaponSkin4 {}
    interface ISpadeSkin {}
    interface IGrenadeSkin {}
    enum SpadeActionType { Idle, Bash, Dig, DigStart }
    enum DynamicLightType { Point, Spotlight, Linear }

    class ConfigItem {
        private string value;
        private string fallback;
        ConfigItem(const string &in name) { value = __PreviewConfig(name, ""); }
        ConfigItem(const string &in name, const string &in def) {
            fallback = def;
            value = __PreviewConfig(name, def);
        }
        float FloatValue { get { return float(ParseDouble(value)); } set { this.value = "" + value; } }
        int IntValue { get { return ParseInt(value); } set { this.value = "" + value; } }
        bool BoolValue { get { return ParseInt(value) != 0; } set { this.value = value ? "1" : "0"; } }
        string StringValue { get { return value; } set { this.value = value; } }
        string DefaultValue { get { return fallback; } }
        bool IsUnknown { get { return false; } }
        ConfigItem@ opAssign(float v) { value = "" + v; return this; }
        ConfigItem@ opAssign(int v) { value = "" + v; return this; }
        ConfigItem@ opAssign(const string &in v) { value = v; return this; }
    }

    class Model {
        string path;
        VoxelModel@ voxels;
        Model(const string &in p) { path = p; }
        Model(VoxelModel@ v) { @voxels = v; }
    }
    class ModelRenderParam {
        Matrix4 matrix;
        Vector3 customColor;
        bool depthHack = false;
        bool castShadow = true;
        bool drawOutline = true;
        bool ghost = false;
        float opacity = 1;
    }
    class Image {
        float Width = 256;
        float Height = 256;
    }
    class DynamicLightParam {
        DynamicLightType type;
        Vector3 origin, point2, color, spotAxisX, spotAxisY, spotAxisZ;
        float radius, spotAngle;
        bool useLensFlare, alwaysLit;
        Image@ image;
    }
    class AudioChunk {}
    class AudioParam {
        float volume = 1;
        float pitch = 1;
        float referenceDistance = 1;
    }
    class AudioDevice {
        AudioChunk@ RegisterSound(const string &in name) { return AudioChunk(); }
        void Play(AudioChunk@ sound, const Vector3 &in pos, const AudioParam &in p) {}
        void PlayLocal(AudioChunk@ sound, const Vector3 &in pos, const AudioParam &in p) {}
        void PlayLocal(AudioChunk@ sound, const AudioParam &in p) {}
    }
    class Renderer {
        private Vector4 color;
        float ScreenWidth { get { return 1024; } }
        float ScreenHeight { get { return 768; } }
        bool IsSoftwareRenderer { get { return false; } }
        Vector4 Color { set { color = value; } }
        Vector4 ColorNP { set { color = value; } }
        Vector4 ColorP { set { color = value; } }
        Model@ RegisterModel(const string &in path) { return Model(path); }
        Model@ CreateModel(VoxelModel@ voxels) { return Model(voxels); }
        Image@ RegisterImage(const string &in path) { return Image(); }
        void AddModel(Model@ model, const ModelRenderParam &in p) {
            if (model is null) return;
            __PreviewModel(model.path, model.voxels, p.matrix, p.customColor,
                           p.ghost ? p.opacity : 1);
        }
        void AddLight(const DynamicLightParam &in p) {}
        void AddLongSprite(Image@ image, const Vector3 &in a, const Vector3 &in b, float radius) {
            __PreviewBeam(a, b, radius, color);
        }
        void AddSprite(Image@ image, const Vector3 &in pos, float radius, float rotation) {}
        void AddImagePlane(Image@ image, const Vector3 &in pos, const Vector3 &in u,
                           const Vector3 &in v) {}
        void DrawImage(Image@ image, const Vector2 &in pos) {}
        void DrawImage(Image@ image, const AABB2 &in rect) {}
        void DrawImage(Image@ image, const Vector2 &in pos, const AABB2 &in source) {}
        void DrawImage(Image@ image, const AABB2 &in rect, const AABB2 &in source) {}
        void DrawImage(Image@ image, const Vector2 &in pos, const Vector2 &in u,
                       const Vector2 &in v, const AABB2 &in source) {}
        void DrawFilledRect(float x1, float y1, float x2, float y2) {}
        void DrawOutlinedRect(float x1, float y1, float x2, float y2) {}
        void DrawLine(const Vector2 &in a, const Vector2 &in b, float width) {}
        void DrawFilledTriangle(const Vector2 &in a, const Vector2 &in b, const Vector2 &in c) {}
    }
}
)AS";
		} // namespace
	} // namespace gui
} // namespace spades
