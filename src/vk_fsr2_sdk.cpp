// Vulkan renderer: official AMD FSR2 SDK (ffx-fsr2-api, vendored in
// external/fsr2, pinned v2.2.1) integration. This is a SEPARATE, explicitly
// opt-in path (vid_vulkan_upscaler==3) from vk_fsr2.c's hand-ported FSR2
// (vid_vulkan_upscaler==1) -- per AGENTS.md, the hand-port must not be
// silently replaced by this, and this must not be declared ready just
// because it compiles (see UPSCALING_PLAN.md Fase 2/7). Until this path is
// validated live, vid_vulkan_upscaler==1 remains the default/recommended
// FSR2 option.
//
// C++ is required here (not vk_fsr2.c, which is C) because the FSR2 VK
// backend's own helper functions (ffxGetTextureResourceVK et al, vk/
// ffx_fsr2_vk.h) use C++ default arguments. This TU deliberately does NOT
// include any engine header (quakedef.h/vk_local.h/etc): those redefine
// true/false (q_shared.h's `typedef enum {false, true} qbool`) to build as
// strict C89, which does not compile as C++. All engine-side data (depth
// format, cvar values, camera near/far/fov, frame time, Vulkan handles) is
// therefore passed in by the C caller (vk_fsr2_sdk_bridge.c) as plain
// parameters -- this file only ever sees <vulkan/vulkan.h> and the FSR2 SDK
// headers, nothing from this project's own C codebase.
#include <cstring>
#include <cstdlib>
#include <cstdint>

#include "ffx_fsr2.h"
#include "vk/ffx_fsr2_vk.h"

extern "C" {

typedef int vk_fsr2_sdk_bool; // 0/1, matches the engine's qbool ABI (int-sized enum) without including q_shared.h

namespace {

FfxFsr2Context g_context;
bool g_contextValid = false;
void* g_scratchBuffer = nullptr;
VkExtent2D g_sceneSize = { 0, 0 };
VkExtent2D g_displaySize = { 0, 0 };
bool g_reset = true;

VkDevice g_device = VK_NULL_HANDLE;
VkPhysicalDevice g_physicalDevice = VK_NULL_HANDLE;

VkImage g_outputImage = VK_NULL_HANDLE;
VkDeviceMemory g_outputImageMemory = VK_NULL_HANDLE;
VkImageView g_outputImageView = VK_NULL_HANDLE;

uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(g_physicalDevice, &memProps);
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
		if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	return UINT32_MAX;
}

void DestroyOutputImage()
{
	if (g_outputImageView != VK_NULL_HANDLE) {
		vkDestroyImageView(g_device, g_outputImageView, nullptr);
		g_outputImageView = VK_NULL_HANDLE;
	}
	if (g_outputImage != VK_NULL_HANDLE) {
		vkDestroyImage(g_device, g_outputImage, nullptr);
		g_outputImage = VK_NULL_HANDLE;
	}
	if (g_outputImageMemory != VK_NULL_HANDLE) {
		vkFreeMemory(g_device, g_outputImageMemory, nullptr);
		g_outputImageMemory = VK_NULL_HANDLE;
	}
}

bool CreateOutputImage(VkExtent2D size, VkFormat format)
{
	VkImageCreateInfo imageInfo{};
	VkMemoryRequirements memReq;
	VkMemoryAllocateInfo allocInfo{};
	VkImageViewCreateInfo viewInfo{};

	DestroyOutputImage();

	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent = { size.width, size.height, 1 };
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (vkCreateImage(g_device, &imageInfo, nullptr, &g_outputImage) != VK_SUCCESS) {
		return false;
	}

	vkGetImageMemoryRequirements(g_device, g_outputImage, &memReq);
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (allocInfo.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(g_device, &allocInfo, nullptr, &g_outputImageMemory) != VK_SUCCESS) {
		DestroyOutputImage();
		return false;
	}
	vkBindImageMemory(g_device, g_outputImage, g_outputImageMemory, 0);

	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = g_outputImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(g_device, &viewInfo, nullptr, &g_outputImageView) != VK_SUCCESS) {
		DestroyOutputImage();
		return false;
	}
	return true;
}

void DestroyContextLocked()
{
	if (g_contextValid) {
		// Spec requirement (ffx_fsr2.h's FfxFsr2Context doc comment): GPU must
		// be idle before destroying the context's resources.
		if (g_device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(g_device);
		}
		ffxFsr2ContextDestroy(&g_context);
		g_contextValid = false;
	}
	if (g_scratchBuffer != nullptr) {
		free(g_scratchBuffer);
		g_scratchBuffer = nullptr;
	}
	DestroyOutputImage();
	g_sceneSize = { 0, 0 };
	g_displaySize = { 0, 0 };
}

bool CreateContextLocked(VkExtent2D sceneSize, VkExtent2D displaySize, int reversedDepth, VkFormat outputFormat)
{
	FfxFsr2ContextDescription desc;
	size_t scratchSize;
	FfxErrorCode err;

	DestroyContextLocked();

	scratchSize = ffxFsr2GetScratchMemorySizeVK(g_physicalDevice);
	g_scratchBuffer = malloc(scratchSize);
	if (g_scratchBuffer == nullptr) {
		return false;
	}

	memset(&desc, 0, sizeof(desc));
	err = ffxFsr2GetInterfaceVK(&desc.callbacks, g_scratchBuffer, scratchSize, g_physicalDevice, vkGetDeviceProcAddr);
	if (err != FFX_OK) {
		free(g_scratchBuffer);
		g_scratchBuffer = nullptr;
		return false;
	}

	desc.maxRenderSize.width = sceneSize.width;
	desc.maxRenderSize.height = sceneSize.height;
	desc.displaySize.width = displaySize.width;
	desc.displaySize.height = displaySize.height;
	desc.device = ffxGetDeviceVK(g_device);
	desc.flags = FFX_FSR2_ENABLE_AUTO_EXPOSURE;
	if (reversedDepth) {
		desc.flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED;
	}

	err = ffxFsr2ContextCreate(&g_context, &desc);
	if (err != FFX_OK) {
		free(g_scratchBuffer);
		g_scratchBuffer = nullptr;
		return false;
	}
	g_contextValid = true;

	// Output image format must match dstImage's (the swapchain image,
	// VK_FORMAT_B8G8R8A8_UNORM in this project) -- vkCmdCopyImage is a raw
	// bit copy, not a format conversion, and requires texel-size-compatible
	// formats between source and destination (Vulkan spec
	// VUID-vkCmdCopyImage-srcImage-01548 family). Using the SDK's own HDR
	// RGBA16F internally and copying straight to an 8-bit swapchain would be
	// the exact format/size mismatch vk_fsr2.c's hand-port has (see
	// UPSCALING_PLAN.md Fase 1, "copy RGBA16F para swapchain BGRA8 é
	// incompatível") -- avoided here by creating the output image at the
	// destination's own format instead.
	if (!CreateOutputImage(displaySize, outputFormat)) {
		DestroyContextLocked();
		return false;
	}

	g_sceneSize = sceneSize;
	g_displaySize = displaySize;
	g_reset = true;
	return true;
}

} // namespace

vk_fsr2_sdk_bool VK_Fsr2SdkInit(VkDevice device, VkPhysicalDevice physicalDevice)
{
	g_device = device;
	g_physicalDevice = physicalDevice;
	return 1;
}

void VK_Fsr2SdkDestroyResources(void)
{
	DestroyContextLocked();
	g_device = VK_NULL_HANDLE;
	g_physicalDevice = VK_NULL_HANDLE;
}

void VK_Fsr2SdkInvalidateHistory(void)
{
	g_reset = true;
}

vk_fsr2_sdk_bool VK_Fsr2SdkComposite(
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
	VkImage dstImage, VkImageLayout dstImageLayoutBeforeCopy, VkImageLayout dstImageLayoutAfterCopy)
{
	VkExtent2D sceneSize = { sceneWidth, sceneHeight };
	VkExtent2D displaySize = { displayWidth, displayHeight };
	FfxFsr2DispatchDescription dispatch;
	FfxErrorCode err;
	VkImageMemoryBarrier barriers[2];
	VkImageCopy region;

	if (g_device == VK_NULL_HANDLE || sceneWidth == 0 || sceneHeight == 0 || displayWidth == 0 || displayHeight == 0) {
		return 0;
	}

	if (!g_contextValid || g_sceneSize.width != sceneWidth || g_sceneSize.height != sceneHeight ||
		g_displaySize.width != displayWidth || g_displaySize.height != displayHeight) {
		if (!CreateContextLocked(sceneSize, displaySize, reversedDepth, outputFormat)) {
			return 0;
		}
	}

	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.commandList = ffxGetCommandListVK(commandBuffer);
	dispatch.color = ffxGetTextureResourceVK(&g_context, sceneColorImage, sceneColorView, sceneWidth, sceneHeight,
		sceneColorFormat, nullptr, FFX_RESOURCE_STATE_COMPUTE_READ);
	dispatch.depth = ffxGetTextureResourceVK(&g_context, sceneDepthImage, sceneDepthView, sceneWidth, sceneHeight,
		VK_FORMAT_D32_SFLOAT, nullptr, FFX_RESOURCE_STATE_COMPUTE_READ);
	dispatch.motionVectors = ffxGetTextureResourceVK(&g_context, motionVectorsImage, motionVectorsView, sceneWidth, sceneHeight,
		VK_FORMAT_R16G16_SFLOAT, nullptr, FFX_RESOURCE_STATE_COMPUTE_READ);
	dispatch.output = ffxGetTextureResourceVK(&g_context, g_outputImage, g_outputImageView, displayWidth, displayHeight,
		outputFormat, nullptr, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
	dispatch.jitterOffset.x = jitterX;
	dispatch.jitterOffset.y = jitterY;
	// Sign, NOT magnitude: this project's motion-vector buffer (vk_motion_vectors.frag)
	// stores currentUV - previousUV, already in UV space [0,1] (confirmed by
	// reading the shader). The SDK's own LoadInputMotionVector
	// (ffx_fsr2_callbacks_glsl.h) computes fUvMotionVector = fSrcMotionVector *
	// MotionVectorScale() and then uses it as fReprojectedUv = fUv +
	// fMotionVector (ffx_fsr2_reconstruct_dilated_velocity_and_previous_depth.h)
	// -- i.e. the SDK expects previousUV - currentUV (added to current UV to
	// reach the previous frame's UV), the OPPOSITE sign from what this
	// buffer stores. Scale stays 1:1 in magnitude (buffer is already UV, not
	// pixels -- the official sample's own motionVectorScale=(renderWidth,
	// renderHeight) only applies to a buffer stored in NDC [-1,1], which this
	// one is not), just negated. Confirmed against the vendored v2.2.1
	// source, not the README's generic pixel-space description, which
	// applies to the pre-scale raw buffer, not the post-scale fUvMotionVector
	// this file produces.
	dispatch.motionVectorScale.x = -1.0f;
	dispatch.motionVectorScale.y = -1.0f;
	dispatch.renderSize.width = sceneWidth;
	dispatch.renderSize.height = sceneHeight;
	dispatch.enableSharpening = enableSharpening != 0;
	dispatch.sharpness = sharpness;
	dispatch.frameTimeDelta = frameTimeDeltaMs;
	dispatch.preExposure = 1.0f;
	dispatch.reset = g_reset;
	dispatch.cameraNear = cameraNear;
	dispatch.cameraFar = cameraFar;
	dispatch.cameraFovAngleVertical = cameraFovYRadians;
	dispatch.viewSpaceToMetersFactor = viewSpaceToMetersFactor;

	err = ffxFsr2ContextDispatch(&g_context, &dispatch);
	if (err != FFX_OK) {
		// Leave g_reset set on failure -- a failed dispatch means the SDK's
		// own internal history (whatever state it holds) didn't advance
		// correctly this frame, so the NEXT real dispatch must still request
		// a reset rather than blend against a frame that silently failed.
		return 0;
	}
	g_reset = false;

	// Copy the SDK's output image into dstImage. The backend tracks
	// g_outputImage's own layout internally; it was registered as
	// FFX_RESOURCE_STATE_UNORDERED_ACCESS above and the dispatch just wrote
	// it via compute, which the backend leaves in VK_IMAGE_LAYOUT_GENERAL
	// (its own UAV convention -- see ffx_fsr2_vk.cpp's resourceStateToLayout
	// mapping for FFX_RESOURCE_STATE_UNORDERED_ACCESS).
	barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barriers[0].pNext = nullptr;
	barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].image = g_outputImage;
	barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	barriers[1] = barriers[0];
	barriers[1].srcAccessMask = 0;
	barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barriers[1].oldLayout = dstImageLayoutBeforeCopy;
	barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barriers[1].image = dstImage;

	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

	memset(&region, 0, sizeof(region));
	region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	region.extent = { displayWidth, displayHeight, 1 };
	vkCmdCopyImage(commandBuffer, g_outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
	barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;

	barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barriers[1].dstAccessMask = 0;
	barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barriers[1].newLayout = dstImageLayoutAfterCopy;

	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

	return 1;
}

} // extern "C"
