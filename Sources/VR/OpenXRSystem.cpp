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

#include "OpenXRSystem.h"

#if USE_OPENXR

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include <Core/Debug.h>
#include <Core/Settings.h>

#ifdef __ANDROID__
#include <Imports/SDL.h>
#endif

// Eye resolution relative to what the runtime recommends.
DEFINE_SPADES_SETTING(vr_renderScale, "1");

// Older OpenXR headers (Ubuntu 24.04's 1.0.20) do not name 1.0 by itself
#ifndef XR_API_VERSION_1_0
#define XR_API_VERSION_1_0 XR_MAKE_VERSION(1, 0, XR_VERSION_PATCH(XR_CURRENT_API_VERSION))
#endif
// The HUD panel, in metres, straight ahead of the head.
DEFINE_SPADES_SETTING(vr_hudDistance, "1.5");
DEFINE_SPADES_SETTING(vr_hudWidth, "1.6");
// Optional OpenXR extensions not to enable even when the runtime has them
// (comma separated). Makes a PC runtime lack what a Quest lacks, or works around
// a runtime that misbehaves with one.
DEFINE_SPADES_SETTING(vr_disableExtensions, "");
// Keeps a standalone headset's CPU and GPU at their high sustained levels
// (XR_EXT_performance_settings) instead of the runtime's own choice: steadier
// in busy scenes, for more battery and heat.
DEFINE_SPADES_SETTING(vr_performanceBoost, "0");
// The headset's refresh rate, in Hz: 0 leaves it its own (a Quest's 72).
// Higher is smoother, and leaves less time for each frame.
DEFINE_SPADES_SETTING(vr_refreshRate, "0");
// 1: draw each eye with the smallest centred frustum covering the runtime's, as
// before the renderer took off-centre ones (10-20% more pixels). The control for
// the asymmetric path, and a way back if a runtime mishandles it.
DEFINE_SPADES_SETTING(vr_symmetricFrustum, "0");
// Foveated rendering, by the headset's runtime (XR_FB_foveation): each eye's
// edges shaded at lower density, 0 off, 1 low, 2 medium, 3 high, 4 high top
// (high, and more off the top of each eye, which a player seldom looks at; Meta's
// runtime knows it, though OpenXR's headers stop at high). Whether it is
// on at all is read at start (the renderer's passes are made for it or not);
// the level is applied as it changes. Only where the eyes are drawn at once,
// straight into the headset's images (VulkanRenderer's direct eyes).
DEFINE_SPADES_SETTING(vr_foveation, "0");
// For tests: "WxH", the eye images that size whatever the runtime recommends (a
// Quest 3's runtime gave 2400x2514 one launch and 1680x1760 the next)
DEFINE_SPADES_SETTING(vr_debugEyeSize, "");
// 1: the runtime lowers the foveation below vr_foveation's level while the GPU
// has time to spare, and takes it back up to it when it has not. Off by
// default: the edges of the view changing their detail as the load does is
// what made a tester feel sick (a steady level is easier on the eyes).
DEFINE_SPADES_SETTING(vr_foveationDynamic, "0");
// 1: the foveated eye images stored subsampled, the coarse parts at the size
// they were shaded at, which the headset filters up as it shows them, rather
// than each coarse fragment copied into its block of pixels: smooth edges where
// the blocks' stair steps crawled (testers: flicker at the voxels' edges, and
// sick). Meta's recommended layout. Read at start. Off by default until seen
// in a headset: a Quest 3's screen capture showed one flat colour with it.
DEFINE_SPADES_SETTING(vr_foveationSubsampled, "0");
// For tests: "yaw pitch" in degrees replaces the headset's head direction with a
// fixed one (pitch below 0 looks down), so every run sees the same picture. The
// eyes keep their spacing. Empty: the headset's own.
DEFINE_SPADES_SETTING(vr_debugFixedLook, "");
// For tests: record every frame's head, eyes and controllers to this file (a
// line each; VRTraceFormat), or replay a file instead of the headset's own
// input, one line a frame, holding the last. Read at the session's start.
DEFINE_SPADES_SETTING(vr_traceRecord, "");
DEFINE_SPADES_SETTING(vr_traceReplay, "");
SPADES_SETTING(vr_wristHud);
SPADES_SETTING(vr_wristWidth);
// Tracked hands point at the menus and pinch to click, when the controllers
// are put down (Quest; a PC runtime with hand tracking). Read at start.
DEFINE_SPADES_SETTING(vr_handTracking, "1");
// The controllers' buttons, moved about (VRParseBindings; the in-game Controls
// screen's Change Buttons writes it). Empty: the default layout.
DEFINE_SPADES_SETTING(vr_bindings, "");
// Walk with the tool hand's stick and turn with the other's.
DEFINE_SPADES_SETTING(vr_swapSticks, "0");

namespace spades {
	namespace vr {

		namespace {
			OpenXRSystem* g_system = nullptr;

			client::VRPose ToPose(const XrPosef& p, bool valid) {
				client::VRPose o;
				o.position = MakeVector3(p.position.x, p.position.y, p.position.z);
				o.orientation =
				  client::VRQuat(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w);
				o.valid = valid;
				return o;
			}

			bool PoseValid(XrSpaceLocationFlags flags) {
				return (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
				       (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
			}

			XrPosef IdentityPose() {
				XrPosef p{};
				p.orientation.w = 1.0F;
				return p;
			}
		} // namespace

		OpenXRSystem* GetOpenXRSystem() { return g_system; }
		void SetOpenXRSystem(OpenXRSystem* s) {
			g_system = s;
			client::SetVRSystem(s);
		}

		bool OpenXRSystem::Check(XrResult r, const char* what) {
			if (XR_SUCCEEDED(r))
				return true;
			char buf[XR_MAX_RESULT_STRING_SIZE];
			if (instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(instance, r, buf)))
				std::snprintf(buf, sizeof(buf), "XrResult %d", static_cast<int>(r));
			SPLog("[OpenXR] %s failed: %s", what, buf);
			return false;
		}

		XrPath OpenXRSystem::Path(const char* s) {
			XrPath p = XR_NULL_PATH;
			Check(xrStringToPath(instance, s, &p), s);
			return p;
		}

#ifdef __ANDROID__
		namespace {
			// The only Android-specific OpenXR code: the loader and the instance
			// want the Java VM and the activity, which SDL's activity holds.
			bool InitAndroidLoader() {
				static bool done = false;
				if (done)
					return true;
				PFN_xrInitializeLoaderKHR init = nullptr;
				if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
				                                    reinterpret_cast<PFN_xrVoidFunction*>(&init))) ||
				    !init)
					return false;
				JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
				JavaVM* vm = nullptr;
				if (!env || env->GetJavaVM(&vm) != 0)
					return false;
				XrLoaderInitInfoAndroidKHR li{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
				li.applicationVM = vm;
				li.applicationContext = SDL_AndroidGetActivity();
				done = XR_SUCCEEDED(init(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&li)));
				return done;
			}
		} // namespace
#endif

		std::string OpenXRSystem::ProbeRuntime(std::string& why) {
#ifdef __ANDROID__
			if (!InitAndroidLoader()) {
				why = "The OpenXR loader could not start.";
				return std::string();
			}
#endif
			uint32_t extCount = 0;
			if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr))) {
				why = "No OpenXR runtime is installed or active (SteamVR, Meta Link, "
				      "Virtual Desktop and WMR all provide one).";
				return std::string();
			}
			std::vector<XrExtensionProperties> exts(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
			xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());
			bool hasVulkan2 = false;
			for (const auto& e : exts)
				if (std::strcmp(e.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0)
					hasVulkan2 = true;

			XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
			std::strncpy(ici.applicationInfo.applicationName, "ZeroSpades launcher",
			             XR_MAX_APPLICATION_NAME_SIZE - 1);
			std::strncpy(ici.applicationInfo.engineName, "ZeroSpades", XR_MAX_ENGINE_NAME_SIZE - 1);
			ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
			XrInstance inst = XR_NULL_HANDLE;
			if (XR_FAILED(xrCreateInstance(&ici, &inst))) {
				why = "The OpenXR runtime would not start.";
				return std::string();
			}
			std::string name = "OpenXR runtime";
			XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
			if (XR_SUCCEEDED(xrGetInstanceProperties(inst, &props)))
				name = props.runtimeName;
			xrDestroyInstance(inst);

			if (!hasVulkan2) {
				why = name + " cannot share Vulkan images, which VR needs.";
				return std::string();
			}
			return name;
		}

		OpenXRSystem* OpenXRSystem::TryCreate() {
			std::unique_ptr<OpenXRSystem> s(new OpenXRSystem());
			if (!s->Initialize())
				return nullptr;
			return s.release();
		}

		bool OpenXRSystem::Initialize() {
			SPADES_MARK_FUNCTION();

#ifdef __ANDROID__
			if (!InitAndroidLoader()) {
				SPLog("[OpenXR] The loader could not start.");
				return false;
			}
#endif
			// The runtime has to be able to hand us a Vulkan device.
			uint32_t extCount = 0;
			if (!Check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr),
			           "xrEnumerateInstanceExtensionProperties")) {
				SPLog("[OpenXR] No OpenXR runtime is installed or active.");
				return false;
			}
			std::vector<XrExtensionProperties> exts(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
			xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());
			bool hasVulkan2 = false;
			for (const auto& e : exts)
				if (std::strcmp(e.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0)
					hasVulkan2 = true;
			if (!hasVulkan2) {
				SPLog("[OpenXR] The active runtime does not support %s.",
				      XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
				return false;
			}

			// The required one, then every optional one the runtime has (XrCaps).
			std::vector<std::string> available;
			for (const auto& e : exts)
				available.push_back(e.extensionName);
			std::vector<std::string> enableNames = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
#ifdef __ANDROID__
			const bool android = true;
#else
			const bool android = false;
#endif
			caps = NegotiateXrCaps(available, vr_disableExtensions, android, enableNames);
			std::vector<const char*> enabledExts;
			for (const auto& n : enableNames)
				enabledExts.push_back(n.c_str());
			for (int i = 0; i < (int)XrCap::Count; ++i) {
				const char* name = XrCapExtension((XrCap)i);
				const bool listed =
				  std::find(available.begin(), available.end(), name) != available.end();
				SPLog("[OpenXR] %-36s %s", name,
				      caps.has[i] ? "on" : listed ? "off (vr_disableExtensions or a prerequisite)" : "-");
			}

			XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
			std::strncpy(ici.applicationInfo.applicationName, "ZeroSpades",
			             XR_MAX_APPLICATION_NAME_SIZE - 1);
			ici.applicationInfo.applicationVersion = 1;
			std::strncpy(ici.applicationInfo.engineName, "ZeroSpades", XR_MAX_ENGINE_NAME_SIZE - 1);
			ici.applicationInfo.engineVersion = 1;
			// 1.0 rather than the header's current version: a 1.0 runtime refuses
			// an instance that asks for 1.1, and nothing here needs 1.1.
			ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
			ici.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
			ici.enabledExtensionNames = enabledExts.data();
#ifdef __ANDROID__
			XrInstanceCreateInfoAndroidKHR aci{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
			if (caps[XrCap::AndroidCreateInstance]) {
				JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
				JavaVM* vm = nullptr;
				if (env)
					env->GetJavaVM(&vm);
				aci.applicationVM = vm;
				aci.applicationActivity = SDL_AndroidGetActivity();
				ici.next = &aci;
			}
#endif
			if (!Check(xrCreateInstance(&ici, &instance), "xrCreateInstance"))
				return false;

			XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
			if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &props)))
				SPLog("[OpenXR] Runtime: %s %d.%d.%d", props.runtimeName,
				      (int)XR_VERSION_MAJOR(props.runtimeVersion),
				      (int)XR_VERSION_MINOR(props.runtimeVersion),
				      (int)XR_VERSION_PATCH(props.runtimeVersion));

			XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
			sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
			XrResult r = xrGetSystem(instance, &sgi, &systemId);
			if (r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
				SPLog("[OpenXR] No headset is connected (is the headset on and the "
				      "streaming app running?).");
				return false;
			}
			if (!Check(r, "xrGetSystem"))
				return false;

			XrSystemProperties sysProps{XR_TYPE_SYSTEM_PROPERTIES};
			if (XR_SUCCEEDED(xrGetSystemProperties(instance, systemId, &sysProps))) {
				SPLog("[OpenXR] Headset: %s", sysProps.systemName);
				headsetName = sysProps.systemName;
			}

			auto load = [this](const char* name, PFN_xrVoidFunction* fn) {
				return Check(xrGetInstanceProcAddr(instance, name, fn), name);
			};
			if (!load("xrCreateVulkanInstanceKHR",
			          reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateVulkanInstance)) ||
			    !load("xrCreateVulkanDeviceKHR",
			          reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateVulkanDevice)) ||
			    !load("xrGetVulkanGraphicsDevice2KHR",
			          reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetVulkanGraphicsDevice)) ||
			    !load("xrGetVulkanGraphicsRequirements2KHR",
			          reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetVulkanGraphicsRequirements)))
				return false;
			if (caps[XrCap::PerformanceSettings] &&
			    !load("xrPerfSettingsSetPerformanceLevelEXT",
			          reinterpret_cast<PFN_xrVoidFunction*>(&pfnPerfSettingsSetPerformanceLevel)))
				pfnPerfSettingsSetPerformanceLevel = nullptr;
			if (caps[XrCap::DisplayRefreshRate] &&
			    (!load("xrEnumerateDisplayRefreshRatesFB",
			           reinterpret_cast<PFN_xrVoidFunction*>(&pfnEnumerateDisplayRefreshRates)) ||
			     !load("xrRequestDisplayRefreshRateFB",
			           reinterpret_cast<PFN_xrVoidFunction*>(&pfnRequestDisplayRefreshRate)))) {
				pfnEnumerateDisplayRefreshRates = nullptr;
				pfnRequestDisplayRefreshRate = nullptr;
			}
#ifdef XR_EXT_hand_tracking
			if (caps[XrCap::HandTracking] &&
			    !(load("xrCreateHandTrackerEXT",
			           reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateHandTracker)) &&
			      load("xrDestroyHandTrackerEXT",
			           reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyHandTracker)) &&
			      load("xrLocateHandJointsEXT",
			           reinterpret_cast<PFN_xrVoidFunction*>(&pfnLocateHandJoints)))) {
				pfnCreateHandTracker = nullptr;
				pfnDestroyHandTracker = nullptr;
				pfnLocateHandJoints = nullptr;
			}
#endif

			// Required before a session may be created, and it says which Vulkan
			// version the instance has to ask for.
			XrGraphicsRequirementsVulkan2KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
			if (!Check(pfnGetVulkanGraphicsRequirements(instance, systemId, &req),
			           "xrGetVulkanGraphicsRequirements2KHR"))
				return false;
			uint32_t minVk = VK_MAKE_VERSION(XR_VERSION_MAJOR(req.minApiVersionSupported),
			                                 XR_VERSION_MINOR(req.minApiVersionSupported), 0);
			requiredVulkanApiVersion = std::max<uint32_t>(VK_API_VERSION_1_0, minVk);

			uint32_t viewCount = 0;
			if (!Check(xrEnumerateViewConfigurationViews(instance, systemId,
			                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			                                             0, &viewCount, nullptr),
			           "xrEnumerateViewConfigurationViews"))
				return false;
			if (viewCount != 2) {
				SPLog("[OpenXR] Expected a stereo headset, got %u views.", viewCount);
				return false;
			}
			viewConfigs.assign(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
			xrEnumerateViewConfigurationViews(instance, systemId,
			                                  XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount,
			                                  &viewCount, viewConfigs.data());
			SPLog("[OpenXR] Recommended eye size %ux%u", viewConfigs[0].recommendedImageRectWidth,
			      viewConfigs[0].recommendedImageRectHeight);

			for (auto& v : views)
				v = {XR_TYPE_VIEW};
			return true;
		}

		OpenXRSystem::~OpenXRSystem() {
			DestroySession();
			if (instance != XR_NULL_HANDLE)
				xrDestroyInstance(instance);
			if (g_system == this)
				SetOpenXRSystem(nullptr);
		}

		void OpenXRSystem::DestroySession() {
			if (traceOut) {
				std::fclose(traceOut);
				traceOut = nullptr;
			}
			// The swapchain images may still be the target of submitted work.
			if (vkDevice != VK_NULL_HANDLE) {
				vkDeviceWaitIdle(vkDevice);
				vkDevice = VK_NULL_HANDLE;
			}
			if (foveationProfile != XR_NULL_HANDLE && pfnDestroyFoveationProfile)
				pfnDestroyFoveationProfile(foveationProfile);
			foveationProfile = XR_NULL_HANDLE;
			appliedFoveation = -1;
			for (auto& sc : eyeSwapchains)
				DestroySwapchain(sc);
			DestroySwapchain(hudSwapchain);
			DestroySwapchain(wristSwapchain);
			for (int i = 0; i < 2; i++) {
				if (gripSpaces[i] != XR_NULL_HANDLE)
					xrDestroySpace(gripSpaces[i]);
				if (aimSpaces[i] != XR_NULL_HANDLE)
					xrDestroySpace(aimSpaces[i]);
				gripSpaces[i] = aimSpaces[i] = XR_NULL_HANDLE;
			}
			if (appSpace != XR_NULL_HANDLE)
				xrDestroySpace(appSpace);
			if (viewSpace != XR_NULL_HANDLE)
				xrDestroySpace(viewSpace);
			appSpace = viewSpace = XR_NULL_HANDLE;
			if (actionSet != XR_NULL_HANDLE)
				xrDestroyActionSet(actionSet);
			actionSet = XR_NULL_HANDLE;
			DestroyHandTrackers();
			if (session != XR_NULL_HANDLE) {
				if (sessionRunning)
					xrEndSession(session);
				xrDestroySession(session);
			}
			session = XR_NULL_HANDLE;
			sessionRunning = false;
			frameBegun = false;
			frameState.running = false;
		}

		VkResult OpenXRSystem::CreateVulkanInstance(const VkInstanceCreateInfo& info,
		                                            VkInstance& out) {
			XrVulkanInstanceCreateInfoKHR ci{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
			ci.systemId = systemId;
			ci.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
			ci.vulkanCreateInfo = &info;
			VkResult vkResult = VK_ERROR_INITIALIZATION_FAILED;
			if (!Check(pfnCreateVulkanInstance(instance, &ci, &out, &vkResult),
			           "xrCreateVulkanInstanceKHR"))
				return VK_ERROR_INITIALIZATION_FAILED;
			return vkResult;
		}

		VkPhysicalDevice OpenXRSystem::GetVulkanPhysicalDevice(VkInstance vkInstance) {
			XrVulkanGraphicsDeviceGetInfoKHR gi{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
			gi.systemId = systemId;
			gi.vulkanInstance = vkInstance;
			VkPhysicalDevice pd = VK_NULL_HANDLE;
			if (!Check(pfnGetVulkanGraphicsDevice(instance, &gi, &pd),
			           "xrGetVulkanGraphicsDevice2KHR"))
				return VK_NULL_HANDLE;
			return pd;
		}

		VkResult OpenXRSystem::CreateVulkanDevice(VkPhysicalDevice physicalDevice,
		                                          const VkDeviceCreateInfo& info, VkDevice& out) {
			XrVulkanDeviceCreateInfoKHR ci{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
			ci.systemId = systemId;
			ci.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
			ci.vulkanPhysicalDevice = physicalDevice;
			ci.vulkanCreateInfo = &info;
			VkResult vkResult = VK_ERROR_INITIALIZATION_FAILED;
			if (!Check(pfnCreateVulkanDevice(instance, &ci, &out, &vkResult),
			           "xrCreateVulkanDeviceKHR"))
				return VK_ERROR_INITIALIZATION_FAILED;
			return vkResult;
		}

		bool OpenXRSystem::CreateSession(VkInstance vkInstance, VkPhysicalDevice physicalDevice,
		                                 VkDevice device, uint32_t queueFamily,
		                                 uint32_t queueIndex) {
			SPADES_MARK_FUNCTION();

			XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
			binding.instance = vkInstance;
			binding.physicalDevice = physicalDevice;
			binding.device = device;
			binding.queueFamilyIndex = queueFamily;
			binding.queueIndex = queueIndex;

			XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
			sci.next = &binding;
			sci.systemId = systemId;
			if (!Check(xrCreateSession(instance, &sci, &session), "xrCreateSession"))
				return false;
			vkDevice = device;

			// A stage puts the floor at y = 0; without one the play space is
			// recentred on the head anyway, so LOCAL loses nothing that matters.
			uint32_t spaceCount = 0;
			xrEnumerateReferenceSpaces(session, 0, &spaceCount, nullptr);
			std::vector<XrReferenceSpaceType> spaces(spaceCount);
			xrEnumerateReferenceSpaces(session, spaceCount, &spaceCount, spaces.data());
			appSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
			for (auto t : spaces)
				if (t == XR_REFERENCE_SPACE_TYPE_STAGE)
					appSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;

			XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
			rsci.poseInReferenceSpace = IdentityPose();
			rsci.referenceSpaceType = appSpaceType;
			if (!Check(xrCreateReferenceSpace(session, &rsci, &appSpace), "xrCreateReferenceSpace"))
				return false;
			rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
			if (!Check(xrCreateReferenceSpace(session, &rsci, &viewSpace),
			           "xrCreateReferenceSpace(VIEW)"))
				return false;
			SPLog("[OpenXR] Tracking space: %s",
			      appSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE ? "stage" : "local");

			uint32_t fmtCount = 0;
			xrEnumerateSwapchainFormats(session, 0, &fmtCount, nullptr);
			runtimeFormats.resize(fmtCount);
			xrEnumerateSwapchainFormats(session, fmtCount, &fmtCount, runtimeFormats.data());

			if (!CreateActions())
				return false;
			CreateHandTrackers();

			SPLog("[OpenXR] Session created");
			return true;
		}

		bool OpenXRSystem::CreateActions() {
			XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
			std::strcpy(asci.actionSetName, "gameplay");
			std::strcpy(asci.localizedActionSetName, "Gameplay");
			if (!Check(xrCreateActionSet(instance, &asci, &actionSet), "xrCreateActionSet"))
				return false;

			handPaths[0] = Path("/user/hand/left");
			handPaths[1] = Path("/user/hand/right");

			auto make = [this](XrAction& action, XrActionType type, const char* name,
			                   const char* localized) {
				XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
				aci.actionType = type;
				std::strcpy(aci.actionName, name);
				std::strcpy(aci.localizedActionName, localized);
				aci.countSubactionPaths = 2;
				aci.subactionPaths = handPaths;
				return Check(xrCreateAction(actionSet, &aci, &action), name);
			};
			bool ok = make(gripPoseAction, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Hand grip") &&
			          make(aimPoseAction, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Hand aim") &&
			          make(triggerAction, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger") &&
			          make(squeezeAction, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Grip") &&
			          make(thumbstickAction, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick",
			               "Thumbstick") &&
			          make(thumbstickClickAction, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click",
			               "Thumbstick click") &&
			          make(primaryAction, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_primary",
			               "A / X button") &&
			          make(secondaryAction, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_secondary",
			               "B / Y button") &&
			          make(menuAction, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu") &&
			          make(hapticAction, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Rumble");
			if (!ok)
				return false;

			SuggestBindings();

			XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
			attach.countActionSets = 1;
			attach.actionSets = &actionSet;
			if (!Check(xrAttachSessionActionSets(session, &attach), "xrAttachSessionActionSets"))
				return false;

			for (int h = 0; h < 2; h++) {
				XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
				sci.poseInActionSpace = IdentityPose();
				sci.subactionPath = handPaths[h];
				sci.action = gripPoseAction;
				Check(xrCreateActionSpace(session, &sci, &gripSpaces[h]), "grip space");
				sci.action = aimPoseAction;
				Check(xrCreateActionSpace(session, &sci, &aimSpaces[h]), "aim space");
			}
			return true;
		}

		bool OpenXRSystem::SuggestBindings() {
			// One list per controller family. A path a profile does not have makes
			// the runtime refuse the whole profile, so each list names only inputs
			// that profile documents. Every profile gets its own call, so one bad
			// profile cannot take the others down with it.
			struct B {
				XrAction action;
				const char* path;
			};
			auto suggest = [this](const char* profile, std::initializer_list<B> list) {
				std::vector<XrActionSuggestedBinding> bindings;
				for (const auto& b : list)
					bindings.push_back({b.action, Path(b.path)});
				XrInteractionProfileSuggestedBinding s{
				  XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
				s.interactionProfile = Path(profile);
				s.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
				s.suggestedBindings = bindings.data();
				Check(xrSuggestInteractionProfileBindings(instance, &s), profile);
			};

			// Quest (Touch), which is also what Virtual Desktop and Link present.
			suggest("/interaction_profiles/oculus/touch_controller",
			        {{gripPoseAction, "/user/hand/left/input/grip/pose"},
			         {gripPoseAction, "/user/hand/right/input/grip/pose"},
			         {aimPoseAction, "/user/hand/left/input/aim/pose"},
			         {aimPoseAction, "/user/hand/right/input/aim/pose"},
			         {triggerAction, "/user/hand/left/input/trigger/value"},
			         {triggerAction, "/user/hand/right/input/trigger/value"},
			         {squeezeAction, "/user/hand/left/input/squeeze/value"},
			         {squeezeAction, "/user/hand/right/input/squeeze/value"},
			         {thumbstickAction, "/user/hand/left/input/thumbstick"},
			         {thumbstickAction, "/user/hand/right/input/thumbstick"},
			         {thumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
			         {thumbstickClickAction, "/user/hand/right/input/thumbstick/click"},
			         {primaryAction, "/user/hand/left/input/x/click"},
			         {primaryAction, "/user/hand/right/input/a/click"},
			         {secondaryAction, "/user/hand/left/input/y/click"},
			         {secondaryAction, "/user/hand/right/input/b/click"},
			         {menuAction, "/user/hand/left/input/menu/click"},
			         {hapticAction, "/user/hand/left/output/haptic"},
			         {hapticAction, "/user/hand/right/output/haptic"}});

			// Touch Plus (Quest 3, 3S) and Touch Pro (Quest Pro), where the runtime
			// names them: the same buttons as Touch, and the picture of the
			// controllers in hand (no ring) can then be told from a Quest 2's.
			// `squeeze`: the grip's input there (a value, or only a click)
			auto touchLike = [&](const char* profile, const char* squeeze = "value") {
				const std::string sl = std::string("/user/hand/left/input/squeeze/") + squeeze;
				const std::string sr = std::string("/user/hand/right/input/squeeze/") + squeeze;
				suggest(profile,
				        {{gripPoseAction, "/user/hand/left/input/grip/pose"},
				         {gripPoseAction, "/user/hand/right/input/grip/pose"},
				         {aimPoseAction, "/user/hand/left/input/aim/pose"},
				         {aimPoseAction, "/user/hand/right/input/aim/pose"},
				         {triggerAction, "/user/hand/left/input/trigger/value"},
				         {triggerAction, "/user/hand/right/input/trigger/value"},
				         {squeezeAction, sl.c_str()},
				         {squeezeAction, sr.c_str()},
				         {thumbstickAction, "/user/hand/left/input/thumbstick"},
				         {thumbstickAction, "/user/hand/right/input/thumbstick"},
				         {thumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
				         {thumbstickClickAction, "/user/hand/right/input/thumbstick/click"},
				         {primaryAction, "/user/hand/left/input/x/click"},
				         {primaryAction, "/user/hand/right/input/a/click"},
				         {secondaryAction, "/user/hand/left/input/y/click"},
				         {secondaryAction, "/user/hand/right/input/b/click"},
				         {menuAction, "/user/hand/left/input/menu/click"},
				         {hapticAction, "/user/hand/left/output/haptic"},
				         {hapticAction, "/user/hand/right/output/haptic"}});
			};
			if (caps[XrCap::TouchControllerPlus])
				touchLike("/interaction_profiles/meta/touch_controller_plus");
			if (caps[XrCap::TouchControllerPro])
				touchLike("/interaction_profiles/facebook/touch_controller_pro");
			// The others laid out as Touch is: X and Y on the left with a menu
			// button, A and B on the right, a stick each
			if (caps[XrCap::PicoController]) {
				touchLike("/interaction_profiles/bytedance/pico4_controller");
				touchLike("/interaction_profiles/bytedance/pico_neo3_controller");
			}
			if (caps[XrCap::Focus3Controller])
				touchLike("/interaction_profiles/htc/vive_focus3_controller");
			if (caps[XrCap::CosmosController])
				touchLike("/interaction_profiles/htc/vive_cosmos_controller", "click");
			if (caps[XrCap::HpController])
				touchLike("/interaction_profiles/hp/mixed_reality_controller");

			// Steam Frame: a D-pad on the left and A, B, X, Y on the right, as
			// SteamVR maps Touch onto it (the lower button A / D-pad down, the
			// upper ones B, X, Y / D-pad up, left, right), and View for the menu.
			if (caps[XrCap::FrameController])
				suggest("/interaction_profiles/valve/frame_controller_valve",
				        {{gripPoseAction, "/user/hand/left/input/grip/pose"},
				         {gripPoseAction, "/user/hand/right/input/grip/pose"},
				         {aimPoseAction, "/user/hand/left/input/aim/pose"},
				         {aimPoseAction, "/user/hand/right/input/aim/pose"},
				         {triggerAction, "/user/hand/left/input/trigger/value"},
				         {triggerAction, "/user/hand/right/input/trigger/value"},
				         {squeezeAction, "/user/hand/left/input/squeeze/value"},
				         {squeezeAction, "/user/hand/right/input/squeeze/value"},
				         {thumbstickAction, "/user/hand/left/input/thumbstick"},
				         {thumbstickAction, "/user/hand/right/input/thumbstick"},
				         {thumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
				         {thumbstickClickAction, "/user/hand/right/input/thumbstick/click"},
				         {primaryAction, "/user/hand/left/input/dpad_down/click"},
				         {primaryAction, "/user/hand/right/input/a/click"},
				         {secondaryAction, "/user/hand/left/input/dpad_up/click"},
				         {secondaryAction, "/user/hand/left/input/dpad_left/click"},
				         {secondaryAction, "/user/hand/left/input/dpad_right/click"},
				         {secondaryAction, "/user/hand/right/input/b/click"},
				         {secondaryAction, "/user/hand/right/input/x/click"},
				         {secondaryAction, "/user/hand/right/input/y/click"},
				         {menuAction, "/user/hand/left/input/view/click"},
				         {hapticAction, "/user/hand/left/output/haptic"},
				         {hapticAction, "/user/hand/right/output/haptic"}});

			// Index. Its system button is reserved, so there is no menu binding;
			// holding both B buttons opens the menu instead (see the client).
			suggest("/interaction_profiles/valve/index_controller",
			        {{gripPoseAction, "/user/hand/left/input/grip/pose"},
			         {gripPoseAction, "/user/hand/right/input/grip/pose"},
			         {aimPoseAction, "/user/hand/left/input/aim/pose"},
			         {aimPoseAction, "/user/hand/right/input/aim/pose"},
			         {triggerAction, "/user/hand/left/input/trigger/value"},
			         {triggerAction, "/user/hand/right/input/trigger/value"},
			         {squeezeAction, "/user/hand/left/input/squeeze/value"},
			         {squeezeAction, "/user/hand/right/input/squeeze/value"},
			         {thumbstickAction, "/user/hand/left/input/thumbstick"},
			         {thumbstickAction, "/user/hand/right/input/thumbstick"},
			         {thumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
			         {thumbstickClickAction, "/user/hand/right/input/thumbstick/click"},
			         {primaryAction, "/user/hand/left/input/a/click"},
			         {primaryAction, "/user/hand/right/input/a/click"},
			         {secondaryAction, "/user/hand/left/input/b/click"},
			         {secondaryAction, "/user/hand/right/input/b/click"},
			         {hapticAction, "/user/hand/left/output/haptic"},
			         {hapticAction, "/user/hand/right/output/haptic"}});

			// Vive wands: the trackpad stands in for the thumbstick.
			suggest("/interaction_profiles/htc/vive_controller",
			        {{gripPoseAction, "/user/hand/left/input/grip/pose"},
			         {gripPoseAction, "/user/hand/right/input/grip/pose"},
			         {aimPoseAction, "/user/hand/left/input/aim/pose"},
			         {aimPoseAction, "/user/hand/right/input/aim/pose"},
			         {triggerAction, "/user/hand/left/input/trigger/value"},
			         {triggerAction, "/user/hand/right/input/trigger/value"},
			         {squeezeAction, "/user/hand/left/input/squeeze/click"},
			         {squeezeAction, "/user/hand/right/input/squeeze/click"},
			         {thumbstickAction, "/user/hand/left/input/trackpad"},
			         {thumbstickAction, "/user/hand/right/input/trackpad"},
			         {thumbstickClickAction, "/user/hand/left/input/trackpad/click"},
			         {primaryAction, "/user/hand/right/input/trackpad/click"},
			         {secondaryAction, "/user/hand/right/input/menu/click"},
			         {menuAction, "/user/hand/left/input/menu/click"},
			         {hapticAction, "/user/hand/left/output/haptic"},
			         {hapticAction, "/user/hand/right/output/haptic"}});

			// Windows Mixed Reality.
			suggest("/interaction_profiles/microsoft/motion_controller",
			        {{gripPoseAction, "/user/hand/left/input/grip/pose"},
			         {gripPoseAction, "/user/hand/right/input/grip/pose"},
			         {aimPoseAction, "/user/hand/left/input/aim/pose"},
			         {aimPoseAction, "/user/hand/right/input/aim/pose"},
			         {triggerAction, "/user/hand/left/input/trigger/value"},
			         {triggerAction, "/user/hand/right/input/trigger/value"},
			         {squeezeAction, "/user/hand/left/input/squeeze/click"},
			         {squeezeAction, "/user/hand/right/input/squeeze/click"},
			         {thumbstickAction, "/user/hand/left/input/thumbstick"},
			         {thumbstickAction, "/user/hand/right/input/thumbstick"},
			         {thumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
			         {thumbstickClickAction, "/user/hand/right/input/thumbstick/click"},
			         {primaryAction, "/user/hand/right/input/trackpad/click"},
			         {secondaryAction, "/user/hand/right/input/menu/click"},
			         {menuAction, "/user/hand/left/input/menu/click"},
			         {hapticAction, "/user/hand/left/output/haptic"},
			         {hapticAction, "/user/hand/right/output/haptic"}});

			// Anything else: at least point, click and open the menu.
			suggest("/interaction_profiles/khr/simple_controller",
			        {{gripPoseAction, "/user/hand/left/input/grip/pose"},
			         {gripPoseAction, "/user/hand/right/input/grip/pose"},
			         {aimPoseAction, "/user/hand/left/input/aim/pose"},
			         {aimPoseAction, "/user/hand/right/input/aim/pose"},
			         {triggerAction, "/user/hand/left/input/select/click"},
			         {triggerAction, "/user/hand/right/input/select/click"},
			         {menuAction, "/user/hand/left/input/menu/click"},
			         {hapticAction, "/user/hand/left/output/haptic"},
			         {hapticAction, "/user/hand/right/output/haptic"}});
			return true;
		}

		bool OpenXRSystem::CreateSwapchain(XRSwapchain& sc, uint32_t width, uint32_t height,
		                                   const std::vector<int64_t>& preferredFormats,
		                                   uint32_t layers, bool foveated) {
			DestroySwapchain(sc);

			int64_t format = 0;
			for (int64_t want : preferredFormats) {
				if (std::find(runtimeFormats.begin(), runtimeFormats.end(), want) !=
				    runtimeFormats.end()) {
					format = want;
					break;
				}
			}
			if (format == 0) {
				// The renderer's output is linear and relies on an sRGB target to
				// encode it; anything else comes out too dark, but it still shows.
				format = runtimeFormats.empty() ? VK_FORMAT_R8G8B8A8_SRGB : runtimeFormats[0];
				SPLog("[OpenXR] No sRGB swapchain format offered; using %lld (colours "
				      "will be off)",
				      static_cast<long long>(format));
			}

			XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
			// The renderer finishes each image with a blit, so it needs TRANSFER_DST;
			// COLOR_ATTACHMENT is what the runtime expects the layout to be in.
			// Eyes drawn straight into it (layered) have the scene resolved into it,
			// and it is SHADER_READ_ONLY between their passes, which needs SAMPLED.
			ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
			                XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
			                (layers > 1 ? XR_SWAPCHAIN_USAGE_SAMPLED_BIT : 0);
			ci.format = format;
			ci.sampleCount = 1;
			ci.width = width;
			ci.height = height;
			ci.faceCount = 1;
			ci.arraySize = layers;
			ci.mipCount = 1;
			// A density map with each image, which the runtime fills for the level
			// ApplyFoveation gives it
			XrSwapchainCreateInfoFoveationFB fov{XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB};
			fov.flags = XR_SWAPCHAIN_CREATE_FOVEATION_FRAGMENT_DENSITY_MAP_BIT_FB;
			XrVulkanSwapchainCreateInfoMETA meta{XR_TYPE_VULKAN_SWAPCHAIN_CREATE_INFO_META};
			meta.additionalCreateFlags = VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT;
			const bool subsampled =
			  foveated && (bool)vr_foveationSubsampled && caps[XrCap::VulkanSwapchainCreateInfo];
			if (subsampled)
				fov.next = &meta;
			if (foveated)
				ci.next = &fov;
			if (!Check(xrCreateSwapchain(session, &ci, &sc.handle), "xrCreateSwapchain"))
				return false;

			uint32_t count = 0;
			xrEnumerateSwapchainImages(sc.handle, 0, &count, nullptr);
			std::vector<XrSwapchainImageVulkan2KHR> images(count,
			                                               {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
			std::vector<XrSwapchainImageFoveationVulkanFB> density(
			  foveated ? count : 0, {XR_TYPE_SWAPCHAIN_IMAGE_FOVEATION_VULKAN_FB});
			for (size_t i = 0; i < density.size(); i++)
				images[i].next = &density[i];
			if (!Check(xrEnumerateSwapchainImages(
			             sc.handle, count, &count,
			             reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
			           "xrEnumerateSwapchainImages"))
				return false;
			sc.images.clear();
			for (const auto& img : images)
				sc.images.push_back(img.image);
			sc.densityImages.clear();
			sc.densityWidth = sc.densityHeight = 0;
			for (const auto& d : density) {
				if (d.image == VK_NULL_HANDLE || d.width == 0 || d.height == 0) {
					SPLog("[OpenXR] The runtime gave an eye image no density map");
					sc.densityImages.clear();
					break;
				}
				sc.densityImages.push_back(d.image);
				sc.densityWidth = d.width;
				sc.densityHeight = d.height;
			}
			sc.layers = layers;
			sc.subsampled = subsampled && !sc.densityImages.empty();
			sc.format = static_cast<VkFormat>(format);
			sc.width = width;
			sc.height = height;
			sc.acquired = UINT32_MAX;
			return true;
		}

		void OpenXRSystem::DestroySwapchain(XRSwapchain& sc) {
			if (sc.handle != XR_NULL_HANDLE)
				xrDestroySwapchain(sc.handle);
			sc = XRSwapchain();
		}

		void OpenXRSystem::CreateEyeSwapchains() {
			// Fixed for the run: the images are sized for it.
			symmetricFrustum = (bool)vr_symmetricFrustum;
			// The runtime's own frustum is drawn at the runtime's own size. A
			// symmetric one covering it is wider, and keeps the recommended pixel
			// density only if the image grows with it.
			float growX = 1.0F, growY = 1.0F;
			for (int i = 0; symmetricFrustum && i < 2; i++) {
				const XrFovf& f = views[i].fov;
				float l = std::tan(-f.angleLeft), r = std::tan(f.angleRight);
				float u = std::tan(f.angleUp), d = std::tan(-f.angleDown);
				float tx = std::max(l, r), ty = std::max(u, d);
				growX = std::max(growX, (2.0F * tx) / (l + r));
				growY = std::max(growY, (2.0F * ty) / (u + d));
			}

			float scale = std::max(0.25F, std::min((float)vr_renderScale, 2.0F));
			auto size = [&](uint32_t rec, uint32_t max, float grow) {
				uint32_t s = static_cast<uint32_t>(rec * scale * grow + 0.5F);
				s = std::min(std::max(s, 64U), max);
				return (s + 1U) & ~1U;
			};
			uint32_t w = size(viewConfigs[0].recommendedImageRectWidth,
			                  viewConfigs[0].maxImageRectWidth, growX);
			uint32_t h = size(viewConfigs[0].recommendedImageRectHeight,
			                  viewConfigs[0].maxImageRectHeight, growY);
			{
				unsigned fw = 0, fh = 0;
				const std::string fixed = vr_debugEyeSize;
				if (std::sscanf(fixed.c_str(), "%ux%u", &fw, &fh) == 2 && fw >= 64 && fh >= 64) {
					w = std::min(fw, viewConfigs[0].maxImageRectWidth) & ~1U;
					h = std::min(fh, viewConfigs[0].maxImageRectHeight) & ~1U;
				}
			}

			std::vector<int64_t> formats = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB};
			// Both eyes in one two-layer image when they are drawn at once, else
			// an image each
			if (eyesLayered) {
				DestroySwapchain(eyeSwapchains[1]);
				if (!CreateSwapchain(eyeSwapchains[0], w, h, formats, 2, eyesFoveated))
					return;
			} else {
				for (int i = 0; i < 2; i++) {
					if (!CreateSwapchain(eyeSwapchains[i], w, h, formats))
						return;
				}
			}
			eyeSwapchainGeneration++;
			appliedFoveation = -1;
			if (!eyeSwapchains[0].densityImages.empty())
				SPLog("[OpenXR] Eye density maps %ux%u, from the runtime%s",
				      eyeSwapchains[0].densityWidth, eyeSwapchains[0].densityHeight,
				      eyeSwapchains[0].subsampled ? "; the eye images subsampled" : "");
			frameState.eyeWidth = static_cast<int>(w);
			frameState.eyeHeight = static_cast<int>(h);
			if (symmetricFrustum)
				SPLog("[OpenXR] Eye swapchains %ux%u (symmetric frustum, x%.2f/x%.2f over the "
				      "runtime's)",
				      w, h, growX, growY);
			else
				SPLog("[OpenXR] Eye swapchains %ux%u (the runtime's own frustum)", w, h);
		}

		void OpenXRSystem::HandleSessionStateChange(XrSessionState state) {
			sessionState = state;
			switch (state) {
				case XR_SESSION_STATE_READY: {
					XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
					bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					if (Check(xrBeginSession(session, &bi), "xrBeginSession")) {
						sessionRunning = true;
						appliedPerformanceBoost = -1;
						// Not yet: a session can start with the headset on the desk.
						// Focus means someone is in the app.
						recenterOnFocus = true;
						SPLog("[OpenXR] Session running");
					}
					break;
				}
				case XR_SESSION_STATE_FOCUSED:
					if (recenterOnFocus)
						recenterRequested = true;
					recenterOnFocus = false;
					break;
				case XR_SESSION_STATE_STOPPING:
					Check(xrEndSession(session), "xrEndSession");
					sessionRunning = false;
					SPLog("[OpenXR] Session stopped");
					break;
				case XR_SESSION_STATE_EXITING:
					// The user quit from the runtime's own menu.
					sessionRunning = false;
					quitRequested = true;
					break;
				case XR_SESSION_STATE_LOSS_PENDING:
					sessionRunning = false;
					SPLog("[OpenXR] Session lost; continuing on the monitor");
					break;
				default: break;
			}
		}

		void OpenXRSystem::ApplyRefreshRate() {
			// The rate asked for, or the nearest the headset offers under it
			// (over it if none is under). 0, until asked otherwise, is the
			// headset's own; asked back to 0, its lowest. The frame's time
			// (displayPeriod, and so Hold The Frame Rate's budget) follows.
			if (!pfnRequestDisplayRefreshRate || !pfnEnumerateDisplayRefreshRates ||
			    session == XR_NULL_HANDLE)
				return;
			const int want = std::max(0, (int)vr_refreshRate);
			if (want == appliedRefreshRate || (appliedRefreshRate < 0 && want == 0)) {
				appliedRefreshRate = want;
				return;
			}
			uint32_t n = 0;
			if (XR_FAILED(pfnEnumerateDisplayRefreshRates(session, 0, &n, nullptr)) || n == 0) {
				appliedRefreshRate = want;
				return;
			}
			std::vector<float> rates(n);
			if (XR_FAILED(pfnEnumerateDisplayRefreshRates(session, n, &n, rates.data()))) {
				appliedRefreshRate = want;
				return;
			}
			std::sort(rates.begin(), rates.end());
			float pick = rates.front();
			if (want > 0) {
				for (float r : rates)
					if (r <= static_cast<float>(want) + 0.5F)
						pick = r;
			}
			const bool ok = Check(pfnRequestDisplayRefreshRate(session, pick),
			                      "xrRequestDisplayRefreshRateFB");
			appliedRefreshRate = want;
			SPLog("[OpenXR] Refresh rate %.0f Hz asked for (wanted %d)%s", pick, want,
			      ok ? "" : " (refused)");
		}

		void OpenXRSystem::ApplyPerformanceLevels() {
			if (!pfnPerfSettingsSetPerformanceLevel)
				return;
			const int want = (int)vr_performanceBoost != 0 ? 1 : 0;
			// Off from the start leaves the runtime its own choice; off after on
			// goes back to the sustained low level.
			if (want == appliedPerformanceBoost || (appliedPerformanceBoost < 0 && want == 0)) {
				appliedPerformanceBoost = want;
				return;
			}
			const XrPerfSettingsLevelEXT level = want ? XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT
			                                          : XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT;
			bool ok = true;
			for (XrPerfSettingsDomainEXT domain :
			     {XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_DOMAIN_GPU_EXT})
				ok &= Check(pfnPerfSettingsSetPerformanceLevel(session, domain, level),
				            "xrPerfSettingsSetPerformanceLevelEXT");
			appliedPerformanceBoost = want;
			SPLog("[OpenXR] CPU and GPU levels: sustained %s%s", want ? "high" : "low",
			      ok ? "" : " (refused)");
		}

		void OpenXRSystem::PollEvents() {
			if (instance == XR_NULL_HANDLE)
				return;
			for (;;) {
				XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
				XrResult r = xrPollEvent(instance, &ev);
				if (r != XR_SUCCESS)
					break;
				switch (ev.type) {
					case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
						const auto& e = *reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
						if (e.session == session)
							HandleSessionStateChange(e.state);
						break;
					}
					case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
						sessionRunning = false;
						SPLog("[OpenXR] Runtime is going away");
						break;
					case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
						// The user recentred from the runtime's menu.
						recenterRequested = true;
						break;
					case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
						for (int h = 0; h < 2; h++) {
							XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
							if (XR_FAILED(xrGetCurrentInteractionProfile(session, handPaths[h], &ps)) ||
							    ps.interactionProfile == XR_NULL_PATH)
								continue;
							char buf[XR_MAX_PATH_LENGTH];
							uint32_t len = 0;
							if (XR_SUCCEEDED(xrPathToString(instance, ps.interactionProfile,
							                                sizeof(buf), &len, buf))) {
								SPLog("[OpenXR] %s hand: %s", h ? "Right" : "Left", buf);
								// For the Controls slides (VRControlsWindow).
								if (h == client::VRToolHand() || controllerProfile.empty())
									controllerProfile = buf;
							}
						}
						break;
					}
					default: break;
				}
			}
		}

		void OpenXRSystem::BeginFrame() {
			frameState.running = false;
#ifdef __ANDROID__
			// No session (the app is in the background): nothing paces the loop,
			// and with no window to vsync on it would spin at full speed.
			if (!sessionRunning)
				SDL_Delay(50);
#endif
			if (!sessionRunning || frameBegun)
				return;

			XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
			XrFrameState fs{XR_TYPE_FRAME_STATE};
			if (!Check(xrWaitFrame(session, &wi, &fs), "xrWaitFrame"))
				return;
			XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
			if (!Check(xrBeginFrame(session, &bi), "xrBeginFrame"))
				return;

			frameBegun = true;
			ApplyPerformanceLevels();
			ApplyRefreshRate();
			predictedDisplayTime = fs.predictedDisplayTime;
			if (fs.predictedDisplayPeriod > 0)
				frameState.displayPeriod = static_cast<float>(fs.predictedDisplayPeriod) * 1.0E-9F;
			frameState.running = true;
			frameState.shouldRender = fs.shouldRender == XR_TRUE;
			frameState.focused = sessionState == XR_SESSION_STATE_FOCUSED;
			frameState.hudDistance = std::max(0.3F, (float)vr_hudDistance);
			frameState.hudWidth = std::max(0.2F, (float)vr_hudWidth);

			LocateViews();
			UpdateInput();
			UpdateHandTracking();
			UpdateTrace();
			ApplyBindings();
			PlaceHudPanel();

			frameState.wristWidth = std::max(0.08F, (float)vr_wristWidth);
			const int wristHand = 1 - client::VRToolHand();
			frameState.wristShown =
			  (int)vr_wristHud != 0 &&
			  client::VRPlaceWristPanel(frameState, wristHand, frameState.wristShown,
			                            frameState.wristPanel, frameState.wristExpanded);

			// The other hand brought to it opens the wrist menu: the panel grows
			// (upwards, off the arm) to take buttons a finger can hit.
			frameState.wristExpanded = client::VRWristReached(
			  frameState, client::VRToolHand(), frameState.wristExpanded);
			const float target = frameState.wristExpanded ? 1.0F : 0.0F;
			const float step = frameState.displayPeriod / 0.15F;
			frameState.wristGrow = frameState.wristGrow < target
			                         ? std::min(target, frameState.wristGrow + step)
			                         : std::max(target, frameState.wristGrow - step);
			if (!frameState.wristShown)
				frameState.wristGrow = 0.0F;
			if (frameState.wristGrow > 0.0F) {
				const float g = frameState.wristGrow * frameState.wristGrow *
				                (3.0F - 2.0F * frameState.wristGrow);
				const float grown = frameState.wristWidth * (1.0F + 0.6F * g);
				const float aspect = static_cast<float>(client::kVRWristCanvasHeight) /
				                     static_cast<float>(client::kVRWristCanvasWidth);
				const client::VRPose& p = frameState.wristPanel;
				frameState.wristPanel.position =
				  p.position + p.Axis(1) * ((grown - frameState.wristWidth) * aspect * 0.5F);
				frameState.wristWidth = grown;
			}

			// The big map held in the off hand: the wrist panel, in that hand,
			// wider, and no wrist menu meanwhile.
			if (heldMapWanted &&
			    client::VRPlaceHeldMap(frameState, wristHand, frameState.wristPanel)) {
				frameState.wristShown = true;
				frameState.wristExpanded = false;
				frameState.wristGrow = 0.0F;
				frameState.wristWidth = client::kVRHeldMapWidth;
			}

			// The keyboard standing on its own takes the wrist panel while it is
			// up (typing has no use for the HUD). Asked for each frame.
			frameState.keyboardPanel = keyboardWanted && keyboardPose.valid;
			if (frameState.keyboardPanel) {
				frameState.wristShown = true;
				frameState.wristExpanded = false;
				frameState.wristGrow = 0.0F;
				frameState.wristPanel = keyboardPose;
				frameState.wristWidth = keyboardWidth;
			}
			keyboardWanted = false;
		}

		void OpenXRSystem::LocateViews() {
			XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
			li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			li.displayTime = predictedDisplayTime;
			li.space = appSpace;
			XrViewState vs{XR_TYPE_VIEW_STATE};
			uint32_t count = 0;
			if (!Check(xrLocateViews(session, &li, &vs, 2, &count, views), "xrLocateViews") ||
			    count != 2 || !PoseValid(vs.viewStateFlags)) {
				// Nothing sensible can be drawn without eye poses.
				frameState.shouldRender = false;
				frameState.head.valid = false;
				return;
			}

			for (int i = 0; i < 2; i++) {
				frameState.eyes[i] = ToPose(views[i].pose, true);
				const XrFovf& f = views[i].fov;
				float tx = std::max(std::tan(-f.angleLeft), std::tan(f.angleRight));
				float ty = std::max(std::tan(f.angleUp), std::tan(-f.angleDown));
				frameState.eyeTanX[i] = tx;
				frameState.eyeTanY[i] = ty;
				if (symmetricFrustum) {
					renderFov[i].angleLeft = -std::atan(tx);
					renderFov[i].angleRight = std::atan(tx);
					renderFov[i].angleUp = std::atan(ty);
					renderFov[i].angleDown = -std::atan(ty);
				} else {
					renderFov[i] = f;
				}
				// What is drawn is what is submitted.
				frameState.eyeLeft[i] = std::tan(renderFov[i].angleLeft);
				frameState.eyeRight[i] = std::tan(renderFov[i].angleRight);
				frameState.eyeDown[i] = std::tan(renderFov[i].angleDown);
				frameState.eyeUp[i] = std::tan(renderFov[i].angleUp);
				frameState.eyeAsymmetric = !symmetricFrustum;
			}

			XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
			if (XR_SUCCEEDED(xrLocateSpace(viewSpace, appSpace, predictedDisplayTime, &head)) &&
			    PoseValid(head.locationFlags)) {
				frameState.head = ToPose(head.pose, true);
			} else {
				// Midway between the eyes is as good a head as any.
				frameState.head = frameState.eyes[0];
				frameState.head.position =
				  (frameState.eyes[0].position + frameState.eyes[1].position) * 0.5F;
			}

			// vr_debugFixedLook: a fixed head direction for tests.
			{
				const std::string look = vr_debugFixedLook;
				float yawDeg = 0.0F, pitchDeg = 0.0F;
				if (!look.empty() && std::sscanf(look.c_str(), "%f %f", &yawDeg, &pitchDeg) >= 1) {
					const float y = yawDeg * 0.0174532925F * 0.5F, p = pitchDeg * 0.0174532925F * 0.5F;
					const float cy = std::cos(y), sy = std::sin(y), cp = std::cos(p), sp = std::sin(p);
					// Yaw about tracking +Y, then pitch about the head's +X.
					const client::VRQuat q(cy * sp, sy * cp, -sy * sp, cy * cp);
					const float halfIpd =
					  (frameState.eyes[1].position - frameState.eyes[0].position).GetLength() * 0.5F;
					const Vector3 right = MakeVector3(std::cos(2.0F * y), 0.0F, -std::sin(2.0F * y));
					frameState.head.orientation = q;
					for (int i = 0; i < 2; i++) {
						frameState.eyes[i].orientation = q;
						frameState.eyes[i].position =
						  frameState.head.position + right * (i == 0 ? -halfIpd : halfIpd);
					}
				}
			}

			if (eyeSwapchains[0].handle == XR_NULL_HANDLE)
				CreateEyeSwapchains();
			if (eyeSwapchains[0].handle == XR_NULL_HANDLE)
				frameState.shouldRender = false;
			else
				ApplyFoveation();
		}

		void OpenXRSystem::PlaceHudPanel() {
			// The panel floats in front of you and trails the head, as menus and
			// popups do in other headset games, rather than being glued to the
			// face (testers: the popups were huge and in the way) or left standing
			// in the room. It stays put while you look about it, then, once the
			// head is past the dead zone, eases back in front (about a fifth of a
			// second). It turns only about you, level at eye height, never up or
			// down with the head (testers: menus started high up). A menu's dead
			// zone is half the panel's width and
			// a little more, so pointing at its edges does not move it, and it
			// does not set off while a trigger is held (a slider being dragged).
			// In a game (SetHudFollowsHead) the panel holds popups, so its dead
			// zone is small. Names and markers are drawn into each eye, so nothing
			// needs the panel square in front.
			const client::VRPose& head = frameState.head;
			const bool game = hudFollowAsked;
			hudFollowAsked = false;
			const float aside = hudLookAside;
			hudLookAside = 0.0F;
			const float d = frameState.hudDistance;
			const Vector3 ahead = head.orientation.Rotate(MakeVector3(0.0F, 0.0F, -1.0F));
			if (!head.valid) {
				frameState.hudAnchored = false;
				frameState.hudPanel.position = head.position + ahead * d;
				frameState.hudPanel.orientation = head.orientation;
				frameState.hudPanel.valid = false;
				hudTrailValid = false;
				return;
			}
			// Where the head faces, level; looking straight up or down has no
			// heading, so the panel stays where it is.
			Vector3 level = MakeVector3(ahead.x, 0.0F, ahead.z);
			const bool hasHeading = level.GetLength() > 0.2F;
			level = hasHeading ? level.Normalize() : hudTrailDir;
			if (!hudTrailValid) {
				hudTrailDir = hasHeading ? level : MakeVector3(0.0F, 0.0F, -1.0F);
				hudTrailValid = true;
				hudTrailMoving = false;
			}
			const float deg = 0.0174532925F;
			const float dead =
			  game ? 12.0F * deg : std::atan(frameState.hudWidth * 0.5F / d) + 5.0F * deg;
			// Any trigger held, bound or not, or a hand's pinch (what the menu
			// pointer clicks with)
			bool dragging = false;
			for (int i = 0; i < 2 && !game; i++)
				dragging = dragging || frameState.hands[i].trigger > 0.5F ||
				           frameState.rawHands[i].trigger > 0.5F ||
				           frameState.handPinch[i] > 0.5F;
			const float off = std::acos(Clamp(Vector3::Dot(hudTrailDir, level), -1.0F, 1.0F));
			// Turned towards what stands beside the panel (the body mirror), as
			// far as it and its width more: the panel waits, so it can be looked
			// at (testers: the mirror went round with the head, only glimpsed).
			// The cross product's y is + for a turn to the left.
			float deadHere = dead;
			if (aside != 0.0F && !game) {
				const float turn = hudTrailDir.z * level.x - hudTrailDir.x * level.z;
				if ((turn > 0.0F) == (aside > 0.0F))
					deadHere = std::max(dead, std::fabs(aside) + 35.0F * deg);
			}
			if (off > deadHere && !dragging && hasHeading)
				hudTrailMoving = true;
			if (hudTrailMoving) {
				const float k = 1.0F - std::exp(-std::max(0.0F, frameState.displayPeriod) * 5.0F);
				Vector3 next = hudTrailDir + (level - hudTrailDir) * k;
				next.y = 0.0F;
				if (next.GetLength() > 1.0E-3F)
					hudTrailDir = next.Normalize();
				if (off < 1.5F * deg)
					hudTrailMoving = false;
			}
			// Upright, level, facing the head: a turn about +Y.
			const Vector3 f = hudTrailDir;
			const float yaw = std::atan2(-f.x, -f.z);
			frameState.hudAnchored = true;
			frameState.hudPanel.position = head.position + f * d;
			frameState.hudPanel.orientation =
			  client::VRQuat::FromAxisAngle(MakeVector3(0.0F, 1.0F, 0.0F), yaw);
			frameState.hudPanel.valid = true;
		}

		void OpenXRSystem::SetHeldMap(bool held) {
			if (held != heldMapWanted)
				SPLog("[VR] Big map %s", held ? "held in the off hand" : "put away");
			heldMapWanted = held;
		}

		void OpenXRSystem::SetKeyboardPanel(bool wanted, const client::VRPose& pose, float width) {
			keyboardWanted = wanted;
			keyboardPose = pose;
			keyboardWidth = std::max(0.1F, width);
		}

		void OpenXRSystem::UpdateTrace() {
			if (!traceStarted) {
				traceStarted = true;
				const std::string rec = vr_traceRecord, rep = vr_traceReplay;
				if (!rep.empty()) {
					if (std::FILE* f = std::fopen(rep.c_str(), "rb")) {
						std::string line;
						bool header = true;
						for (int ch; (ch = std::fgetc(f)) != EOF;) {
							if (ch != '\n') {
								if (ch != '\r')
									line += static_cast<char>(ch);
								continue;
							}
							if (header ? line != client::kVRTraceHeader : !line.empty())
								traceIn.push_back(line);
							header = false;
							line.clear();
						}
						if (!line.empty())
							traceIn.push_back(line);
						std::fclose(f);
						SPLog("[VR] Replaying %d frames of input from %s", (int)traceIn.size(),
						      rep.c_str());
					} else {
						SPLog("[VR] vr_traceReplay: cannot open %s", rep.c_str());
					}
				}
				if (!rec.empty()) {
					traceOut = std::fopen(rec.c_str(), "wb");
					if (traceOut) {
						std::fprintf(traceOut, "%s\n", client::kVRTraceHeader);
						SPLog("[VR] Recording input to %s", rec.c_str());
					} else {
						SPLog("[VR] vr_traceRecord: cannot write %s", rec.c_str());
					}
				}
			}
			if (!traceIn.empty()) {
				const size_t i = std::min(traceFrame, traceIn.size() - 1);
				if (!client::VRTraceParse(traceIn[i], frameState))
					SPLog("[VR] Trace line %d is not a whole frame; skipped", (int)i + 2);
				if (traceFrame + 1 == traceIn.size())
					SPLog("[VR] Trace replay reached its last frame (%d)", (int)traceIn.size());
				traceFrame++;
			}
			if (traceOut) {
				std::fprintf(traceOut, "%s\n", client::VRTraceFormat(frameState).c_str());
				std::fflush(traceOut); // a test that kills the game keeps every frame
				if (traceIn.empty())
					traceFrame++;
			}
		}

		void OpenXRSystem::UpdateInput() {
			XrActiveActionSet active{actionSet, XR_NULL_PATH};
			XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
			si.countActiveActionSets = 1;
			si.activeActionSets = &active;
			XrResult sr = xrSyncActions(session, &si);
			bool haveInput = sr == XR_SUCCESS; // XR_SESSION_NOT_FOCUSED is a success code

			for (int h = 0; h < 2; h++) {
				client::VRControllerState& c = frameState.hands[h];
				c = client::VRControllerState();

				XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
				gi.subactionPath = handPaths[h];

				XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};
				gi.action = gripPoseAction;
				c.active = XR_SUCCEEDED(xrGetActionStatePose(session, &gi, &ps)) &&
				           ps.isActive;

				XrSpaceVelocity vel{XR_TYPE_SPACE_VELOCITY};
				XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
				loc.next = &vel;
				if (XR_SUCCEEDED(xrLocateSpace(gripSpaces[h], appSpace, predictedDisplayTime,
				                               &loc))) {
					c.grip = ToPose(loc.pose, PoseValid(loc.locationFlags));
					if (vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
						c.linearVelocity = MakeVector3(vel.linearVelocity.x,
						                               vel.linearVelocity.y,
						                               vel.linearVelocity.z);
						c.velocityValid = true;
					}
				}
				XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
				if (XR_SUCCEEDED(xrLocateSpace(aimSpaces[h], appSpace, predictedDisplayTime,
				                               &aim)))
					c.aim = ToPose(aim.pose, PoseValid(aim.locationFlags));

				if (!haveInput)
					continue;

				auto getFloat = [&](XrAction a) {
					XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
					gi.action = a;
					if (XR_SUCCEEDED(xrGetActionStateFloat(session, &gi, &s)) && s.isActive)
						return s.currentState;
					return 0.0F;
				};
				auto getBool = [&](XrAction a) {
					XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
					gi.action = a;
					return XR_SUCCEEDED(xrGetActionStateBoolean(session, &gi, &s)) &&
					       s.isActive && s.currentState;
				};
				c.trigger = getFloat(triggerAction);
				c.squeeze = getFloat(squeezeAction);
				{
					XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};
					gi.action = thumbstickAction;
					if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &gi, &s)) && s.isActive)
						c.thumbstick = MakeVector2(s.currentState.x, s.currentState.y);
				}
				c.thumbstickClick = getBool(thumbstickClickAction);
				c.buttonPrimary = getBool(primaryAction);
				c.buttonSecondary = getBool(secondaryAction);
				c.menu = getBool(menuAction);
			}
		}

		void OpenXRSystem::ApplyBindings() {
			// After the trace, so a trace holds the controllers as they were
			// pressed and replays under whatever bindings are set then.
			const std::string text = vr_bindings;
			if (text != bindingsText) {
				bindingsText = text;
				bindings = client::VRParseBindings(text);
				SPLog("[VR] Button bindings: %s", text.empty() ? "the default layout" : text.c_str());
			}
			frameState.rawHands[0] = frameState.hands[0];
			frameState.rawHands[1] = frameState.hands[1];
			client::VRApplyBindings(frameState.rawHands, frameState.hands, client::VRToolHand(),
			                        bindings, (bool)vr_swapSticks);
		}

		void OpenXRSystem::CreateHandTrackers() {
#ifdef XR_EXT_hand_tracking
			if (!(bool)vr_handTracking || !pfnCreateHandTracker || session == XR_NULL_HANDLE)
				return;
			for (int h = 0; h < 2; h++) {
				XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
				ci.hand = h == client::VRHandLeft ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
				ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
				if (XR_FAILED(pfnCreateHandTracker(session, &ci, &handTrackers[h]))) {
					handTrackers[h] = XR_NULL_HANDLE;
					SPLog("[OpenXR] Hand tracking: no tracker for the %s hand",
					      h == client::VRHandLeft ? "left" : "right");
				}
			}
			if (handTrackers[0] != XR_NULL_HANDLE || handTrackers[1] != XR_NULL_HANDLE)
				SPLog("[OpenXR] Hand tracking for the menus (%s)",
				      caps[XrCap::HandTrackingAim] ? "the runtime's aim and pinch"
				                                   : "the palm, and thumb to finger");
#endif
		}

		void OpenXRSystem::DestroyHandTrackers() {
#ifdef XR_EXT_hand_tracking
			for (auto& t : handTrackers) {
				if (t != XR_NULL_HANDLE && pfnDestroyHandTracker)
					pfnDestroyHandTracker(t);
				t = XR_NULL_HANDLE;
			}
#endif
		}

		void OpenXRSystem::UpdateHandTracking() {
			for (int h = 0; h < 2; h++) {
				frameState.handTracked[h] = false;
				frameState.handPinch[h] = 0.0F;
				frameState.handMenu[h] = false;
			}
#ifdef XR_EXT_hand_tracking
			if (!pfnLocateHandJoints || appSpace == XR_NULL_HANDLE)
				return;
			for (int h = 0; h < 2; h++) {
				if (handTrackers[h] == XR_NULL_HANDLE)
					continue;
				XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
				li.baseSpace = appSpace;
				li.time = predictedDisplayTime;
				XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
				XrHandJointLocationsEXT locs{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
				locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
				locs.jointLocations = joints;
#ifdef XR_FB_hand_tracking_aim
				XrHandTrackingAimStateFB aim{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
				if (caps[XrCap::HandTrackingAim])
					locs.next = &aim;
#endif
				if (XR_FAILED(pfnLocateHandJoints(handTrackers[h], &li, &locs)) || !locs.isActive)
					continue;
#ifdef XR_FB_hand_tracking_aim
				if (caps[XrCap::HandTrackingAim] && (aim.status & XR_HAND_TRACKING_AIM_VALID_BIT_FB)) {
					frameState.handAim[h] = ToPose(aim.aimPose, true);
					frameState.handPinch[h] = aim.pinchStrengthIndex;
					frameState.handMenu[h] = (aim.status & XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB) != 0;
					frameState.handTracked[h] = true;
					continue;
				}
#endif
				// Without the runtime's aim: the palm points (its -Z runs along the
				// fingers), and the thumb's tip to the index finger's is the pinch,
				// full at 1cm, none from 4cm.
				const XrHandJointLocationEXT& palm = joints[XR_HAND_JOINT_PALM_EXT];
				const XrHandJointLocationEXT& thumb = joints[XR_HAND_JOINT_THUMB_TIP_EXT];
				const XrHandJointLocationEXT& index = joints[XR_HAND_JOINT_INDEX_TIP_EXT];
				if (!PoseValid(palm.locationFlags))
					continue;
				frameState.handAim[h] = ToPose(palm.pose, true);
				if (PoseValid(thumb.locationFlags) && PoseValid(index.locationFlags)) {
					const float dx = thumb.pose.position.x - index.pose.position.x;
					const float dy = thumb.pose.position.y - index.pose.position.y;
					const float dz = thumb.pose.position.z - index.pose.position.z;
					const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
					frameState.handPinch[h] = std::max(0.0F, std::min(1.0F, (0.04F - d) / 0.03F));
				}
				frameState.handTracked[h] = true;
			}
#endif
		}

		void OpenXRSystem::SetEyeLayout(bool layered, bool foveated) {
			eyesLayered = layered;
			eyesFoveated = layered && foveated && CanFoveate();
		}

		bool OpenXRSystem::CanFoveate() const {
			return caps[XrCap::Foveation] && caps[XrCap::FoveationConfiguration] &&
			       caps[XrCap::FoveationVulkan] && caps[XrCap::SwapchainUpdateState];
		}

		bool OpenXRSystem::WillSubsampleEyes() const {
			return eyesFoveated && (bool)vr_foveationSubsampled &&
			       caps[XrCap::VulkanSwapchainCreateInfo];
		}

		VkFormat OpenXRSystem::GetPlannedEyeFormat() const {
			for (int64_t want : {(int64_t)VK_FORMAT_R8G8B8A8_SRGB, (int64_t)VK_FORMAT_B8G8R8A8_SRGB})
				if (std::find(runtimeFormats.begin(), runtimeFormats.end(), want) !=
				    runtimeFormats.end())
					return static_cast<VkFormat>(want);
			return runtimeFormats.empty() ? VK_FORMAT_R8G8B8A8_SRGB
			                              : static_cast<VkFormat>(runtimeFormats[0]);
		}

		void OpenXRSystem::ApplyFoveation() {
			const XRSwapchain& sc = eyeSwapchains[0];
			if (sc.handle == XR_NULL_HANDLE || sc.densityImages.empty())
				return;
			const int level = std::max(0, std::min(4, (int)vr_foveation));
			const bool dynamic = level > 0 && (bool)vr_foveationDynamic;
			const int want = level + (dynamic ? 8 : 0);
			if (want == appliedFoveation)
				return;
			appliedFoveation = want;
			if (!pfnCreateFoveationProfile) {
				xrGetInstanceProcAddr(instance, "xrCreateFoveationProfileFB",
				                      reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateFoveationProfile));
				xrGetInstanceProcAddr(instance, "xrDestroyFoveationProfileFB",
				                      reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyFoveationProfile));
				xrGetInstanceProcAddr(instance, "xrUpdateSwapchainFB",
				                      reinterpret_cast<PFN_xrVoidFunction*>(&pfnUpdateSwapchain));
			}
			if (!pfnCreateFoveationProfile || !pfnDestroyFoveationProfile || !pfnUpdateSwapchain)
				return;
			// High top: Meta's XR_FOVEATION_LEVEL_HIGH_TOP_FB, not in OpenXR's
			// headers. A runtime that does not know it refuses the profile, and
			// the level before stays.
			static const XrFoveationLevelFB kLevels[] = {
			  XR_FOVEATION_LEVEL_NONE_FB, XR_FOVEATION_LEVEL_LOW_FB, XR_FOVEATION_LEVEL_MEDIUM_FB,
			  XR_FOVEATION_LEVEL_HIGH_FB, static_cast<XrFoveationLevelFB>(4)};
			XrFoveationLevelProfileCreateInfoFB li{XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB};
			li.level = kLevels[level];
			li.verticalOffset = 0.0F;
			li.dynamic = dynamic ? XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB
			                     : XR_FOVEATION_DYNAMIC_DISABLED_FB;
			XrFoveationProfileCreateInfoFB pi{XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB};
			pi.next = &li;
			XrFoveationProfileFB profile = XR_NULL_HANDLE;
			if (!Check(pfnCreateFoveationProfile(session, &pi, &profile),
			           "xrCreateFoveationProfileFB"))
				return;
			XrSwapchainStateFoveationFB state{XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB};
			state.profile = profile;
			Check(pfnUpdateSwapchain(sc.handle,
			                         reinterpret_cast<const XrSwapchainStateBaseHeaderFB*>(&state)),
			      "xrUpdateSwapchainFB");
			// The swapchain keeps what it was given; the old profile can go
			if (foveationProfile != XR_NULL_HANDLE)
				pfnDestroyFoveationProfile(foveationProfile);
			foveationProfile = profile;
			SPLog("[OpenXR] Foveation level %d%s", level, dynamic ? ", dynamic" : "");
		}

		bool OpenXRSystem::AcquireEyeImage(int eye, VkImage& image) {
			XRSwapchain& sc = eyeSwapchains[eyeSwapchains[0].layers > 1 ? 0 : eye];
			if (!frameBegun || sc.handle == XR_NULL_HANDLE)
				return false;
			if (sc.acquired != UINT32_MAX) {
				image = sc.images[sc.acquired];
				return true;
			}
			XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
			uint32_t index = 0;
			if (!Check(xrAcquireSwapchainImage(sc.handle, &ai, &index), "xrAcquireSwapchainImage"))
				return false;
			XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
			wi.timeout = XR_INFINITE_DURATION;
			if (!Check(xrWaitSwapchainImage(sc.handle, &wi), "xrWaitSwapchainImage")) {
				XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
				xrReleaseSwapchainImage(sc.handle, &ri);
				return false;
			}
			sc.acquired = index;
			image = sc.images[index];
			return true;
		}

		void OpenXRSystem::ReleaseEyeImage(int eye) {
			XRSwapchain& sc = eyeSwapchains[eye];
			if (sc.acquired == UINT32_MAX)
				return;
			XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			Check(xrReleaseSwapchainImage(sc.handle, &ri), "xrReleaseSwapchainImage");
			sc.acquired = UINT32_MAX;
		}

		bool OpenXRSystem::AcquirePanelImage(XRSwapchain& sc, uint32_t width, uint32_t height,
		                                     VkImage& image, const char* what) {
			if (!frameBegun || width == 0 || height == 0)
				return false;
			if (sc.handle == XR_NULL_HANDLE || sc.width != width || sc.height != height) {
				std::vector<int64_t> formats = {VK_FORMAT_R8G8B8A8_SRGB,
				                                VK_FORMAT_B8G8R8A8_SRGB};
				if (!CreateSwapchain(sc, width, height, formats))
					return false;
			}
			XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
			uint32_t index = 0;
			if (!Check(xrAcquireSwapchainImage(sc.handle, &ai, &index), what))
				return false;
			XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
			wi.timeout = XR_INFINITE_DURATION;
			if (!Check(xrWaitSwapchainImage(sc.handle, &wi), what)) {
				XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
				xrReleaseSwapchainImage(sc.handle, &ri);
				return false;
			}
			sc.acquired = index;
			image = sc.images[index];
			return true;
		}

		void OpenXRSystem::ReleasePanelImage(XRSwapchain& sc, const char* what) {
			if (sc.acquired == UINT32_MAX)
				return;
			XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			Check(xrReleaseSwapchainImage(sc.handle, &ri), what);
			sc.acquired = UINT32_MAX;
		}

		bool OpenXRSystem::AcquireHudImage(uint32_t width, uint32_t height, VkImage& image) {
			return AcquirePanelImage(hudSwapchain, width, height, image, "HUD image");
		}

		void OpenXRSystem::ReleaseHudImage() { ReleasePanelImage(hudSwapchain, "HUD image"); }

		bool OpenXRSystem::AcquireWristImage(VkImage& image) {
			if (!frameState.wristShown)
				return false;
			return AcquirePanelImage(wristSwapchain, client::kVRWristCanvasWidth,
			                         client::kVRWristCanvasHeight, image, "wrist image");
		}

		void OpenXRSystem::ReleaseWristImage() { ReleasePanelImage(wristSwapchain, "wrist image"); }

		void OpenXRSystem::EndFrame(bool submitEyes, bool submitHud, bool submitWrist) {
			if (!frameBegun)
				return;
			frameBegun = false;
			frameState.running = false;

			// Anything still held would never be released otherwise.
			ReleaseEyeImage(0);
			ReleaseEyeImage(1);
			ReleaseHudImage();
			ReleaseWristImage();

			std::vector<XrCompositionLayerBaseHeader*> layers;

			XrCompositionLayerProjectionView pv[2]{};
			XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
			if (submitEyes && frameState.shouldRender) {
				for (int i = 0; i < 2; i++) {
					pv[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
					pv[i].pose = views[i].pose;
					pv[i].fov = renderFov[i];
					const bool layered = eyeSwapchains[0].layers > 1;
					const XRSwapchain& sc = eyeSwapchains[layered ? 0 : i];
					pv[i].subImage.swapchain = sc.handle;
					pv[i].subImage.imageArrayIndex = layered ? static_cast<uint32_t>(i) : 0U;
					pv[i].subImage.imageRect.offset = {0, 0};
					pv[i].subImage.imageRect.extent = {static_cast<int32_t>(sc.width),
					                                   static_cast<int32_t>(sc.height)};
				}
				proj.space = appSpace;
				proj.viewCount = 2;
				proj.views = pv;
				layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&proj));
			}

			XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
			if (submitHud && frameState.shouldRender && hudSwapchain.handle != XR_NULL_HANDLE) {
				quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
				quad.space = frameState.hudAnchored ? appSpace : viewSpace;
				quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				quad.subImage.swapchain = hudSwapchain.handle;
				quad.subImage.imageRect.offset = {0, 0};
				quad.subImage.imageRect.extent = {static_cast<int32_t>(hudSwapchain.width),
				                                  static_cast<int32_t>(hudSwapchain.height)};
				quad.pose = IdentityPose();
				quad.pose.position.z = -frameState.hudDistance;
				if (frameState.hudAnchored) {
					const client::VRPose& hp = frameState.hudPanel;
					quad.pose.position = {hp.position.x, hp.position.y, hp.position.z};
					quad.pose.orientation = {hp.orientation.x, hp.orientation.y, hp.orientation.z,
					                         hp.orientation.w};
				}
				quad.size.width = frameState.hudWidth;
				quad.size.height = frameState.hudWidth * static_cast<float>(hudSwapchain.height) /
				                   static_cast<float>(hudSwapchain.width);
				layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad));
			}

			// Last, so it is over the front panel: it is the nearer of the two.
			XrCompositionLayerQuad wrist{XR_TYPE_COMPOSITION_LAYER_QUAD};
			if (submitWrist && frameState.shouldRender && frameState.wristShown &&
			    wristSwapchain.handle != XR_NULL_HANDLE) {
				const client::VRPose& p = frameState.wristPanel;
				wrist.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
				wrist.space = appSpace;
				wrist.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				wrist.subImage.swapchain = wristSwapchain.handle;
				wrist.subImage.imageRect.offset = {0, 0};
				wrist.subImage.imageRect.extent = {static_cast<int32_t>(wristSwapchain.width),
				                                   static_cast<int32_t>(wristSwapchain.height)};
				wrist.pose.position = {p.position.x, p.position.y, p.position.z};
				wrist.pose.orientation = {p.orientation.x, p.orientation.y, p.orientation.z,
				                          p.orientation.w};
				wrist.size.width = frameState.wristWidth;
				wrist.size.height = frameState.wristWidth *
				                    static_cast<float>(wristSwapchain.height) /
				                    static_cast<float>(wristSwapchain.width);
				layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&wrist));
			}

			XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
			ei.displayTime = predictedDisplayTime;
			ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
			ei.layerCount = static_cast<uint32_t>(layers.size());
			ei.layers = layers.data();
			Check(xrEndFrame(session, &ei), "xrEndFrame");
		}

		void OpenXRSystem::Vibrate(int hand, float amplitude, float seconds) {
			if (session == XR_NULL_HANDLE || !frameState.focused || hand < 0 || hand > 1)
				return;
			XrHapticVibration vib{XR_TYPE_HAPTIC_VIBRATION};
			vib.amplitude = std::max(0.0F, std::min(amplitude, 1.0F));
			vib.duration = static_cast<XrDuration>(seconds * 1.0E9);
			vib.frequency = XR_FREQUENCY_UNSPECIFIED;
			XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
			hi.action = hapticAction;
			hi.subactionPath = handPaths[hand];
			xrApplyHapticFeedback(session, &hi, reinterpret_cast<XrHapticBaseHeader*>(&vib));
		}

		void OpenXRSystem::Vibrate(int hand, client::VRHapticShape shape, float amplitude,
		                           float seconds) {
			// Without envelopes (or for a flat one), the plain rumble it always was.
			if (!caps[XrCap::HapticAmplitudeEnvelope] || shape == client::VRHapticShape::Flat) {
				Vibrate(hand, amplitude, seconds);
				return;
			}
			if (session == XR_NULL_HANDLE || !frameState.focused || hand < 0 || hand > 1)
				return;
#ifndef XR_FB_haptic_amplitude_envelope
			// Headers older than the extension (Ubuntu 24.04's): the plain rumble
			Vibrate(hand, amplitude, seconds);
#else
			float amps[XR_MAX_HAPTIC_AMPLITUDE_ENVELOPE_SAMPLES_FB];
			const int n = client::VRHapticEnvelope(shape, amplitude, seconds, amps,
			                                       (int)XR_MAX_HAPTIC_AMPLITUDE_ENVELOPE_SAMPLES_FB);
			XrHapticAmplitudeEnvelopeVibrationFB env{XR_TYPE_HAPTIC_AMPLITUDE_ENVELOPE_VIBRATION_FB};
			env.duration = static_cast<XrDuration>(seconds * 1.0E9);
			env.amplitudeCount = static_cast<uint32_t>(n);
			env.amplitudes = amps;
			XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
			hi.action = hapticAction;
			hi.subactionPath = handPaths[hand];
			xrApplyHapticFeedback(session, &hi, reinterpret_cast<XrHapticBaseHeader*>(&env));
#endif
		}
	} // namespace vr
} // namespace spades

#endif // USE_OPENXR
