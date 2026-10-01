// Vulkan renderer: C-side bridge for the official AMD FSR2 SDK path
// (vid_vulkan_upscaler==3). Gathers every piece of engine state the SDK
// dispatch needs (cvars, camera near/far/fov, frame time, Vulkan handles)
// and calls through to vk_fsr2_sdk.cpp's pure-C-ABI primitives -- kept in a
// separate .c file because that .cpp deliberately never includes any engine
// header (see its own header comment: q_shared.h's qbool typedef collides
// with C++'s built-in bool/true/false). This file is the only place that
// knows both the FSR2 SDK's C ABI AND this project's engine types.
#include "quakedef.h"
#include "tr_types.h"
#include "r_matrix.h"
#include "vk_local.h"

extern cvar_t vid_vulkan_sharpness;

// Declared here, not in vk_local.h, since these are vk_fsr2_sdk.cpp's raw
// primitives -- only this bridge file calls them directly. vk_local.h
// exposes this file's own VK_Fsr2Sdk* wrappers instead, matching every
// other upscaler entry point's naming (VK_Fsr2Composite, VK_DLSS_Composite).
extern int VK_Fsr2SdkInit(VkDevice device, VkPhysicalDevice physicalDevice);
extern void VK_Fsr2SdkDestroyResources(void);
extern void VK_Fsr2SdkInvalidateHistory(void);
extern int VK_Fsr2SdkComposite(
	VkCommandBuffer commandBuffer,
	VkImage sceneColorImage, VkImageView sceneColorView, VkFormat sceneColorFormat,
	VkImage sceneDepthImage, VkImageView sceneDepthView,
	VkImage motionVectorsImage, VkImageView motionVectorsView,
	VkFormat outputFormat,
	uint32_t sceneWidth, uint32_t sceneHeight,
	uint32_t displayWidth, uint32_t displayHeight,
	float jitterX, float jitterY,
	int reversedDepth,
	int enableSharpening, float sharpness,
	float frameTimeDeltaMs,
	float cameraNear, float cameraFar, float cameraFovYRadians,
	float viewSpaceToMetersFactor,
	VkImage dstImage, VkImageLayout dstImageLayoutBeforeCopy, VkImageLayout dstImageLayoutAfterCopy);

// Quake world units are not metres -- id Software's own convention: a
// player's bounding box is 56 units tall, matched to a roughly 1.7m-tall
// player (see vk_fsr2.c's VK_FSR2_QUAKE_UNITS_TO_METERS comment for the same
// derivation, used by the hand-port's own depth-clip heuristic).
#define VK_FSR2_SDK_QUAKE_UNITS_TO_METERS (1.7f / 56.0f)

static qbool fsr2SdkInitialized;

qbool VK_Fsr2SdkCreateResources(void)
{
	if (!fsr2SdkInitialized) {
		fsr2SdkInitialized = VK_Fsr2SdkInit(vk_options.logicalDevice, vk_options.physicalDevice) != 0;
	}
	return fsr2SdkInitialized;
}

void VK_Fsr2SdkDestroyResourcesWrapper(void)
{
	VK_Fsr2SdkDestroyResources();
	fsr2SdkInitialized = false;
}

void VK_Fsr2SdkInvalidateHistoryWrapper(void)
{
	VK_Fsr2SdkInvalidateHistory();
}

qbool VK_Fsr2SdkCompositeWrapper(VkCommandBuffer commandBuffer, VkImage sceneColorImage, VkImageView sceneColorView,
	VkImage sceneDepthImage, VkImageView sceneDepthView, VkImage motionVectorsImage, VkImageView motionVectorsView,
	VkExtent2D sceneSize, VkExtent2D displaySize, float jitterX, float jitterY,
	VkImage dstImage, VkImageLayout dstImageLayoutBeforeCopy, VkImageLayout dstImageLayoutAfterCopy)
{
	extern float R_NearPlaneZ(void);
	extern float R_FarPlaneZ(void);
	extern refdef_t r_refdef;
	qbool sharpenEnabled;

	if (!VK_Fsr2SdkCreateResources()) {
		return false;
	}

	sharpenEnabled = vid_vulkan_sharpness.value > 0.0f;

	return VK_Fsr2SdkComposite(commandBuffer,
		sceneColorImage, sceneColorView, vk_options.physicalDeviceSurfaceFormat.format,
		sceneDepthImage, sceneDepthView,
		motionVectorsImage, motionVectorsView,
		vk_options.physicalDeviceSurfaceFormat.format,
		sceneSize.width, sceneSize.height,
		displaySize.width, displaySize.height,
		jitterX, jitterY,
		glConfig.reversed_depth ? 1 : 0,
		sharpenEnabled ? 1 : 0, bound(0.0f, vid_vulkan_sharpness.value, 1.0f),
		(float)max(0.001, cls.frametime) * 1000.0f,
		R_NearPlaneZ(), R_FarPlaneZ(), (float)(r_refdef.fov_y * M_PI / 180.0),
		VK_FSR2_SDK_QUAKE_UNITS_TO_METERS,
		dstImage, dstImageLayoutBeforeCopy, dstImageLayoutAfterCopy) != 0;
}
