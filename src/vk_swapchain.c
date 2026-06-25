/*
Copyright (C) 2018 ezQuake team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

// vk_main.c
// - Main entry point for Vulkan

#ifdef RENDERER_OPTION_VULKAN

#include <vulkan/vulkan.h>
#include "quakedef.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "vk_local.h"

#ifdef __ANDROID__
// Pre-rotation (swapped swapchain extent + real preTransform + a compensating
// rotation matrix in r_matrix.c) is a big performance win: ~70fps -> 450fps+
// on a Xiaomi 14T Pro (Dimensity 9300+/Mali G720, portrait-native panel,
// landscape-locked app), confirmed via `dumpsys SurfaceFlinger` showing zero
// hardware compositor rotation work once preTransform is set correctly (see
// the formula below). Both orientation correctness and the performance win
// are confirmed live simultaneously as of 2026-06-22 -- see the
// android-prerotation-orientation-debug memory file for the full derivation
// and the dead ends ruled out along the way before changing this again.
#define VK_ANDROID_PREROTATION_ENABLED 1

// Set in VK_CreateSwapChain(), read back by VK_AndroidPreRotationDegrees()
// (called from r_matrix.c). currentTransform is NOT a fixed property of the
// device: android:screenOrientation="sensorLandscape" can settle into either
// landscape orientation (normal or reverse, 180 degrees apart) depending on
// how the phone was held at launch, and the surface reports a different
// currentTransform (ROTATE_90 vs ROTATE_270, observed both) for each. See the
// comment on the extent-swap block below for the formula this drives.
static int s_androidPreRotationContentDegrees;
#endif

static void VK_DestroySwapChainDepthResources(void)
{
	if (vk_options.swapChain.depthImageView != VK_NULL_HANDLE) {
		vkDestroyImageView(vk_options.logicalDevice, vk_options.swapChain.depthImageView, NULL);
		vk_options.swapChain.depthImageView = VK_NULL_HANDLE;
	}
	if (vk_options.swapChain.depthImage != VK_NULL_HANDLE) {
		vkDestroyImage(vk_options.logicalDevice, vk_options.swapChain.depthImage, NULL);
		vk_options.swapChain.depthImage = VK_NULL_HANDLE;
	}
	if (vk_options.swapChain.depthImageMemory != VK_NULL_HANDLE) {
		vkFreeMemory(vk_options.logicalDevice, vk_options.swapChain.depthImageMemory, NULL);
		vk_options.swapChain.depthImageMemory = VK_NULL_HANDLE;
	}
}

static qbool VK_CreateSwapChainDepthResources(void)
{
	VkImageViewCreateInfo createImageViewInfo;

	VK_DestroySwapChainDepthResources();

	if (!VK_CreateImageResource(
			vk_options.swapChain.imageSize.width,
			vk_options.swapChain.imageSize.height,
			1,
			vk_options.msaaSamples,
			VK_DepthFormat(),
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk_options.swapChain.depthImage,
			&vk_options.swapChain.depthImageMemory)) {
		return false;
	}

	VK_InitialiseStructure(createImageViewInfo);
	createImageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	createImageViewInfo.image = vk_options.swapChain.depthImage;
	createImageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createImageViewInfo.format = VK_DepthFormat();
	createImageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	createImageViewInfo.subresourceRange.baseMipLevel = 0;
	createImageViewInfo.subresourceRange.levelCount = 1;
	createImageViewInfo.subresourceRange.baseArrayLayer = 0;
	createImageViewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(vk_options.logicalDevice, &createImageViewInfo, NULL, &vk_options.swapChain.depthImageView) != VK_SUCCESS) {
		VK_DestroySwapChainDepthResources();
		return false;
	}

	return true;
}

static void VK_DestroySwapChainMSAAColorResources(void)
{
	if (vk_options.swapChain.msaaColorImageView != VK_NULL_HANDLE) {
		vkDestroyImageView(vk_options.logicalDevice, vk_options.swapChain.msaaColorImageView, NULL);
		vk_options.swapChain.msaaColorImageView = VK_NULL_HANDLE;
	}
	if (vk_options.swapChain.msaaColorImage != VK_NULL_HANDLE) {
		vkDestroyImage(vk_options.logicalDevice, vk_options.swapChain.msaaColorImage, NULL);
		vk_options.swapChain.msaaColorImage = VK_NULL_HANDLE;
	}
	if (vk_options.swapChain.msaaColorImageMemory != VK_NULL_HANDLE) {
		vkFreeMemory(vk_options.logicalDevice, vk_options.swapChain.msaaColorImageMemory, NULL);
		vk_options.swapChain.msaaColorImageMemory = VK_NULL_HANDLE;
	}
}

// Only called when vk_options.msaaSamples > VK_SAMPLE_COUNT_1_BIT (see
// VK_CreateSwapChainFramebuffers). This image is the multisampled render
// target the main render pass actually draws into; the render pass resolves
// it straight into the real swapchain image via a resolve attachment, so it's
// never sampled or read back -- TRANSIENT_ATTACHMENT_BIT lets tile-based GPUs
// (most Android hardware) keep it in on-chip tile memory instead of writing
// it out to VRAM, which is the whole point of doing MSAA this way instead of
// through an offscreen target meant to be read later.
static qbool VK_CreateSwapChainMSAAColorResources(void)
{
	VkImageViewCreateInfo createImageViewInfo;

	VK_DestroySwapChainMSAAColorResources();

	if (!VK_CreateImageResource(
			vk_options.swapChain.imageSize.width,
			vk_options.swapChain.imageSize.height,
			1,
			vk_options.msaaSamples,
			vk_options.physicalDeviceSurfaceFormat.format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vk_options.swapChain.msaaColorImage,
			&vk_options.swapChain.msaaColorImageMemory)) {
		return false;
	}

	VK_InitialiseStructure(createImageViewInfo);
	createImageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	createImageViewInfo.image = vk_options.swapChain.msaaColorImage;
	createImageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createImageViewInfo.format = vk_options.physicalDeviceSurfaceFormat.format;
	createImageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	createImageViewInfo.subresourceRange.baseMipLevel = 0;
	createImageViewInfo.subresourceRange.levelCount = 1;
	createImageViewInfo.subresourceRange.baseArrayLayer = 0;
	createImageViewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(vk_options.logicalDevice, &createImageViewInfo, NULL, &vk_options.swapChain.msaaColorImageView) != VK_SUCCESS) {
		VK_DestroySwapChainMSAAColorResources();
		return false;
	}

	return true;
}

qbool VK_CreateSwapChain(SDL_Window* window, VkInstance instance, VkSurfaceKHR surface)
{
	uint32_t requestedImageCount;
	uint32_t queueFamilyIndices[2];
	uint32_t swapChainImageCount;
	uint32_t i;
	VkSwapchainCreateInfoKHR createInfo = { 0 };

	requestedImageCount = vk_options.physicalDeviceSurfaceCapabilities.minImageCount;
	// The "+1" is only needed to turn a tight (2-image) minimum into the 3
	// images MAILBOX needs to be truly non-blocking. Some Android WSI
	// drivers (observed: MTK/Mali) already report a generous minImageCount
	// (5-6) for the surface; piling another image on top of that exceeds
	// what the platform's BufferQueue/BLASTBufferQueue can actually keep
	// acquired at once, surfacing as a constant stream of logcat
	// "acquireNextBufferLocked: ... NO_BUFFER_AVAILABLE" and corresponding
	// CPU stalls in the image-in-flight wait below.
	if (vk_options.physicalDevicePresentationMode == VK_PRESENT_MODE_MAILBOX_KHR &&
		vk_options.physicalDeviceSurfaceCapabilities.minImageCount < 3) {
		requestedImageCount += 1;
	}
	if (vk_options.physicalDeviceSurfaceCapabilities.maxImageCount > 0) {
		requestedImageCount = min(requestedImageCount, vk_options.physicalDeviceSurfaceCapabilities.maxImageCount);
	}
#ifdef __ANDROID__
	__android_log_print(ANDROID_LOG_INFO, "VK_PROFILE",
		"swapchain present mode=%d (0=IMMEDIATE 1=MAILBOX 2=FIFO 3=FIFO_RELAXED) images requested=%u min=%u max=%u",
		(int)vk_options.physicalDevicePresentationMode, requestedImageCount,
		vk_options.physicalDeviceSurfaceCapabilities.minImageCount, vk_options.physicalDeviceSurfaceCapabilities.maxImageCount);
	__android_log_print(ANDROID_LOG_INFO, "VK_PROFILE",
		"surface transform currentTransform=%d supportedTransforms=0x%x (1=IDENTITY 2=ROTATE_90 4=ROTATE_180 8=ROTATE_270) currentExtent=%ux%u",
		(int)vk_options.physicalDeviceSurfaceCapabilities.currentTransform,
		(unsigned int)vk_options.physicalDeviceSurfaceCapabilities.supportedTransforms,
		vk_options.physicalDeviceSurfaceCapabilities.currentExtent.width,
		vk_options.physicalDeviceSurfaceCapabilities.currentExtent.height);
	{
		int sdlW = 0, sdlH = 0;
		SDL_GetWindowSizeInPixels(window, &sdlW, &sdlH);
		__android_log_print(ANDROID_LOG_INFO, "VK_PROFILE",
			"SDL window size=%dx%d orientation hint=%s",
			sdlW, sdlH, SDL_GetHint(SDL_HINT_ORIENTATIONS) ? SDL_GetHint(SDL_HINT_ORIENTATIONS) : "(unset)");
	}
#endif

	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.minImageCount = requestedImageCount;
	createInfo.surface = surface;
	createInfo.imageArrayLayers = 1;
	createInfo.imageColorSpace = vk_options.physicalDeviceSurfaceFormat.colorSpace;
	createInfo.imageFormat = vk_options.physicalDeviceSurfaceFormat.format;
	if (vk_options.physicalDeviceSurfaceCapabilities.currentExtent.width != ~(uint32_t)0) {
		createInfo.imageExtent = vk_options.physicalDeviceSurfaceCapabilities.currentExtent;
	}
	else {
		int width, height;

		SDL_GetWindowSizeInPixels(window, &width, &height);

		width = bound(vk_options.physicalDeviceSurfaceCapabilities.minImageExtent.width, width, vk_options.physicalDeviceSurfaceCapabilities.maxImageExtent.width);
		height = bound(vk_options.physicalDeviceSurfaceCapabilities.minImageExtent.height, height, vk_options.physicalDeviceSurfaceCapabilities.maxImageExtent.height);

		createInfo.imageExtent.width = width;
		createInfo.imageExtent.height = height;
	}
#if defined(__ANDROID__) && VK_ANDROID_PREROTATION_ENABLED
	// EXPERIMENT 3 (2026-06-22, see android-prerotation-orientation-debug memory):
	// `dumpsys SurfaceFlinger` HWC layer dumps across declared preTransform
	// values, RE-MEASURED after currentTransform itself was caught changing
	// between launches (ROTATE_90 vs ROTATE_270 -- sensorLandscape can settle
	// either way), showed the general rule:
	//   HWC_actual_degrees = (360 - currentTransform_degrees - declared_degrees) mod 360
	// Solving for HWC_actual=0 (zero-overhead fast path, confirmed via dumpsys):
	//   declared_degrees = (360 - currentTransform_degrees) mod 360
	// i.e. declare the NEGATION of currentTransform, not currentTransform
	// itself (matching currentTransform, the official-docs recipe, does NOT
	// reach HWC_actual=0 on this device's compositor). The content rotation
	// needed in r_matrix.c to look correct then equals that same declared
	// value (empirically: 180->180 confirmed correct live; 270->270 derived
	// and consistent). Both 90 and 270 are in the "90-degree family" and need
	// the width/height extent swap; 0 and 180 don't.
	{
		int currentTransformDegrees;
		switch (vk_options.physicalDeviceSurfaceCapabilities.currentTransform) {
			case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: currentTransformDegrees = 90; break;
			case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: currentTransformDegrees = 180; break;
			case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: currentTransformDegrees = 270; break;
			default: currentTransformDegrees = 0; break;
		}
		s_androidPreRotationContentDegrees = (360 - currentTransformDegrees) % 360;

		if (s_androidPreRotationContentDegrees == 90 || s_androidPreRotationContentDegrees == 270) {
			uint32_t temp = createInfo.imageExtent.width;
			createInfo.imageExtent.width = createInfo.imageExtent.height;
			createInfo.imageExtent.height = temp;
		}
	}
#endif
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // VK_IMAGE_USAGE_TRANSFER_DST_BIT if pre-processing enabled
	if (VK_PhysicalDeviceGraphicsQueueFamilyIndex() != VK_PhysicalDevicePresentQueueFamilyIndex()) {
		queueFamilyIndices[0] = VK_PhysicalDeviceGraphicsQueueFamilyIndex();
		queueFamilyIndices[1] = VK_PhysicalDevicePresentQueueFamilyIndex();

		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	}
	else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = NULL;
	}
#if defined(__ANDROID__) && VK_ANDROID_PREROTATION_ENABLED
	// Declare the NEGATION of currentTransform -- see the comment on the
	// extent swap above for why matching currentTransform doesn't work here.
	switch (s_androidPreRotationContentDegrees) {
		case 90: createInfo.preTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR; break;
		case 180: createInfo.preTransform = VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR; break;
		case 270: createInfo.preTransform = VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR; break;
		default: createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; break;
	}
#else
	if (vk_options.physicalDeviceSurfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
		createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else {
		createInfo.preTransform = vk_options.physicalDeviceSurfaceCapabilities.currentTransform;
	}
#endif
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = vk_options.physicalDevicePresentationMode;
	createInfo.clipped = VK_FALSE; // meag: setting this to false so we can read-back for screenshots
	createInfo.oldSwapchain = vk_options.swapChain.handle;

	if (vkCreateSwapchainKHR(vk_options.logicalDevice, &createInfo, NULL, &vk_options.swapChain.handle) != VK_SUCCESS) {
		return false;
	}

	// Create images
	Q_free(vk_options.swapChain.images);
	if (vkGetSwapchainImagesKHR(vk_options.logicalDevice, vk_options.swapChain.handle, &swapChainImageCount, NULL) != VK_SUCCESS) {
		return false;
	}
	vk_options.swapChain.images = Q_malloc(swapChainImageCount * sizeof(vk_options.swapChain.images[0]));
	if (vkGetSwapchainImagesKHR(vk_options.logicalDevice, vk_options.swapChain.handle, &swapChainImageCount, vk_options.swapChain.images) != VK_SUCCESS) {
		Q_free(vk_options.swapChain.images);
		return false;
	}
	vk_options.swapChain.imageCount = swapChainImageCount;
	vk_options.swapChain.imageSize = createInfo.imageExtent;
#ifdef __ANDROID__
	__android_log_print(ANDROID_LOG_INFO, "VK_PROFILE", "swapchain final imageExtent=%ux%u preTransform=%d",
		vk_options.swapChain.imageSize.width, vk_options.swapChain.imageSize.height, (int)createInfo.preTransform);
#endif

	// Create image views
	vk_options.swapChain.imageViews = Q_malloc(swapChainImageCount * sizeof(vk_options.swapChain.imageViews[0]));
	for (i = 0; i < vk_options.swapChain.imageCount; ++i) {
		VkImageViewCreateInfo createImageViewInfo = { 0 };
		createImageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createImageViewInfo.image = vk_options.swapChain.images[i];
		createImageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createImageViewInfo.format = createInfo.imageFormat;
		createImageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createImageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createImageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createImageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		createImageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createImageViewInfo.subresourceRange.baseMipLevel = 0;
		createImageViewInfo.subresourceRange.levelCount = 1;
		createImageViewInfo.subresourceRange.baseArrayLayer = 0;
		createImageViewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(vk_options.logicalDevice, &createImageViewInfo, NULL, &vk_options.swapChain.imageViews[i]) != VK_SUCCESS) {
			return false;
		}
	}

	return true;
}

qbool VK_CreateSwapChainFramebuffers(void)
{
	uint32_t i;
	VkRenderPass renderPass = VK_MainRenderPass();

	if (renderPass == VK_NULL_HANDLE || !vk_options.swapChain.imageViews) {
		return false;
	}

	if (!VK_CreateSwapChainDepthResources()) {
		return false;
	}

	if (vk_options.msaaSamples > VK_SAMPLE_COUNT_1_BIT && !VK_CreateSwapChainMSAAColorResources()) {
		return false;
	}

	vk_options.swapChain.framebuffers = Q_calloc(vk_options.swapChain.imageCount, sizeof(vk_options.swapChain.framebuffers[0]));
	for (i = 0; i < vk_options.swapChain.imageCount; ++i) {
		// With MSAA, the render pass's color attachment 0 is the shared
		// multisampled image (one resource for every swapchain image, like
		// depth above) and the per-image swapchain view only appears as the
		// resolve attachment (2) -- see VK_RenderPassCreate. Without MSAA,
		// attachment 0 is the swapchain image directly, same as before this
		// feature existed.
		qbool msaa = vk_options.msaaSamples > VK_SAMPLE_COUNT_1_BIT;
		VkImageView attachments[3];
		VkFramebufferCreateInfo framebufferInfo = { 0 };

		if (msaa) {
			attachments[0] = vk_options.swapChain.msaaColorImageView;
			attachments[1] = vk_options.swapChain.depthImageView;
			attachments[2] = vk_options.swapChain.imageViews[i];
		}
		else {
			attachments[0] = vk_options.swapChain.imageViews[i];
			attachments[1] = vk_options.swapChain.depthImageView;
		}

		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = msaa ? 3 : 2;
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = vk_options.swapChain.imageSize.width;
		framebufferInfo.height = vk_options.swapChain.imageSize.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(vk_options.logicalDevice, &framebufferInfo, NULL, &vk_options.swapChain.framebuffers[i]) != VK_SUCCESS) {
			VK_DestroySwapChainFramebuffers();
			return false;
		}
	}

#ifdef __ANDROID__
	__android_log_print(ANDROID_LOG_INFO, "VK_PROFILE", "framebuffers created width=%u height=%u count=%u",
		vk_options.swapChain.imageSize.width, vk_options.swapChain.imageSize.height, vk_options.swapChain.imageCount);
#endif

	return true;
}

void VK_DestroySwapChainFramebuffers(void)
{
	if (vk_options.swapChain.framebuffers) {
		uint32_t i;

		for (i = 0; i < vk_options.swapChain.imageCount; ++i) {
			if (vk_options.swapChain.framebuffers[i] != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(vk_options.logicalDevice, vk_options.swapChain.framebuffers[i], NULL);
			}
		}

		Q_free(vk_options.swapChain.framebuffers);
		vk_options.swapChain.framebuffers = NULL;
	}

	VK_DestroySwapChainMSAAColorResources();
	VK_DestroySwapChainDepthResources();
}

void VK_DestroySwapChain(void)
{
	VK_DestroySwapChainFramebuffers();

	if (vk_options.swapChain.imageViews) {
		uint32_t i;

		for (i = 0; i < vk_options.swapChain.imageCount; ++i) {
			vkDestroyImageView(vk_options.logicalDevice, vk_options.swapChain.imageViews[i], NULL);
		}

		Q_free(vk_options.swapChain.imageViews);
		vk_options.swapChain.imageViews = NULL;
	}

	if (vk_options.swapChain.handle != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(vk_options.logicalDevice, vk_options.swapChain.handle, NULL);
		vk_options.swapChain.handle = VK_NULL_HANDLE;
	}

	Q_free(vk_options.swapChain.images);
	vk_options.swapChain.images = NULL;
	vk_options.swapChain.imageCount = 0;
}

#ifdef __ANDROID__
// Called from the shared (renderer-agnostic) r_matrix.c to compensate, in the
// projection matrix, for the swapchain pre-rotation set up above -- see the
// pre-rotation comment in VK_CreateSwapChain(). Not declared in a shared
// header on purpose: r_matrix.c takes this as a plain extern so it doesn't
// need to depend on any Vulkan-specific header.
int VK_AndroidPreRotationDegrees(void)
{
#if !VK_ANDROID_PREROTATION_ENABLED
	return 0;
#else
	// Computed once in VK_CreateSwapChain() from the surface's currentTransform
	// at that moment -- see the comment on the extent-swap block there. Equals
	// the same degrees value used for preTransform.
	return s_androidPreRotationContentDegrees;
#endif
}
#endif

#endif // RENDERER_OPTION_VULKAN
