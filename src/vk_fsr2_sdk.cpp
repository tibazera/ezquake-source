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

// Mirrors VK_MAX_FRAMES_IN_FLIGHT (vk_local.h) -- duplicated as a literal
// rather than included, since this TU deliberately avoids every engine
// header (see this file's own header comment on why). If the engine's
// constant ever changes, this one must change with it.
#define FSR2_SDK_MAX_FRAMES_IN_FLIGHT 3

namespace {

FfxFsr2Context g_context;
bool g_contextValid = false;
void* g_scratchBuffer = nullptr;
VkExtent2D g_sceneSize = { 0, 0 };
VkExtent2D g_displaySize = { 0, 0 };
bool g_reset = true;

VkDevice g_device = VK_NULL_HANDLE;
VkPhysicalDevice g_physicalDevice = VK_NULL_HANDLE;

// Per-frame-in-flight, NOT a single shared image -- this is the exact same
// cross-command-buffer race already found and fixed across vk_fsr2.c,
// vk_upscale.c and vk_dlss.c this session (see those files' own history-slot
// comments for the full derivation): with VK_MAX_FRAMES_IN_FLIGHT=3 real
// command buffers potentially executing concurrently on the GPU, a single
// shared output image means frame N+1's slEvaluateFeature-equivalent
// (ffxFsr2ContextDispatch writing g_outputImage) could race frame N's still
// in-flight read+copy of that same image. Found by Codex's independent
// investigation into the Tiago-reported shimmer/perf regression
// (2026-10-02) -- confirmed real by inspection, not yet proven to be the
// sole cause of the perf drop (that needs GPU timing this session doesn't
// have access to), but it is a genuine correctness bug regardless and the
// same class fixed everywhere else in this codebase. Indexed by frameSlot
// (passed in from VK_Fsr2SdkComposite/VK_Fsr2SdkCompositeWrapper's caller,
// vk_options.frame.currentFrame on the C side).
VkImage g_outputImage[FSR2_SDK_MAX_FRAMES_IN_FLIGHT];
VkDeviceMemory g_outputImageMemory[FSR2_SDK_MAX_FRAMES_IN_FLIGHT];
VkImageView g_outputImageView[FSR2_SDK_MAX_FRAMES_IN_FLIGHT];

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
	for (int slot = 0; slot < FSR2_SDK_MAX_FRAMES_IN_FLIGHT; ++slot) {
		if (g_outputImageView[slot] != VK_NULL_HANDLE) {
			vkDestroyImageView(g_device, g_outputImageView[slot], nullptr);
			g_outputImageView[slot] = VK_NULL_HANDLE;
		}
		if (g_outputImage[slot] != VK_NULL_HANDLE) {
			vkDestroyImage(g_device, g_outputImage[slot], nullptr);
			g_outputImage[slot] = VK_NULL_HANDLE;
		}
		if (g_outputImageMemory[slot] != VK_NULL_HANDLE) {
			vkFreeMemory(g_device, g_outputImageMemory[slot], nullptr);
			g_outputImageMemory[slot] = VK_NULL_HANDLE;
		}
	}
}

bool CreateOutputImage(VkExtent2D size, VkFormat format)
{
	DestroyOutputImage();

	for (int slot = 0; slot < FSR2_SDK_MAX_FRAMES_IN_FLIGHT; ++slot) {
		VkImageCreateInfo imageInfo{};
		VkMemoryRequirements memReq;
		VkMemoryAllocateInfo allocInfo{};
		VkImageViewCreateInfo viewInfo{};

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

		if (vkCreateImage(g_device, &imageInfo, nullptr, &g_outputImage[slot]) != VK_SUCCESS) {
			DestroyOutputImage();
			return false;
		}

		vkGetImageMemoryRequirements(g_device, g_outputImage[slot], &memReq);
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (allocInfo.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(g_device, &allocInfo, nullptr, &g_outputImageMemory[slot]) != VK_SUCCESS) {
			DestroyOutputImage();
			return false;
		}
		vkBindImageMemory(g_device, g_outputImage[slot], g_outputImageMemory[slot], 0);

		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = g_outputImage[slot];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(g_device, &viewInfo, nullptr, &g_outputImageView[slot]) != VK_SUCCESS) {
			DestroyOutputImage();
			return false;
		}
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
	// NOT FFX_FSR2_ENABLE_AUTO_EXPOSURE: this flag only selects WHICH
	// exposure value is used (the SPD-computed one vs params->exposure/the
	// internal default) -- ffx_fsr2.cpp's scheduleDispatch call for
	// pipelineComputeLuminancePyramid is UNCONDITIONAL regardless of this
	// flag (confirmed by reading the dispatch function directly), so this
	// does NOT skip the SPD pass or its cost. Kept removed anyway because
	// it's still the more correct setting for this project (v_gamma/
	// v_contrast already handle exposure/tonemapping on the engine side,
	// same reasoning vk_dlss.c already applies with a fixed preExposure),
	// but this alone does not explain the ~3000->~1400 FPS drop Tiago
	// reported with Vulkan+FSR2 vs Vulkan alone -- that cost is elsewhere,
	// still being investigated.
	desc.flags = 0;
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
	VkCommandBuffer commandBuffer, uint32_t frameSlot,
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

	if (g_device == VK_NULL_HANDLE || sceneWidth == 0 || sceneHeight == 0 || displayWidth == 0 || displayHeight == 0 ||
		frameSlot >= FSR2_SDK_MAX_FRAMES_IN_FLIGHT) {
		return 0;
	}

	// Confirmed NOT the cause of the 2026-10-02 FPS regression (Codex's
	// independent investigation + inspection here agree): sceneSize/displaySize
	// are stable frame to frame (stored once at swapchain creation,
	// VK_SceneRenderExtent/vk_options.swapChain.imageSize don't change
	// per-frame), so this branch only fires on real resize/renderscale
	// change, not every frame. Real diagnostic logging removed -- see
	// CONTINUE.md for what WAS found (g_outputImage race, fixed below).
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
	dispatch.output = ffxGetTextureResourceVK(&g_context, g_outputImage[frameSlot], g_outputImageView[frameSlot], displayWidth, displayHeight,
		outputFormat, nullptr, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
	dispatch.jitterOffset.x = jitterX;
	dispatch.jitterOffset.y = jitterY;
	// CORRECTED (2026-10-02, second pass): previous comment's sign-only fix
	// was wrong. Verified against the actual official Vulkan sample
	// (external/fsr2/src/VK/UpscaleContext_FSR2_API.cpp:231-232 and
	// libs/cauldron/src/VK/shaders/GLTFMotionVectorsPass-frag.glsl:58-59),
	// not just the SDK's internal shader headers in isolation:
	// - The official sample's own motion-vector shader computes
	//   `motionVect = CurrPosition.xy/w - PrevPosition.xy/w` -- NDC space
	//   [-1,1], SAME sign convention (current - previous) this project's
	//   vk_motion_vectors.frag already uses, just in UV [0,1] instead of NDC.
	// - The sample passes motionVectorScale = (renderWidth, renderHeight),
	//   POSITIVE, not negated.
	// - ffx_fsr2.cpp:902-903 divides that scale by the motion-vector target
	//   size (renderWidth/Height here, since FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS
	//   is not set) before storing it as cbFSR2.fMotionVectorScale -- so the
	//   sample's effective internal scale is exactly 1.0, meaning its NDC
	//   buffer is consumed as-is with no further unit conversion inside the
	//   shaders despite LoadInputMotionVector's result being named
	//   fUvMotionVector.
	// This project's buffer is UV [0,1], which is exactly half the magnitude
	// of the sample's NDC [-1,1] delta for the same underlying motion
	// (NDC = 2*UV - 1). To reproduce the sample's proven-correct effective
	// scale of 1.0 on an NDC-space buffer using a UV-space buffer of half
	// the magnitude, the pre-division scale parameter must be doubled:
	// 2*renderWidth instead of renderWidth. Sign stays unchanged (current -
	// previous matches the sample already).
	dispatch.motionVectorScale.x = 2.0f * (float)sceneWidth;
	dispatch.motionVectorScale.y = 2.0f * (float)sceneHeight;
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
	barriers[0].image = g_outputImage[frameSlot];
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
	vkCmdCopyImage(commandBuffer, g_outputImage[frameSlot], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

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
