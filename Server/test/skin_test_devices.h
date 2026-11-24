// Headless devices for mod-overlay integration tests. No world scene or GPU.

#pragma once
#include <stdexcept>

#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Bitmap.h>
#include <Core/ResourceChanges.h>
using namespace spades;
using namespace spades::client;
class TestRenderer : public IRenderer {
public:
	ResourceChanges lastChanges;
	int reloads = 0;
	bool failNextReload = false;
	void ReloadResources(const ResourceChanges& c) override {
		if (failNextReload) {
			failNextReload = false;
			throw std::runtime_error("renderer failure");
		}
		lastChanges = c;
		++reloads;
	}
	void Init() override {}
	void Shutdown() override {}
	Handle<IImage> RegisterImage(const char* filename) override { return {}; }
	Handle<IModel> RegisterModel(const char* filename) override { return {}; }
	Handle<IImage> CreateImage(Bitmap&) override { return {}; }
	Handle<IModel> CreateModel(VoxelModel&) override { return {}; }
	void SetGameMap(stmp::optional<GameMap&>) override {}
	void SetFogDistance(float) override {}
	void SetFogColor(Vector3) override {}
	void StartScene(const SceneDefinition&) override {}
	void AddLight(const client::DynamicLightParam& light) override {}
	void RenderModel(IModel&, const ModelRenderParam&) override {}
	void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) override {}
	void AddSprite(IImage&, Vector3 center, float radius, float rotation) override {}
	void AddLongSprite(IImage&, Vector3 p1, Vector3 p2, float radius) override {}
	void AddImagePlane(IImage&, Vector3 origin, Vector3 axisU, Vector3 axisV) override {}
	void AddGlare(IImage&, const GlareParam&) override {}
	void EndScene() override {}
	void MultiplyScreenColor(Vector3) override {}
	void SetColor(Vector4) override {}
	void SetColorAlphaPremultiplied(Vector4) override {}
	void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft) override {}
	void DrawImage(stmp::optional<IImage&>, const AABB2& outRect) override {}
	void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft,
	               const AABB2& inRect) override {}
	void DrawImage(stmp::optional<IImage&>, const AABB2& outRect, const AABB2& inRect) override {}
	void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft, const Vector2& outTopRight,
	               const Vector2& outBottomLeft, const AABB2& inRect) override {}
	void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) override {}
	void DrawFlatGameMap(const Vector2& outTopLeft, const Vector2& outTopRight,
	                     const Vector2& outBottomLeft, const AABB2& inRect) override {}
	void FrameDone() override {}
	void Flip() override {}
	Handle<Bitmap> ReadBitmap() override { return {}; }
	float ScreenWidth() override { return 1568; }
	float ScreenHeight() override { return 882; }
	float ScreenPixelRatio() override { return 1; }
	void DrawFilledRectFade(float x0, float y0, float x1, float y1, Vector4 color0, Vector4 color1,
	                        bool horizontal = false) override {}
	void DrawFilledTriangle(const Vector2& v0, const Vector2& v1, const Vector2& v2) override {}
	void DrawShadedTriangle(const Vector2& v0, const Vector2& v1, const Vector2& v2,
	                        const Vector4& c0, const Vector4& c1, const Vector4& c2) override {}
};
class TestAudio : public IAudioDevice {
public:
	int reloads = 0;
	void ReloadResources(const ResourceChanges&) override { ++reloads; }
	IAudioChunk* RegisterSound(const char*) override { return nullptr; }
	void SetGameMap(GameMap*) override {}
	void Play(IAudioChunk*, const Vector3&, const AudioParam&) override {}
	void PlayLocal(IAudioChunk*, const Vector3&, const AudioParam&) override {}
	void PlayLocal(IAudioChunk*, const AudioParam&) override {}
	void Respatialize(const Vector3&, const Vector3&, const Vector3&) override {}
};