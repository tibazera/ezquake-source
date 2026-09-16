// Vulkan renderer: real NVIDIA DLSS integration via the Streamline SDK
// (v2.14.1, see external/streamline/), loaded dynamically at runtime --
// sl.interposer.dll and its plugin DLLs (sl.dlss.dll, sl.common.dll) ship
// alongside the exe (see external/streamline/bin/) but are only actually
// usable on an RTX 50-series+ GPU with a current NVIDIA driver; every
// entry point here fails gracefully (returns false / no-ops) when the DLL
// isn't present or the feature isn't supported, falling back to the
// existing FSR2-style spatial/temporal upscaler in vk_upscale.c.
//
// This project's rendering code is C, but Streamline's public headers are
// C++-only (SL_STRUCT_BEGIN generates classes inheriting sl::BaseStructure,
// one struct -- sl::FrameToken -- has a virtual member function). Rather
// than compile a C++ shim into this otherwise-C codebase, vk_dlss_types.h
// hand-mirrors every struct's exact memory layout as plain C structs (see
// its own header comment for the ABI reasoning), and this file calls the
// DLL's exported/feature-loaded functions directly through those layouts.
// sl::FrameToken specifically is never constructed here -- only ever held
// as the opaque pointer slGetNewFrameToken hands back, exactly as the SDK's
// own documented usage requires.
//
// Manual hooking mode (PreferenceFlag::eUseManualHooking) is used, not the
// global DXGI/Vulkan interception mode -- this engine creates its own
// VkInstance/VkDevice directly (see vk_instance.c/vk_physical_devices.c)
// and manual hooking is what the SDK's own Vulkan guide recommends to
// avoid extra proxy-call overhead in that case (see
// ProgrammingGuideManualHooking.md's Vulkan section). Only the swapchain
// present path needs the sl.interposer proxy per that guide; this
// integration currently does NOT proxy vkQueuePresentKHR (see
// VK_DLSS_Composite's own comment on why that's an acceptable gap for a
// first pass, and what breaks without it).

#include "quakedef.h"
#include "tr_types.h"
#include "r_matrix.h"
#include "vk_local.h"
#include "vk_dlss_types.h"

#include <windows.h>
#include <vulkan/vulkan.h>

extern cvar_t vid_vulkan_upscaler;

// ---- Dynamically loaded entry points ----------------------------------

typedef sl_result_t (*PFN_slInit)(const sl_preferences_t* pref, uint64_t sdkVersion);
typedef sl_result_t (*PFN_slShutdown)(void);
typedef sl_result_t (*PFN_slIsFeatureSupported)(sl_feature_t feature, const sl_adapter_info_t* adapterInfo);
typedef sl_result_t (*PFN_slSetVulkanInfo)(const sl_vulkan_info_t* info);
typedef sl_result_t (*PFN_slSetTagForFrame)(const void* frame, const sl_viewport_handle_t* viewport, const sl_resource_tag_t* resources, uint32_t numResources, void* cmdBuffer);
typedef sl_result_t (*PFN_slSetConstants)(const sl_constants_t* values, const void* frame, const sl_viewport_handle_t* viewport);
typedef sl_result_t (*PFN_slAllocateResources)(void* cmdBuffer, sl_feature_t feature, const sl_viewport_handle_t* viewport);
typedef sl_result_t (*PFN_slFreeResources)(sl_feature_t feature, const sl_viewport_handle_t* viewport);
typedef sl_result_t (*PFN_slEvaluateFeature)(sl_feature_t feature, const void* frame, const sl_base_structure_t** inputs, uint32_t numInputs, void* cmdBuffer);
typedef sl_result_t (*PFN_slGetFeatureFunction)(sl_feature_t feature, const char* functionName, void** function);
typedef sl_result_t (*PFN_slGetNewFrameToken)(void** token, const uint32_t* frameIndex);

// Feature-loaded (obtained via slGetFeatureFunction, not exported directly
// from the DLL -- see sl_dlss.h's SL_FEATURE_FUN_IMPORT_STATIC pattern).
typedef sl_result_t (*PFN_slDLSSGetOptimalSettings)(const sl_dlss_options_t* options, sl_dlss_optimal_settings_t* settings);
typedef sl_result_t (*PFN_slDLSSSetOptions)(const sl_viewport_handle_t* viewport, const sl_dlss_options_t* options);

static HMODULE vk_dlss_module;
static PFN_slInit p_slInit;
static PFN_slShutdown p_slShutdown;
static PFN_slIsFeatureSupported p_slIsFeatureSupported;
static PFN_slSetVulkanInfo p_slSetVulkanInfo;
static PFN_slSetTagForFrame p_slSetTagForFrame;
static PFN_slSetConstants p_slSetConstants;
static PFN_slAllocateResources p_slAllocateResources;
static PFN_slFreeResources p_slFreeResources;
static PFN_slEvaluateFeature p_slEvaluateFeature;
static PFN_slGetFeatureFunction p_slGetFeatureFunction;
static PFN_slGetNewFrameToken p_slGetNewFrameToken;
static PFN_slDLSSGetOptimalSettings p_slDLSSGetOptimalSettings;
static PFN_slDLSSSetOptions p_slDLSSSetOptions;

// State
static qbool vk_dlss_available;       // DLL loaded, symbols resolved
static qbool vk_dlss_supported;       // slIsFeatureSupported succeeded for this adapter
static qbool vk_dlss_initialized;     // slInit succeeded, device registered
static uint32_t vk_dlss_frameIndex;
static sl_viewport_handle_t vk_dlss_viewport;

qbool VK_DLSS_Available(void)
{
	return vk_dlss_available && vk_dlss_supported;
}

// sl::kSDKVersion equivalent -- from sl_version.h, encoded as
// (major << 48) | (minor << 24) | patch, version 2.14.1 for the SDK this
// was integrated against (external/streamline/, bin/lib copied from the
// matching v2.14.1 GitHub release). If a newer Streamline SDK is ever
// dropped in, bump this to match sl_version.h's actual kSDKVersion --
// slInit checks it and refuses a mismatched build.
#define VK_DLSS_SDK_VERSION (((uint64_t)2 << 48) | ((uint64_t)14 << 24) | (uint64_t)1)

static void VK_DLSS_LogCallback(sl_log_level_t level, const char* msg)
{
	if (level == SL_LOG_LEVEL_OFF) {
		return;
	}
	Con_DPrintf("streamline: %s", msg ? msg : "(null)");
}

// Loads sl.interposer.dll and resolves the handful of core entry points
// this integration needs, but does NOT call slInit yet (that needs the
// Vulkan instance/device to already exist -- see VK_DLSS_SetVulkanInfo,
// called separately after VK_CreateLogicalDevice). Safe to call even when
// the DLL is missing (e.g. non-NVIDIA GPU, or the redistributable wasn't
// deployed) -- just leaves vk_dlss_available false.
qbool VK_DLSS_LoadLibrary(void)
{
	wchar_t path[MAX_PATH];
	wchar_t* lastSlash;
	DWORD pathLen;

	if (vk_dlss_module != NULL) {
		return vk_dlss_available;
	}

	// Load from external/streamline/bin/ next to the exe, not just
	// LoadLibraryW("sl.interposer.dll") relying on the default search path
	// -- this repo's redistribution layout keeps SL binaries in a
	// subdirectory rather than flat next to the exe, mirroring how
	// vk_upscale.c's shader SPIR-V is embedded rather than loose files.
	pathLen = GetModuleFileNameW(NULL, path, MAX_PATH);
	if (pathLen == 0 || pathLen >= MAX_PATH) {
		return false;
	}
	lastSlash = wcsrchr(path, L'\\');
	if (!lastSlash) {
		return false;
	}
	// "\\external\\streamline\\bin\\sl.interposer.dll" fits comfortably
	// under MAX_PATH for any reasonable install path length; if it doesn't,
	// LoadLibraryW below simply fails and vk_dlss_available stays false --
	// same as any other missing-DLL case.
	wcscpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - path), L"external\\streamline\\bin\\sl.interposer.dll");

	vk_dlss_module = LoadLibraryW(path);
	if (!vk_dlss_module) {
		Con_DPrintf("vulkan: DLSS unavailable (sl.interposer.dll not found at %ls)\n", path);
		return false;
	}

	p_slInit = (PFN_slInit)GetProcAddress(vk_dlss_module, "slInit");
	p_slShutdown = (PFN_slShutdown)GetProcAddress(vk_dlss_module, "slShutdown");
	p_slIsFeatureSupported = (PFN_slIsFeatureSupported)GetProcAddress(vk_dlss_module, "slIsFeatureSupported");
	p_slSetVulkanInfo = (PFN_slSetVulkanInfo)GetProcAddress(vk_dlss_module, "slSetVulkanInfo");
	p_slSetTagForFrame = (PFN_slSetTagForFrame)GetProcAddress(vk_dlss_module, "slSetTagForFrame");
	p_slSetConstants = (PFN_slSetConstants)GetProcAddress(vk_dlss_module, "slSetConstants");
	p_slAllocateResources = (PFN_slAllocateResources)GetProcAddress(vk_dlss_module, "slAllocateResources");
	p_slFreeResources = (PFN_slFreeResources)GetProcAddress(vk_dlss_module, "slFreeResources");
	p_slEvaluateFeature = (PFN_slEvaluateFeature)GetProcAddress(vk_dlss_module, "slEvaluateFeature");
	p_slGetFeatureFunction = (PFN_slGetFeatureFunction)GetProcAddress(vk_dlss_module, "slGetFeatureFunction");
	p_slGetNewFrameToken = (PFN_slGetNewFrameToken)GetProcAddress(vk_dlss_module, "slGetNewFrameToken");

	if (!p_slInit || !p_slShutdown || !p_slIsFeatureSupported || !p_slSetVulkanInfo ||
		!p_slSetTagForFrame || !p_slSetConstants || !p_slAllocateResources || !p_slFreeResources ||
		!p_slEvaluateFeature || !p_slGetFeatureFunction || !p_slGetNewFrameToken) {
		Con_Printf("vulkan: DLSS unavailable (sl.interposer.dll missing expected exports -- wrong/corrupt DLL?)\n");
		FreeLibrary(vk_dlss_module);
		vk_dlss_module = NULL;
		return false;
	}

	{
		sl_preferences_t pref;
		sl_feature_t featuresToLoad[1];
		sl_result_t result;

		memset(&pref, 0, sizeof(pref));
		pref.structType.data1 = 0x1ca10965;
		pref.structType.data2 = 0xbf8e;
		pref.structType.data3 = 0x432b;
		{
			static const uint8_t guid4[8] = { 0x8d, 0xa1, 0x67, 0x16, 0xd8, 0x79, 0xfb, 0x14 };
			memcpy(pref.structType.data4, guid4, 8);
		}
		pref.structVersion = 1; // kStructVersion1

		pref.showConsole = 0;
		pref.logLevel = SL_LOG_LEVEL_DEFAULT;
		pref.logMessageCallback = (void*)VK_DLSS_LogCallback;
		pref.flags = SL_PREF_DISABLE_CL_STATE_TRACKING | SL_PREF_ALLOW_OTA | SL_PREF_LOAD_DOWNLOADED_PLUGINS;
		featuresToLoad[0] = SL_FEATURE_DLSS;
		pref.featuresToLoad = featuresToLoad;
		pref.numFeaturesToLoad = 1;
		pref.renderAPI = SL_RENDER_API_VULKAN;

		result = p_slInit(&pref, VK_DLSS_SDK_VERSION);
		if (result != SL_RESULT_OK) {
			Con_Printf("vulkan: DLSS slInit failed (result=%d) -- driver may be too old, or DLSS not supported on this GPU\n", result);
			FreeLibrary(vk_dlss_module);
			vk_dlss_module = NULL;
			return false;
		}
	}

	vk_dlss_available = true;
	Con_Printf("vulkan: DLSS (Streamline SDK) loaded successfully\n");
	return true;
}

// Called once after the Vulkan physical device is chosen (VK_CreatePhysicalDevice)
// but BEFORE the logical device is created -- slIsFeatureSupported needs a
// VkPhysicalDevice, and slSetVulkanInfo (called separately, see
// VK_DLSS_SetVulkanInfo below) needs the info struct's queue family/index
// fields, both of which this engine already knows by that point without
// Streamline's help (it creates its own device, doesn't use the
// vkCreateDevice proxy -- see this file's header comment on manual hooking).
qbool VK_DLSS_CheckSupport(VkPhysicalDevice physicalDevice)
{
	sl_adapter_info_t adapterInfo;
	sl_result_t result;

	if (!vk_dlss_available) {
		return false;
	}

	memset(&adapterInfo, 0, sizeof(adapterInfo));
	adapterInfo.structType.data1 = 0x677315f;
	adapterInfo.structType.data2 = 0xa746;
	adapterInfo.structType.data3 = 0x4492;
	{
		static const uint8_t guid4[8] = { 0x9f, 0x42, 0xcb, 0x61, 0x42, 0xc9, 0xc3, 0xd4 };
		memcpy(adapterInfo.structType.data4, guid4, 8);
	}
	adapterInfo.structVersion = 1;
	adapterInfo.vkPhysicalDevice = physicalDevice;

	result = p_slIsFeatureSupported(SL_FEATURE_DLSS, &adapterInfo);
	vk_dlss_supported = (result == SL_RESULT_OK);
	if (!vk_dlss_supported) {
		Con_DPrintf("vulkan: DLSS not supported on this adapter (result=%d) -- needs RTX 50-series+ and a current driver\n", result);
	}
	return vk_dlss_supported;
}

// Called once after the logical device is created (VK_CreateLogicalDevice)
// -- tells Streamline which device/instance/queues to use. Queue indices
// are this engine's own single graphics queue for both graphics and
// compute roles (this renderer doesn't maintain a separate compute queue);
// Streamline is responsible for not colliding with it as long as the
// indices/families reported here are accurate, per the manual-hooking
// guide's section 5.2.
qbool VK_DLSS_SetVulkanInfo(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t graphicsQueueFamily, uint32_t graphicsQueueIndex)
{
	sl_vulkan_info_t info;
	sl_result_t result;

	if (!vk_dlss_available || !vk_dlss_supported) {
		return false;
	}

	memset(&info, 0, sizeof(info));
	info.structType.data1 = 0xeed6fd5;
	info.structType.data2 = 0x82cd;
	info.structType.data3 = 0x43a9;
	{
		static const uint8_t guid4[8] = { 0xbd, 0xb5, 0x47, 0xa5, 0xba, 0x2f, 0x45, 0xd6 };
		memcpy(info.structType.data4, guid4, 8);
	}
	info.structVersion = 3; // kStructVersion3, matches the real struct's field set

	info.device = device;
	info.instance = instance;
	info.physicalDevice = physicalDevice;
	info.computeQueueIndex = graphicsQueueIndex;
	info.computeQueueFamily = graphicsQueueFamily;
	info.graphicsQueueIndex = graphicsQueueIndex;
	info.graphicsQueueFamily = graphicsQueueFamily;

	result = p_slSetVulkanInfo(&info);
	if (result != SL_RESULT_OK) {
		Con_Printf("vulkan: DLSS slSetVulkanInfo failed (result=%d)\n", result);
		vk_dlss_initialized = false;
		return false;
	}

	vk_dlss_initialized = true;

	memset(&vk_dlss_viewport, 0, sizeof(vk_dlss_viewport));
	vk_dlss_viewport.structType.data1 = 0x171b6435;
	vk_dlss_viewport.structType.data2 = 0x9b3c;
	vk_dlss_viewport.structType.data3 = 0x4fc8;
	{
		static const uint8_t guid4[8] = { 0x99, 0x94, 0xfb, 0xe5, 0x25, 0x69, 0xaa, 0xa4 };
		memcpy(vk_dlss_viewport.structType.data4, guid4, 8);
	}
	vk_dlss_viewport.structVersion = 1;
	vk_dlss_viewport.value = 0; // single viewport, this renderer has no split-screen-via-SL concept

	return true;
}

qbool VK_DLSS_Active(void)
{
	return VK_UpscaleActive() && vid_vulkan_upscaler.integer == 2 && vk_dlss_initialized;
}

// Queries DLSS for the recommended render resolution at the current output
// size -- called from VK_ResolveSceneSize (vk_swapchain.c) as an
// alternative to the FSR2-style vid_vulkan_renderscale-driven calculation,
// when DLSS is the active upscaler. Falls back to false (caller keeps using
// its own renderscale math) if DLSS isn't available/initialized/the query
// fails.
qbool VK_DLSS_GetOptimalRenderSize(uint32_t outputWidth, uint32_t outputHeight, uint32_t* renderWidth, uint32_t* renderHeight)
{
	sl_dlss_options_t options;
	sl_dlss_optimal_settings_t settings;
	sl_result_t result;

	if (!VK_DLSS_Active()) {
		return false;
	}

	if (!p_slDLSSGetOptimalSettings) {
		void* fn = NULL;
		if (p_slGetFeatureFunction(SL_FEATURE_DLSS, "slDLSSGetOptimalSettings", &fn) != SL_RESULT_OK || !fn) {
			return false;
		}
		p_slDLSSGetOptimalSettings = (PFN_slDLSSGetOptimalSettings)fn;
	}
	if (!p_slDLSSSetOptions) {
		void* fn = NULL;
		if (p_slGetFeatureFunction(SL_FEATURE_DLSS, "slDLSSSetOptions", &fn) != SL_RESULT_OK || !fn) {
			return false;
		}
		p_slDLSSSetOptions = (PFN_slDLSSSetOptions)fn;
	}

	memset(&options, 0, sizeof(options));
	options.structType.data1 = 0x6ac826e4;
	options.structType.data2 = 0x4c61;
	options.structType.data3 = 0x4101;
	{
		static const uint8_t guid4[8] = { 0xa9, 0x2d, 0x63, 0x8d, 0x42, 0x10, 0x57, 0xb8 };
		memcpy(options.structType.data4, guid4, 8);
	}
	options.structVersion = 3; // kStructVersion3

	// Balanced is the closest DLSS-native equivalent to this renderer's
	// existing default vid_vulkan_renderscale (0.66-ish quality-mode
	// ratio) -- no separate cvar for DLSS quality mode yet, deliberately
	// simple for this first integration pass.
	options.mode = SL_DLSS_MODE_BALANCED;
	options.outputWidth = outputWidth;
	options.outputHeight = outputHeight;
	options.colorBuffersHDR = SL_FALSE; // this renderer's swapchain format isn't HDR
	options.useAutoExposure = SL_TRUE; // no dedicated exposure buffer tagged yet
	options.dlaaPreset = SL_DLSS_PRESET_K;
	options.qualityPreset = SL_DLSS_PRESET_K;
	options.balancedPreset = SL_DLSS_PRESET_K;
	options.performancePreset = SL_DLSS_PRESET_M;
	options.ultraPerformancePreset = SL_DLSS_PRESET_L;

	result = p_slDLSSSetOptions(&vk_dlss_viewport, &options);
	if (result != SL_RESULT_OK) {
		return false;
	}

	memset(&settings, 0, sizeof(settings));
	result = p_slDLSSGetOptimalSettings(&options, &settings);
	if (result != SL_RESULT_OK || !settings.optimalRenderWidth || !settings.optimalRenderHeight) {
		return false;
	}

	*renderWidth = settings.optimalRenderWidth;
	*renderHeight = settings.optimalRenderHeight;
	return true;
}

// Advances SL's frame token -- call once per real frame, same lifetime
// spot as VK_AdvanceJitter (vk_main.c): both are per-real-frame, not
// per-multiview-pane, state.
void VK_DLSS_AdvanceFrame(void)
{
	if (!vk_dlss_initialized) {
		return;
	}
	++vk_dlss_frameIndex;
}

// Tags the resources DLSS needs (depth, motion vectors, low-res input
// color, native-res output color) and evaluates the feature -- replaces
// VK_UpscaleComposite's EASU+RCAS draw call when VK_DLSS_Active(). Reuses
// the SAME reconstructed motion vectors and depth this project's FSR2-style
// temporal path already computes (see vk_upscale.c/vk_upscale.frag's
// ReprojectToPreviousFrame) -- DLSS doesn't care how the motion-vector
// buffer was produced, only that it's tagged correctly and
// Constants::cameraMotionIncluded is set to reflect that these are camera-
// only (not per-object) vectors, same honesty this project already applies
// to its own FSR2 path (see vk_motion_vectors.frag's header comment on why
// per-object vectors were out of scope).
//
// IMPORTANT GAP (documented, not silently ignored): this does not proxy
// vkQueuePresentKHR through sl.interposer's present hook (see this file's
// header comment on manual hooking scope). Per Streamline's docs this is
// only strictly required for DLSS Frame Generation (sl.dlss_g, which
// interpolates whole extra frames and needs present-time control to know
// where to inject them) -- plain DLSS Super Resolution (sl.dlss, what this
// integration targets) evaluates synchronously within the command buffer
// like any other compute pass and has no documented hard requirement on
// the present hook. If NVIDIA telemetry/overlay features misbehave without
// it, adding the present proxy is the natural next step (see
// ProgrammingGuideManualHooking.md section 4.2).
qbool VK_DLSS_Composite(VkCommandBuffer commandBuffer, VkImage sceneColorImage, VkImageView sceneColorView, VkImage sceneDepthImage, VkImageView sceneDepthView,
	VkImage motionVectorsImage, VkImageView motionVectorsView, VkImage outputImage, VkImageView outputView,
	VkExtent2D sceneSize, VkExtent2D outputSize, const float* invViewProj, const float* prevViewProj, qbool historyValid)
{
	sl_resource_t colorInRes, colorOutRes, depthRes, mvecRes;
	sl_resource_tag_t tags[4];
	sl_extent_t sceneExtent, outputExtent;
	sl_constants_t consts;
	const sl_base_structure_t* inputs[1];
	void* frameTokenVoid = NULL;
	sl_result_t result;

	if (!VK_DLSS_Active()) {
		return false;
	}

	if (p_slGetNewFrameToken(&frameTokenVoid, &vk_dlss_frameIndex) != SL_RESULT_OK || !frameTokenVoid) {
		return false;
	}

	sceneExtent.left = 0;
	sceneExtent.top = 0;
	sceneExtent.width = sceneSize.width;
	sceneExtent.height = sceneSize.height;
	outputExtent.left = 0;
	outputExtent.top = 0;
	outputExtent.width = outputSize.width;
	outputExtent.height = outputSize.height;

	memset(&colorInRes, 0, sizeof(colorInRes));
	colorInRes.structType.data1 = 0x3a9d70cf;
	colorInRes.structType.data2 = 0x2418;
	colorInRes.structType.data3 = 0x4b72;
	{
		static const uint8_t guid4[8] = { 0x83, 0x91, 0x13, 0xf8, 0x72, 0x1c, 0x72, 0x61 };
		memcpy(colorInRes.structType.data4, guid4, 8);
	}
	colorInRes.structVersion = 1;
	colorInRes.native = sceneColorImage;
	colorInRes.view = sceneColorView;
	colorInRes.state = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	colorInRes.width = sceneSize.width;
	colorInRes.height = sceneSize.height;

	colorOutRes = colorInRes;
	colorOutRes.native = outputImage;
	colorOutRes.view = outputView;
	colorOutRes.state = VK_IMAGE_LAYOUT_GENERAL; // DLSS writes via compute (UAV/storage image)
	colorOutRes.width = outputSize.width;
	colorOutRes.height = outputSize.height;

	depthRes = colorInRes;
	depthRes.native = sceneDepthImage;
	depthRes.view = sceneDepthView;
	depthRes.state = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

	mvecRes = colorInRes;
	mvecRes.native = motionVectorsImage;
	mvecRes.view = motionVectorsView;
	mvecRes.state = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	memset(tags, 0, sizeof(tags));
	for (int i = 0; i < 4; ++i) {
		tags[i].structType.data1 = 0x4c6a5aad;
		tags[i].structType.data2 = 0xb445;
		tags[i].structType.data3 = 0x496c;
		{
			static const uint8_t guid4[8] = { 0x87, 0xff, 0x1a, 0xf3, 0x84, 0x5b, 0xe6, 0x53 };
			memcpy(tags[i].structType.data4, guid4, 8);
		}
		tags[i].structVersion = 1;
	}
	tags[0].resource = &colorInRes;
	tags[0].type = SL_BUFFER_TYPE_SCALING_INPUT_COLOR;
	tags[0].lifecycle = SL_RESOURCE_LIFECYCLE_ONLY_VALID_NOW;
	tags[0].extent = sceneExtent;
	tags[1].resource = &colorOutRes;
	tags[1].type = SL_BUFFER_TYPE_SCALING_OUTPUT_COLOR;
	tags[1].lifecycle = SL_RESOURCE_LIFECYCLE_ONLY_VALID_NOW;
	tags[1].extent = outputExtent;
	tags[2].resource = &depthRes;
	tags[2].type = SL_BUFFER_TYPE_DEPTH;
	tags[2].lifecycle = SL_RESOURCE_LIFECYCLE_VALID_UNTIL_PRESENT;
	tags[2].extent = sceneExtent;
	tags[3].resource = &mvecRes;
	tags[3].type = SL_BUFFER_TYPE_MOTION_VECTORS;
	tags[3].lifecycle = SL_RESOURCE_LIFECYCLE_ONLY_VALID_NOW;
	tags[3].extent = sceneExtent;

	if (p_slSetTagForFrame(frameTokenVoid, &vk_dlss_viewport, tags, 4, commandBuffer) != SL_RESULT_OK) {
		return false;
	}

	// Constants::clipToPrevClip/prevClipToClip are pre-composed
	// clip-to-prev-clip matrices per the SDK's own documented formula
	// (clipToPrevClip = clipToView * viewToViewPrev * viewToClipPrev) --
	// this project's vk_upscale.c already computes invViewProj (current,
	// jittered) and prevViewProj (previous, unjittered) for its own FSR2
	// reprojection (VK_CurrentInvViewProjMatrix/VK_PrevViewProjMatrix in
	// vk_main.c), so clipToPrevClip here is exactly invViewProj composed
	// with prevViewProj: a point in this frame's clip space -> world space
	// (via invViewProj) -> previous frame's clip space (via prevViewProj).
	// That's precisely what's passed in as invViewProj/prevViewProj by the
	// caller (VK_UpscaleComposite), already Vulkan-clip-space and
	// row-major-compatible (both this project's matrices and SL's
	// float4x4 are row-major, per sl_consts.h's own comment).
	memset(&consts, 0, sizeof(consts));
	consts.structType.data1 = 0xdcd35ad7;
	consts.structType.data2 = 0x4e4a;
	consts.structType.data3 = 0x4bad;
	{
		static const uint8_t guid4[8] = { 0xa9, 0xc, 0xe0, 0xc4, 0x9e, 0xb2, 0x3a, 0xfe };
		memcpy(consts.structType.data4, guid4, 8);
	}
	consts.structVersion = 2; // kStructVersion2
	memcpy(&consts.clipToPrevClip, invViewProj, sizeof(consts.clipToPrevClip));
	// prevClipToClip is the true inverse of clipToPrevClip, not just
	// invViewProj/prevViewProj swapped (matrix inversion isn't
	// commutative with composition order) -- reuse R_InvertMatrix
	// (r_matrix.c) for correctness rather than approximating.
	{
		float clipToPrevClip16[16];
		float prevClipToClip16[16];
		memcpy(clipToPrevClip16, invViewProj, 16 * sizeof(float));
		if (R_InvertMatrix(clipToPrevClip16, prevClipToClip16)) {
			memcpy(&consts.prevClipToClip, prevClipToClip16, sizeof(consts.prevClipToClip));
		}
	}
	consts.mvecScale.x = 1.0f / (float)max(1, (int)sceneSize.width);
	consts.mvecScale.y = 1.0f / (float)max(1, (int)sceneSize.height);
	consts.jitterOffset.x = 0.0f; // baked into invViewProj already, not separately tracked here yet
	consts.jitterOffset.y = 0.0f;
	consts.depthInverted = glConfig.reversed_depth ? SL_TRUE : SL_FALSE;
	// This project's motion vectors are camera-reprojection only (see
	// vk_motion_vectors.frag) -- honestly reported as camera motion, not
	// per-object, so DLSS knows not to expect real object-motion accuracy.
	consts.cameraMotionIncluded = SL_TRUE;
	consts.motionVectors3D = SL_FALSE;
	consts.reset = historyValid ? SL_FALSE : SL_TRUE;
	consts.orthographicProjection = SL_FALSE;
	consts.motionVectorsDilated = SL_FALSE;
	consts.motionVectorsJittered = SL_FALSE;

	if (p_slSetConstants(&consts, frameTokenVoid, &vk_dlss_viewport) != SL_RESULT_OK) {
		return false;
	}

	inputs[0] = (const sl_base_structure_t*)&vk_dlss_viewport;
	result = p_slEvaluateFeature(SL_FEATURE_DLSS, frameTokenVoid, inputs, 1, commandBuffer);
	if (result != SL_RESULT_OK) {
		Con_DPrintf("vulkan: DLSS slEvaluateFeature failed (result=%d)\n", result);
		return false;
	}

	// IMPORTANT: per the SDK docs (ProgrammingGuideDLSS.md section 7.0),
	// the host must restore command buffer pipeline/descriptor-set binding
	// state after slEvaluateFeature -- SL's compute dispatch inside that
	// call leaves the command buffer's bound pipeline/descriptor state
	// undefined for whatever runs next. VK_UpscaleComposite's caller
	// (VK_PostProcessComposite -> VK_EndWorldPassAndComposite) begins a
	// fresh render pass immediately after this returns, which itself binds
	// a new pipeline before any draw call -- so no explicit restore call is
	// needed here specifically because nothing between this and the next
	// pipeline bind assumes prior state. If a future caller ever needs to
	// keep drawing on this same command buffer without an intervening
	// pipeline bind, add the restore call documented in
	// ProgrammingGuideManualHooking.md section 7.2 here.

	return true;
}

void VK_DLSS_Shutdown(void)
{
	if (vk_dlss_initialized && p_slShutdown) {
		p_slShutdown();
	}
	if (vk_dlss_module) {
		FreeLibrary(vk_dlss_module);
		vk_dlss_module = NULL;
	}
	vk_dlss_available = false;
	vk_dlss_supported = false;
	vk_dlss_initialized = false;
}
