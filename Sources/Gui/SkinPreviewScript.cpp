/*
 Copyright (c) 2026 XRSpades contributors

 This file is part of ZeroSpades, a fork of OpenSpades.
 Distributed under the GNU General Public License, version 3 or later.
 See <http://www.gnu.org/licenses/>.
 */

#include "SkinPreviewScript.h"
#include "SkinPreviewScriptApi.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>

#include <Core/Exception.h>
#include <Core/MemoryStream.h>
#include <ScriptBindings/MathScript.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	namespace gui {
		namespace {
			constexpr std::size_t kMaxScriptBytes = 512 * 1024;
			constexpr std::size_t kMaxModelBytes = 34 * 1024 * 1024;
			constexpr asPWORD kPreviewState = 0x58525056;

			void Check(int result) {
				if (result < 0)
					SPRaise("Preview script API error: %d", result);
			}

			std::string Lower(std::string text) {
				for (auto& c : text)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return text;
			}

			std::string Path(std::string path, const std::string& from = {}) {
				if (path.empty() || path.size() > 512 || path.find(':') != std::string::npos ||
				    path.find('\0') != std::string::npos || path.find('*') != std::string::npos)
					SPRaise("Invalid preview resource path");
				std::replace(path.begin(), path.end(), '\\', '/');
				if (!from.empty() && path.front() != '/')
					path = from.substr(0, from.find_last_of('/') + 1) + path;
				std::vector<std::string> parts;
				for (std::size_t at = 0; at < path.size();) {
					const auto end = path.find('/', at);
					auto part = path.substr(at, end == std::string::npos ? end : end - at);
					at = end == std::string::npos ? path.size() : end + 1;
					if (part.empty() || part == ".")
						continue;
					if (part == "..") {
						if (parts.empty())
							SPRaise("Preview path escapes the resource root");
						parts.pop_back();
					} else {
						parts.push_back(std::move(part));
					}
				}
				std::string result;
				for (const auto& part : parts) {
					if (!result.empty())
						result += '/';
					result += part;
				}
				return result;
			}

			bool Finite(Vector3 v) {
				return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
			}

			struct Capture {
				const SkinPreviewReader& reader;
				const SkinPreviewOptions& options;
				SkinPreviewScene scene;
				std::string error;
				std::set<std::string> sections;
				std::size_t sourceBytes = 0, voxelCells = 0;
				std::size_t instructions = 1000000;
				std::mt19937 random{0x58525056};
				std::chrono::steady_clock::time_point deadline;

				Capture(const SkinPreviewReader& reader, const SkinPreviewOptions& options)
				    : reader(reader), options(options) {}

				void AddSection(CScriptBuilder& builder, const std::string& path) {
					if (!sections.insert(Lower(path)).second)
						return;
					if (sections.size() > 32)
						SPRaise("Too many preview script includes");
					if (Lower(path).compare(0, 8, "scripts/") != 0)
						SPRaise("Preview includes must be scripts");
					auto bytes = reader(path, kMaxScriptBytes);
					if (!bytes)
						SPRaise("Missing preview script: %s", path.c_str());
					sourceBytes += bytes->size();
					if (bytes->size() > kMaxScriptBytes || sourceBytes > 2 * 1024 * 1024)
						SPRaise("Preview scripts are too large");
					Check(builder.AddSectionFromMemory(path.c_str(), bytes->data(),
					                                   static_cast<unsigned>(bytes->size())));
				}
			};

			Capture& Current() {
				return *static_cast<Capture*>(
				  asGetActiveContext()->GetEngine()->GetUserData(kPreviewState));
			}

			void Message(const asSMessageInfo* message, void* data) {
				auto& capture = *static_cast<Capture*>(data);
				if (message->type == asMSGTYPE_ERROR && capture.error.empty())
					capture.error = std::string(message->section) + ":" +
					                std::to_string(message->row) + ": " + message->message;
			}

			void Budget(asIScriptContext* context, void* data) {
				auto& capture = *static_cast<Capture*>(data);
				if (!capture.instructions || std::chrono::steady_clock::now() > capture.deadline)
					context->Abort();
				else
					--capture.instructions;
			}

			asIScriptContext* RequestContext(asIScriptEngine* engine, void* data) {
				auto* context = engine->CreateContext();
				context->SetLineCallback(asFUNCTION(Budget), data, asCALL_CDECL);
				return context;
			}

			void ReturnContext(asIScriptEngine*, asIScriptContext* context, void*) {
				context->Release();
			}

			int Include(const char* include, const char* from, CScriptBuilder* builder,
			            void* data) {
				try {
					static_cast<Capture*>(data)->AddSection(*builder, Path(include, from));
					return 0;
				} catch (const std::exception& ex) {
					static_cast<Capture*>(data)->error = ex.what();
					return -1;
				}
			}

			std::string Config(const std::string& name, const std::string& fallback) {
				const auto& values = Current().options.settings;
				const auto it = values.find(name);
				if (it != values.end())
					return it->second;
				if (!fallback.empty())
					return fallback;
				if (name == "cg_viewWeaponSide")
					return "1";
				if (name == "cg_fov")
					return "90";
				return "0";
			}

			void RecordModel(const std::string& path, VoxelModel* model, const Matrix4& matrix,
			                 const Vector3& color, float opacity) {
				try {
					auto& scene = Current().scene;
					if (scene.parts.size() == 128)
						SPRaise("Too many preview model draws");
					if (!Finite(color) || !std::isfinite(opacity))
						SPRaise("Invalid preview model color");
					SkinPreviewPart part;
					part.path = path.empty() ? "" : Path(path);
					const auto lower = Lower(part.path);
					if (!model && (lower.compare(0, 7, "models/") != 0 || lower.size() < 4 ||
					               lower.compare(lower.size() - 4, 4, ".kv6") != 0))
						SPRaise("Preview draws must reference KV6 models");
					part.model = Handle<VoxelModel>(model);
					part.matrix = matrix;
					part.customColor = color;
					part.opacity = Clamp(opacity, 0.0F, 1.0F);
					scene.parts.push_back(std::move(part));
				} catch (const std::exception& ex) {
					asGetActiveContext()->SetException(ex.what());
				}
			}

			void RecordBeam(const Vector3& start, const Vector3& end, float radius,
			                const Vector4& color) {
				try {
					auto& beams = Current().scene.beams;
					if (beams.size() == 64)
						SPRaise("Too many preview effects");
					if (!Finite(start) || !Finite(end) || !Finite(color.GetXYZ()) ||
					    !std::isfinite(radius))
						SPRaise("Invalid preview effect");
					// Zero-length sprites are scope dots, not part of a weapon's silhouette.
					if ((end - start).GetSquaredLength() > 0.00000001F && radius > 0)
						beams.push_back({start, end, radius, color.GetXYZ()});
				} catch (const std::exception& ex) {
					asGetActiveContext()->SetException(ex.what());
				}
			}

			VoxelModel* NewVoxels(int w, int h, int d) {
				try {
					if (w < 1 || h < 1 || d < 1 || w > 512 || h > 512 || d > 64)
						SPRaise("Invalid preview voxel dimensions");
					auto& cells = Current().voxelCells;
					cells += std::size_t(w) * h * d;
					if (cells > 4 * 1024 * 1024)
						SPRaise("Too many procedural preview voxels");
					return new VoxelModel(w, h, d);
				} catch (const std::exception& ex) {
					asGetActiveContext()->SetException(ex.what());
					return nullptr;
				}
			}

			void SetVoxel(int x, int y, int z, std::uint32_t color, VoxelModel* model) {
				try {
					model->SetSolid(x, y, z, color);
				} catch (const std::exception& ex) {
					asGetActiveContext()->SetException(ex.what());
				}
			}

			void RegisterAPI(asIScriptEngine* engine, Capture& capture) {
				RegisterStdString(engine);
				RegisterScriptArray(engine, true);
				RegisterStdStringUtils(engine);
				RegisterScriptMath(engine);
				RegisterMathScriptBindings(engine, &capture.random);
				Check(engine->SetDefaultNamespace("spades"));
				Check(engine->RegisterObjectType("VoxelModel", 0, asOBJ_REF));
				Check(engine->RegisterObjectBehaviour("VoxelModel", asBEHAVE_ADDREF, "void f()",
				                                      asMETHOD(VoxelModel, AddRef),
				                                      asCALL_THISCALL));
				Check(engine->RegisterObjectBehaviour("VoxelModel", asBEHAVE_RELEASE, "void f()",
				                                      asMETHOD(VoxelModel, Release),
				                                      asCALL_THISCALL));
				Check(engine->RegisterObjectBehaviour("VoxelModel", asBEHAVE_FACTORY,
				                                      "VoxelModel@ f(int, int, int)",
				                                      asFUNCTION(NewVoxels), asCALL_CDECL));
				Check(engine->RegisterObjectMethod("VoxelModel",
				                                   "void SetSolid(int, int, int, uint)",
				                                   asFUNCTION(SetVoxel), asCALL_CDECL_OBJLAST));
				Check(engine->RegisterGlobalFunction(
				  "string __PreviewConfig(const string& in, const string& in)", asFUNCTION(Config),
				  asCALL_CDECL));
				Check(engine->RegisterGlobalFunction(
				  "void __PreviewModel(const string& in, VoxelModel@+, const Matrix4& in, const "
				  "Vector3& in, float)",
				  asFUNCTION(RecordModel), asCALL_CDECL));
				Check(engine->RegisterGlobalFunction("void __PreviewBeam(const Vector3& in, const "
				                                     "Vector3& in, float, const Vector4& in)",
				                                     asFUNCTION(RecordBeam), asCALL_CDECL));
				Check(engine->SetDefaultNamespace(""));
			}

			template <class T> struct Release {
				void operator()(T* object) const {
					if (object)
						object->Release();
				}
			};
			struct ReleaseEngine {
				void operator()(asIScriptEngine* engine) const { engine->ShutDownAndRelease(); }
			};

			void Execute(asIScriptContext& context) {
				if (context.Execute() != asEXECUTION_FINISHED) {
					const auto* message = context.GetExceptionString();
					SPRaise("Preview script failed: %s",
					        message && *message ? message : "execution limit");
				}
			}
		} // namespace

		Handle<VoxelModel> LoadSkinPreviewModel(const std::string& bytes) {
			if (bytes.size() < 32 || bytes.size() > kMaxModelBytes ||
			    bytes.compare(0, 4, "Kvxl") != 0)
				SPRaise("Invalid preview model header");
			auto u32 = [&bytes](std::size_t at) {
				const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + at);
				return std::uint64_t(p[0]) | (std::uint64_t(p[1]) << 8) |
				       (std::uint64_t(p[2]) << 16) | (std::uint64_t(p[3]) << 24);
			};
			const auto w = u32(4), h = u32(8), d = u32(12), blocks = u32(28);
			if (!w || !h || !d || w > 512 || h > 512 || d > 64 || w * h * d > 4 * 1024 * 1024 ||
			    blocks > w * h * d || 32 + blocks * 8 + w * 4 + w * h * 2 > bytes.size())
				SPRaise("Preview model is too large or truncated");
			MemoryStream stream(bytes.data(), bytes.size());
			return VoxelModel::LoadKV6(stream);
		}

		SkinPreviewScene CaptureSkinPreview(SkinType type, const SkinPreviewReader& reader,
		                                    const SkinPreviewOptions& options, bool thirdPerson) {
			if (!IsWeaponSkin(type))
				SPRaise("This category has no voxel preview");
			Capture capture{reader, options};
			std::unique_ptr<asIScriptEngine, ReleaseEngine> engine(asCreateScriptEngine());
			if (!engine)
				SPRaise("Cannot create preview script recorder");
			engine->SetUserData(&capture, kPreviewState);
			Check(engine->SetMessageCallback(asFUNCTION(Message), &capture, asCALL_CDECL));
			Check(engine->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1));
			Check(engine->SetEngineProperty(asEP_DISALLOW_GLOBAL_VARS, 1));
			Check(engine->SetEngineProperty(asEP_MAX_STACK_SIZE, 256 * 1024));
			Check(engine->SetEngineProperty(asEP_MAX_CALL_STACK_SIZE, 128));
			Check(engine->SetEngineProperty(asEP_MAX_NESTED_CALLS, 16));
			Check(engine->SetContextCallbacks(RequestContext, ReturnContext, &capture));
			RegisterAPI(engine.get(), capture);

			CScriptBuilder builder;
			builder.SetIncludeCallback(Include, &capture);
			Check(builder.StartNewModule(engine.get(), "SkinPreview"));
			builder.DefineWord("CLIENT");
			Check(builder.AddSectionFromMemory("preview-api", kSkinPreviewScriptApi));
			for (const char* path : {"Scripts/Base/Utils.as", "Scripts/Skin/BasicViewWeapon.as",
			                         "Scripts/Skin/BasicViewTool.as"})
				capture.AddSection(builder, path);
			const char* names[] = {"Spade", "Grenade", "Rifle", "SMG", "Shotgun"};
			const std::string suffix = thirdPerson ? "ThirdPerson" : "View";
			capture.AddSection(builder, "Scripts/Skin/" +
			                              std::string(names[static_cast<std::size_t>(type)]) + "/" +
			                              suffix + ".as");
			if (builder.BuildModule() < 0)
				SPRaise("Cannot build preview: %s", capture.error.c_str());
			capture.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
			auto* module = builder.GetModule();
			Check(module->SetDefaultNamespace("spades"));
			std::unique_ptr<asIScriptObject, Release<asIScriptObject>> renderer(
			  static_cast<asIScriptObject*>(
			    engine->CreateScriptObject(module->GetTypeInfoByName("Renderer"))));
			std::unique_ptr<asIScriptObject, Release<asIScriptObject>> audio(
			  static_cast<asIScriptObject*>(
			    engine->CreateScriptObject(module->GetTypeInfoByName("AudioDevice"))));
			std::unique_ptr<asIScriptContext, Release<asIScriptContext>> context(
			  RequestContext(engine.get(), &capture));
			const std::string factoryName = std::string("Create") +
			                                (thirdPerson ? "ThirdPerson" : "View") +
			                                names[static_cast<std::size_t>(type)] + "Skin";
			auto* factory = module->GetFunctionByName(factoryName.c_str());
			if (!factory || !renderer || !audio)
				SPRaise("Missing preview skin factory");
			Check(context->Prepare(factory));
			Check(context->SetArgObject(0, renderer.get()));
			Check(context->SetArgObject(1, audio.get()));
			Execute(*context);
			auto* returned = static_cast<asIScriptObject*>(context->GetReturnObject());
			if (!returned)
				SPRaise("Preview skin factory returned null");
			returned->AddRef();
			std::unique_ptr<asIScriptObject, Release<asIScriptObject>> skin(returned);
			context->Unprepare();
			auto prepare = [&](const char* declaration) {
				auto* method = skin->GetObjectType()->GetMethodByDecl(declaration);
				if (!method)
					return false;
				Check(context->Prepare(method));
				Check(context->SetObject(skin.get()));
				return true;
			};
			auto floatProperty = [&](const char* declaration, float value) {
				if (prepare(declaration)) {
					Check(context->SetArgFloat(0, value));
					Execute(*context);
				}
			};
			floatProperty("void set_RaiseState(float)", 1);
			floatProperty("void set_SprintState(float)", 0);
			floatProperty("void set_AimDownSightState(float)", 0);
			floatProperty("void set_ReadyState(float)", 1);
			floatProperty("void set_ReloadProgress(float)", 1);
			floatProperty("void set_CookTime(float)", 0);
			floatProperty("void set_ActionProgress(float)", 0);
			for (const char* declaration : {"void set_IsReloading(bool)", "void set_IsMuted(bool)"})
				if (prepare(declaration)) {
					Check(context->SetArgByte(0, std::string(declaration).find("Muted") !=
					                               std::string::npos));
					Execute(*context);
				}
			const int clipSize = type == SkinType::SMG ? 30 : type == SkinType::Shotgun ? 6 : 10;
			for (const char* declaration : {"void set_Ammo(int)", "void set_ClipSize(int)",
			                                "void set_ActionType(SpadeActionType)"})
				if (prepare(declaration)) {
					Check(context->SetArgDWord(0, std::string(declaration).find("ActionType") !=
					                                  std::string::npos
					                                ? 0
													: clipSize));
					Execute(*context);
				}
			if (prepare("void set_TeamColor(Vector3)")) {
				Check(context->SetArgObject(0, const_cast<Vector3*>(&options.teamColor)));
				Execute(*context);
			}
			Matrix4 identity = Matrix4::Identity();
			for (const char* declaration :
			     {"void set_EyeMatrix(Matrix4)", "void set_OriginMatrix(Matrix4)"})
				if (prepare(declaration)) {
					Check(context->SetArgObject(0, &identity));
					Execute(*context);
				}
			// Settle raise/idle springs without firing/reloading. Each tick is the
			// same length as a normal frame; scripts often clamp larger dt values.
			for (int frame = 0; frame < 60; ++frame)
				if (prepare("void Update(float)")) {
					Check(context->SetArgFloat(0, 1.0F / 60));
					Execute(*context);
				}
			capture.scene.parts.clear();
			capture.scene.beams.clear();
			if (!prepare("void AddToScene()"))
				SPRaise("Preview skin has no scene method");
			Execute(*context);
			if (capture.scene.parts.empty() && capture.scene.beams.empty())
				SPRaise("Preview script drew no weapon");

			std::map<std::string, Handle<VoxelModel>> cache;
			std::size_t modelBytes = 0, modelCells = 0;
			for (auto& part : capture.scene.parts) {
				if (part.model)
					continue;
				auto& model = cache[Lower(part.path)];
				if (!model) {
					auto bytes = reader(part.path, kMaxModelBytes);
					if (!bytes)
						SPRaise("Missing preview component: %s", part.path.c_str());
					modelBytes += bytes->size();
					if (modelBytes > 128 * 1024 * 1024)
						SPRaise("Preview model data is too large");
					model = LoadSkinPreviewModel(*bytes);
					modelCells +=
					  std::size_t(model->GetWidth()) * model->GetHeight() * model->GetDepth();
					if (modelCells > 8 * 1024 * 1024)
						SPRaise("Too many preview model cells");
				}
				part.model = model;
			}
			capture.scene.scripted = true;
			return std::move(capture.scene);
		}
	} // namespace gui
} // namespace spades
