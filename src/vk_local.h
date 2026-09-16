/*
Copyright (C) 2018 ezQuake team.

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

#ifndef EZQUAKE_VK_LOCAL_HEADER
#define EZQUAKE_VK_LOCAL_HEADER

#include <vulkan/vulkan.h>

#include "r_local.h"
#include "r_state.h"

// Depth of the CPU->GPU pipeline: how many frames the CPU is allowed to get
// ahead of the GPU before it must block on a fence. Every per-frame CPU-written
// resource (dynamic buffers in vk_buffers.c, texture upload staging in
// vk_texture.c, the world flat-sky descriptor sets in vk_world.c) is
// double/triple-buffered by this count and indexed by frame.currentFrame, so
// the inFlightFences[currentFrame] wait at the top of VK_BeginFrame is what
// guarantees the GPU finished reading a slot before the CPU rewrites it --
// raising this number widens that safety window, it does not remove it.
//
// Bumped 2 -> 3: with only 2 frames in flight and a swapchain that now runs 3
// images in IMMEDIATE/MAILBOX (see VK_CreateSwapChain), the CPU had to block on
// the GPU almost every frame in uncapped/no-compositor present modes, so each
// vkWaitForFences turned into a near-full-frame stall (the "internal pacing"
// the Linux testers measured as ~1700fps where the GPU could sustain ~5000).
// A third in-flight frame lets the CPU stay one more frame ahead and keeps the
// GPU fed. Changing this resizes the frame-indexed arrays in all of the files
// listed above in lockstep (they are all sized by this macro), so the buffer
// reuse hazard the fences protect against is preserved.
#define VK_MAX_FRAMES_IN_FLIGHT 3

typedef struct SDL_Window SDL_Window;
typedef struct gltexture_s gltexture_t;
struct entity_s;
struct model_s;

#define EZ_VKFUNC_DECL_LOAD(instance, func) PFN_##func q##func = (PFN_##func)vkGetInstanceProcAddr(instance, #func)
#define EZ_VKFUNC_LOAD(instance, func) q##func = (PFN_##func)vkGetInstanceProcAddr(instance, #func)
#define VK_InitialiseStructure(x) memset(&(x), 0, sizeof(x))

// vk_main.c
qbool VK_Initialise(SDL_Window* window);
void VK_Shutdown(r_shutdown_mode_t mode);
void VK_AbandonActiveFrame(void);
void VK_PopulateConfig(void);
void VK_RequestSwapChainRecreate(void);
void VK_RequestSurfaceRecreate(void);
// Writes this frame's jittered projection matrix (16 floats) into `out`,
// for 3D draws (world/aliasmodel/sprite3d MVP construction) -- writes
// R_ProjectionMatrix() unmodified when the upscaler is off, a
// Halton-jittered copy when it's on. Output-parameter style, not
// return-by-pointer -- see vk_main.c's definition for why (a returned
// static buffer pointer was found to crash under this project's LTO
// build). VK_PrevViewProjMatrix is the paired previous-frame matrix the
// motion-vector pass reprojects against.
void VK_JitteredProjectionMatrix(float* out);
const float* VK_PrevViewProjMatrix(void);
void VK_AdvanceJitter(void);
qbool VK_CurrentInvViewProjMatrix(float* out);
void VK_EndWorldPassAndComposite(void);

// vk_instance.c
qbool VK_CreateInstance(SDL_Window* window, VkInstance* instance);

// vk_debug.c
void VK_ShutdownDebugCallback(VkInstance instance);
void VK_InitialiseDebugCallback(VkInstance instance);

// vk_physical_devices.c
qbool VK_SelectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
uint32_t VK_PhysicalDeviceGraphicsQueueFamilyIndex(void);
uint32_t VK_PhysicalDeviceComputeQueueFamilyIndex(void);
qbool VK_RefreshPresentationMode(void);
uint32_t VK_PhysicalDevicePresentQueueFamilyIndex(void);
qbool VK_CreateLogicalDevice(VkInstance instance);
void VK_DetermineMSAASampleCount(void);
void VK_LoadPipelineCache(void);
void VK_SavePipelineCache(void);

// vk_window_surface.c
qbool VK_CreateWindowSurface(SDL_Window* window, VkInstance instance, VkSurfaceKHR* surface);
void VK_DestroyWindowSurface(VkInstance instance, VkSurfaceKHR surface);

// vk_swapchain.c
qbool VK_CreateSwapChain(SDL_Window* window, VkInstance instance, VkSurfaceKHR surface);
void VK_DestroySwapChain(void);
qbool VK_CreateSwapChainFramebuffers(void);
void VK_DestroySwapChainFramebuffers(void);

// vk_renderpass.c
qbool VK_RenderPassCreate(void);
void VK_RenderPassDelete(void);
VkRenderPass VK_MainRenderPass(void);
VkRenderPass VK_FrameRenderPass(qbool clear_color);
VkRenderPass VK_PostProcessRenderPass(void);
VkRenderPass VK_HudRenderPass(void);
VkRenderPass VK_WorldNormalsRenderPass(void);
VkFormat VK_DepthFormat(void);
VkFormat VK_WorldNormalsFormat(void);

// vk_swapchain.c
qbool VK_PostProcessActive(void);
qbool VK_CreatePostProcessResources(void);
void VK_DestroyPostProcessResources(void);
VkFramebuffer VK_PostProcessFramebuffer(uint32_t imageIndex);
VkFramebuffer VK_PostProcessCompositeFramebuffer(uint32_t imageIndex);
VkFramebuffer VK_HudFramebuffer(uint32_t imageIndex);
// The resolution every 3D-scene draw call (world/aliasmodel/sprite3d/vao/
// particles) must set its viewport/scissor to: sceneSize when the upscaler
// is resizing the scene target, imageSize otherwise (upscaler off or
// vid_vulkan_renderscale 1 -- identical to native resolution, so every call
// site that used to hardcode imageSize behaves exactly as before). HUD/2D
// pipelines do NOT use this -- they always target native imageSize, see
// VK_HudSetViewportScissor in vk_draw.c.
VkExtent2D VK_SceneRenderExtent(void);
qbool VK_CreateWorldNormalsResources(void);
void VK_DestroyWorldNormalsResources(void);
VkFramebuffer VK_WorldNormalsFramebuffer(uint32_t imageIndex);
VkDescriptorSet VK_WorldNormalsDescriptorSet(uint32_t imageIndex);

// vk_draw.c
void VK_PostProcessTransitionForSampling(VkCommandBuffer commandBuffer, uint32_t imageIndex);
void VK_PostProcessComposite(VkCommandBuffer commandBuffer, uint32_t imageIndex);
void VK_WorldNormalsTransitionForSampling(VkCommandBuffer commandBuffer, uint32_t imageIndex);
void VK_WorldOutlineComposite(VkCommandBuffer commandBuffer, uint32_t imageIndex);
qbool VK_WorldOutlineActive(void);
VkShaderModule VK_HudCreateShaderModule(const unsigned char* bytes, unsigned int length);
void VK_HudSetViewportScissor(VkCommandBuffer commandBuffer);

// vk_upscale.c
// FSR2-style spatial upscale (EASU+RCAS, see vk_upscale.frag) of the
// low-res scene target (sceneSize) up to the native swapchain resolution
// (imageSize), used by VK_PostProcessComposite in place of a plain blit
// when vk_options.swapChain.upscaleActive. Not temporal FSR2 -- see
// vk_upscale.frag's header comment for why. DLSS is not implemented (needs
// the proprietary NVIDIA NGX SDK, not vendored here); vid_vulkan_upscaler 2
// falls back to this same FSR2-style path until NGX support exists.
qbool VK_UpscaleActive(void);
qbool VK_CreateUpscaleResources(void);
void VK_DestroyUpscaleResources(void);
void VK_UpscaleComposite(VkCommandBuffer commandBuffer, uint32_t imageIndex);
void VK_UpscaleForgetDescriptorSets(void);
// Temporal (motion-vector reprojected history) path on top of the spatial
// EASU+RCAS upscale -- see vk_upscale.c's VK_TemporalUpscaleActive for the
// gating conditions. VK_UpscaleUpdateMatrices must be called before the
// composite render pass begins (vkCmdUpdateBuffer can't run inside a render
// pass instance); VK_UpscaleUpdateHistory must be called after it ends
// (vkCmdCopyImage has the same restriction) and before the following HUD
// pass begins. Both are no-ops (returning false / doing nothing) when
// temporal upscaling isn't applicable this frame.
qbool VK_UpscaleUpdateMatrices(VkCommandBuffer commandBuffer);
void VK_UpscaleUpdateHistory(VkCommandBuffer commandBuffer, uint32_t imageIndex);
// Called whenever DLSS handles a frame instead of this file's own path, so
// a later fallback frame doesn't blend against stale pre-switch history.
void VK_UpscaleInvalidateHistory(void);

// Populates the DLSS motion-vector buffer -- must run after
// VK_UpscaleUpdateMatrices (uses the same matricesBuffers[historyIndex]
// slot it just wrote) and before VK_DLSS_Composite tags/reads it, all
// outside any other render pass instance (this opens/closes its own). See
// vk_upscale.c's own comment on the exact call-order requirement.
qbool VK_MotionVectorsComposite(VkCommandBuffer commandBuffer, uint32_t imageIndex);
VkImage VK_MotionVectorsImage(void);
VkImageView VK_MotionVectorsImageView(void);
VkExtent2D VK_MotionVectorsImageSize(void);
// Shared image-barrier helper, used by both vk_upscale.c's own history
// copy and vk_dlss.c's VK_DLSS_CopyOutputTo.
VkImageMemoryBarrier VK_UpscaleMakeImageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess);

// vk_dlss.c -- real NVIDIA DLSS via Streamline SDK, only actually usable on
// RTX 50-series+ with a current driver. Every entry point fails gracefully
// when unavailable; see vk_dlss.c's own header comment for the full design.
qbool VK_DLSS_LoadLibrary(void);
qbool VK_DLSS_CheckSupport(VkPhysicalDevice physicalDevice);
// Called before vkCreateDevice -- writes up to maxNames required Vulkan
// device extension name pointers into outNames, returns how many were
// written (0 if none needed / not queryable, which is not a failure).
uint32_t VK_DLSS_GetRequiredDeviceExtensions(const char** outNames, uint32_t maxNames);
qbool VK_DLSS_SetVulkanInfo(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t graphicsQueueFamily, uint32_t graphicsQueueIndex);
qbool VK_DLSS_Available(void);
qbool VK_DLSS_Active(void);
qbool VK_DLSS_GetOptimalRenderSize(uint32_t outputWidth, uint32_t outputHeight, uint32_t* renderWidth, uint32_t* renderHeight);
void VK_DLSS_AdvanceFrame(void);
qbool VK_DLSS_Composite(VkCommandBuffer commandBuffer, VkImage sceneColorImage, VkImageView sceneColorView, VkImage sceneDepthImage, VkImageView sceneDepthView,
	VkImage motionVectorsImage, VkImageView motionVectorsView,
	VkExtent2D sceneSize, VkExtent2D outputSize, const float* invViewProj, const float* prevViewProj);
// Copies DLSS's internal output image into dstImage (typically the
// composite pass's framebuffer target) -- call right after
// VK_DLSS_Composite returns true. dstImageLayoutBeforeCopy/AfterCopy let
// the caller specify what layout dstImage is actually in (this project's
// swapchain image is PRESENT_SRC_KHR going in, same convention as
// VK_PostProcessTransitionForSampling elsewhere).
qbool VK_DLSS_CopyOutputTo(VkCommandBuffer commandBuffer, VkImage dstImage, VkImageLayout dstImageLayoutBeforeCopy, VkImageLayout dstImageLayoutAfterCopy);
// Called whenever the FSR2-style path handles a frame instead of DLSS, so
// DLSS's next real run doesn't reproject against a frame it never wrote.
void VK_DLSS_InvalidateHistory(void);
void VK_DLSS_Shutdown(void);

// vk_blending.c
void VK_BlendingConfigure(VkPipelineColorBlendStateCreateInfo* info, VkPipelineColorBlendAttachmentState* blending, r_blendfunc_t func);

// vk_misc.c
void VK_GrowBuffer(void** buffer, int* capacity, int needed, size_t elementSize);

// vk_resources.c
uint32_t VK_FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties);
qbool VK_CreateBufferResource(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* memory);
qbool VK_CreateBufferResourceWithSelector(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	uint32_t (*memoryTypeSelector)(VkMemoryPropertyFlags, uint32_t), VkBuffer* buffer, VkDeviceMemory* memory);
qbool VK_CreateImageResource(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, VkDeviceMemory* memory);
VkCommandBuffer VK_BeginImmediateCommands(void);
qbool VK_EndImmediateCommands(VkCommandBuffer command_buffer);
qbool VK_EndImmediateCommandsAfter(VkCommandBuffer command_buffer, VkSemaphore waitSemaphore, VkPipelineStageFlags waitStage);
void VK_DestroyImmediateCommandPool(void);

// vk_buffers.c
VkBuffer VK_BufferHandle(r_buffer_id id);
VkDeviceSize VK_BufferDeviceOffset(r_buffer_id id);

// vk_texture.c
void VK_AllocateTextureNames(gltexture_t* glt);
void VK_UploadTexture(texture_ref texture, int mode, int width, int height, byte* data);
VkDescriptorSetLayout VK_TextureDescriptorSetLayout(void);
VkDescriptorSet VK_TextureDescriptorSet(texture_ref texture);
VkDescriptorSetLayout VK_TextureBindlessDescriptorSetLayout(void);
VkDescriptorSet VK_TextureBindlessDescriptorSet(void);
qbool VK_TextureDescriptorImageInfo(texture_ref texture, qbool nearest, VkDescriptorImageInfo* info);
qbool VK_TextureReady(texture_ref texture);
void VK_TextureInitialiseState(void);
void VK_TextureShutdown(void);
void VK_TextureDelete(texture_ref texture);
void VK_TextureMipmapGenerate(texture_ref texture);
void VK_TextureWrapModeClamp(texture_ref tex);
void VK_TextureLabelSet(texture_ref texture, const char* label);
qbool VK_TextureUnitBind(int unit, texture_ref texture);
qbool VK_TextureIsUnitBound(int unit, texture_ref texture);
void VK_TextureUnitMultiBind(int first_unit, int num_textures, texture_ref* textures);
void VK_TextureGet(texture_ref texture, int buffer_size, byte* buffer, int bpp);
void VK_TextureCompressionSet(qbool enabled);
void VK_TextureCreate2D(texture_ref* reference, int width, int height, const char* name, qbool is_lightmap);
void VK_TexturesCreate(r_texture_type_id type, int count, texture_ref* textures);
void VK_TextureReplaceSubImageRGBA(texture_ref texture, int offsetx, int offsety, int width, int height, byte* buffer);
void VK_TextureFlushPendingUploads(VkCommandBuffer commandBuffer, uint32_t frameIndex);
void VK_TextureApplyDeferredUploads(void);
void VK_TextureSetFiltering(texture_ref texture, texture_minification_id min_filter, texture_magnification_id mag_filter);
void VK_TextureSetAnisotropy(texture_ref texture, int anisotropy);

// vk_draw.c
void VK_HudResourcesShutdown(void);
void VK_HudSwapchainChanged(void);

// vk_world.c
void VK_WorldResourcesShutdown(void);
void VK_PrepareModelRendering(qbool vid_restart);
void VK_PreRenderView(void);
void VK_DrawWorld(void);
void VK_ChainBrushModelSurfaces(struct model_s* model, struct entity_s* ent);
void VK_DrawBrushModel(struct entity_s* ent, qbool polygonOffset, qbool caustics);
void VK_RenderView(void);

// vk_aliasmodel.c
void VK_AliasModelResourcesShutdown(void);
void VK_AliasModelFrameReset(void);
void VK_AliasQueueDraw(struct entity_s* ent, struct model_s* model, int firstVertex, int vertexCount, texture_ref texture, qbool outline, int effects, int render_effects, float lerpfrac);
void VK_DrawAliasFrame(struct entity_s* ent, struct model_s* model, int pose1, int pose2, texture_ref texture, texture_ref fb_texture, qbool outline, int effects, int render_effects, float lerpfrac);
void VK_DrawAliasModelShadow(struct entity_s* ent);
void VK_DrawAlias3Model(struct entity_s* ent, qbool outline, qbool additive_pass);
void VK_RenderAliasModels(qbool postscene);

// vk_sprite3d.c
void VK_Sprite3DResourcesShutdown(void);
void VK_DrawSpriteModel(struct entity_s* ent);
void VK_DrawSimpleItem(struct model_s* model, int skin, vec3_t origin, float scale, vec3_t up, vec3_t right);
void VK_DrawClassicParticles(int particles_to_draw);
void VK_Prepare3DSprites(void);
void VK_Draw3DSprites(void);

// vk_main.c
VkCommandBuffer VK_CurrentCommandBuffer(void);

// (common)
typedef struct vk_options_s {
	VkInstance instance;
	VkSurfaceKHR surface;
	SDL_Window* window;

	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceFeatures physicalDeviceFeatures;
	VkPhysicalDeviceProperties physicalDeviceProperties;
	uint32_t physicalDeviceGraphicsQueueFamilyIndex;
	uint32_t physicalDeviceComputeQueueFamilyIndex;
	uint32_t physicalDevicePresentQueueFamilyIndex;
	VkPresentModeKHR physicalDevicePresentationMode;
	VkSurfaceFormatKHR physicalDeviceSurfaceFormat;
	VkSurfaceCapabilitiesKHR physicalDeviceSurfaceCapabilities;
	VkDevice logicalDevice;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	// Determined once in VK_Initialise from vid_framebuffer_multisample,
	// clamped against the physical device's actual limits (see
	// VK_DetermineMSAASampleCount). VK_SAMPLE_COUNT_1_BIT (the default,
	// matching vid_framebuffer_multisample's "0") means no MSAA: render pass/
	// framebuffer/pipeline creation all take the exact same path as before
	// this feature existed. Changing the cvar requires a full vid_restart
	// (same as the other Framebuffer menu options) -- this is read once at
	// init, not polled per frame.
	VkSampleCountFlagBits msaaSamples;
	// Anti-lag / low-latency support: detected once in VK_CreateLogicalDevice
	// from the optional extensions the chosen physical device actually
	// supports (VK_AMD_anti_lag requires its pNext feature bit too, not just
	// the extension string -- see VK_PhysicalDeviceSupportsOptionalExtensions).
	// vid_vulkan_antilag is polled per-frame in VK_BeginFrame/VK_EndFrame, no
	// vid_restart needed: it only gates whether these calls happen, nothing
	// about pipeline/render pass/swapchain creation changes.
	qbool supportsAmdAntiLag;
	qbool supportsNvLowLatency2;
	// Core-in-1.2 descriptor indexing features needed for a bindless texture
	// array (VK_TextureBindlessDescriptorSet in vk_texture.c): lets shaders
	// index sampler2D textures[] by a non-constant (push constant) index, and
	// lets that array's descriptor set be updated with UPDATE_AFTER_BIND_BIT
	// while other in-flight command buffers still reference it -- eliminating
	// the whole class of "descriptor set destroyed/updated while still bound"
	// hazard the per-texture descriptor-set path needs manual deferral for.
	// Detected once in VK_CreateLogicalDevice; VK_TextureBindlessDescriptorSet
	// falls back to the legacy per-texture path when false.
	qbool supportsDescriptorIndexing;
	uint64_t antiLagFrameIndex;
	PFN_vkAntiLagUpdateAMD antiLagUpdateAMD;
	PFN_vkSetLatencySleepModeNV setLatencySleepModeNV;
	PFN_vkLatencySleepNV latencySleepNV;
	PFN_vkSetLatencyMarkerNV setLatencyMarkerNV;
	// Dedicated binary semaphore signalled by the driver from vkLatencySleepNV
	// once it's time to let the CPU proceed -- vkLatencySleepNV itself only
	// schedules that signal, it does not block, so VK_BeginFrame must wait on
	// this semaphore right after calling it.
	VkSemaphore latencySleepSemaphore;
	qbool latencySleepModeSet;
	// Persisted across runs via VK_LoadPipelineCache/VK_SavePipelineCache so
	// vkCreateGraphicsPipelines() at the various call sites can skip
	// re-compiling shaders/pipelines already seen on this GPU+driver.
	VkPipelineCache pipelineCache;
	struct {
		VkSwapchainKHR handle;
		VkImage* images;
		VkImageView* imageViews;
		VkFramebuffer* framebuffers;
		VkImage depthImage;
		VkDeviceMemory depthImageMemory;
		VkImageView depthImageView;
		// Only allocated when msaaSamples > VK_SAMPLE_COUNT_1_BIT: the
		// multisampled color attachment the main render pass draws into,
		// resolved straight to the swapchain image via the render pass's
		// resolve attachment. Never sampled/read, so there's exactly one of
		// these shared across every swapchain image (like depthImage above),
		// not one per image.
		VkImage msaaColorImage;
		VkDeviceMemory msaaColorImageMemory;
		VkImageView msaaColorImageView;
		VkExtent2D imageSize;
		int imageCount;
		// Offscreen color target the main render pass draws into when
		// VK_PostProcessActive() is true (real gamma/contrast curve and/or
		// FXAA requested -- see VK_PostProcessActive). One per swapchain
		// image, same lifetime/recreate rules as framebuffers[] above: each
		// swapchain image needs its own offscreen target + framebuffer
		// because the composite pass for image N may still be reading it
		// while frame N+1 starts drawing into a different swapchain image's
		// target (VK_MAX_FRAMES_IN_FLIGHT > 1). When inactive, none of this
		// is allocated and the main render pass targets the swapchain image
		// directly, identical to before this feature existed.
		//
		// sceneSize: the resolution this target is actually created at.
		// Equals imageSize when vid_vulkan_renderscale is 1 (or the upscaler
		// is off) -- identical behaviour to before the upscaler pipeline
		// existed. When < imageSize, VK_UpscaleComposite (vk_upscale.c) is
		// what the composite pass now calls instead of the old plain
		// gamma/FXAA copy: it upscales postProcessColorImages[i] from
		// sceneSize to imageSize (FSR2 or bilinear fallback) as part of the
		// same composite pass, then HUD draws on top at native imageSize --
		// see VK_HudBeginNativePass in vk_draw.c.
		VkExtent2D sceneSize;
		// Depth/MSAA-color for the scene render pass, sized at sceneSize --
		// only allocated when upscaleActive (sceneSize != imageSize), since
		// the shared depthImage/msaaColorImage above are sized at imageSize
		// and Vulkan requires every attachment of a framebuffer to share the
		// same dimensions. When upscaleActive is false these stay
		// VK_NULL_HANDLE and postProcessFramebuffers keeps using
		// depthImage/msaaColorImageView like before this feature existed.
		VkImage sceneDepthImage;
		VkDeviceMemory sceneDepthImageMemory;
		VkImageView sceneDepthImageView;
		VkImage sceneMsaaColorImage;
		VkDeviceMemory sceneMsaaColorImageMemory;
		VkImageView sceneMsaaColorImageView;
		VkImage* postProcessColorImages;
		VkDeviceMemory* postProcessColorImageMemory;
		VkImageView* postProcessColorImageViews;
		VkFramebuffer* postProcessFramebuffers;
		VkFramebuffer* postProcessCompositeFramebuffers;
		// Framebuffer for vk_renderpass_hud (VK_HudRenderPass): same swapchain
		// image view as postProcessCompositeFramebuffers[i], bound against the
		// LOAD-instead-of-DONT_CARE render pass so HUD can draw on top of the
		// composite pass's output -- see VK_UpscaleCompositeAndBeginHudPass.
		VkFramebuffer* hudFramebuffers;
		VkDescriptorPool postProcessDescriptorPool;
		VkDescriptorSet* postProcessDescriptorSets;
		qbool postProcessActive;
		// True once sceneSize < imageSize (upscaler pipeline actually
		// resizing, not just doing gamma/FXAA at native res). Distinguishes
		// "composite pass is active" (postProcessActive) from "composite
		// pass also needs to upscale" -- VK_EndFrame checks this to decide
		// whether HUD draws in the main pass (legacy/native path) or in its
		// own native-resolution pass after the upscale composite.
		qbool upscaleActive;
		// gl_outline & 2 (world outline) prepass resources -- see
		// VK_CreateWorldNormalsResources. One per swapchain image, same
		// reasoning as the postProcess* fields above: this pass's framebuffer
		// for image N could still be draining GPU work from a previous frame
		// while frame N+1 (targeting a different swapchain image) starts
		// drawing into its own copy. Allocated unconditionally alongside the
		// swapchain (gl_outline is unlatched, changeable without vid_restart
		// -- same reasoning as postProcess* above).
		VkImage* worldNormalsColorImages;
		VkDeviceMemory* worldNormalsColorImageMemory;
		VkImageView* worldNormalsColorImageViews;
		VkImage* worldNormalsDepthImages;
		VkDeviceMemory* worldNormalsDepthImageMemory;
		VkImageView* worldNormalsDepthImageViews;
		VkFramebuffer* worldNormalsFramebuffers;
		VkDescriptorPool worldNormalsDescriptorPool;
		VkDescriptorSet* worldNormalsDescriptorSets;
	} swapChain;
	struct {
		VkCommandPool commandPool;
		VkCommandBuffer* commandBuffers;
		// Acquire semaphore: signalled by vkAcquireNextImageKHR, waited on by the
		// frame's queue submit. Safe to index per frame-in-flight because the
		// inFlightFences[currentFrame] wait at the top of VK_BeginFrame already
		// guarantees the previous use of this frame slot's submit (the only
		// waiter of this semaphore) has completed before it is reused.
		VkSemaphore imageAvailableSemaphores[VK_MAX_FRAMES_IN_FLIGHT];
		// Present ("render finished") semaphore: signalled by the queue submit,
		// waited on by vkQueuePresentKHR for a SPECIFIC swapchain image. This
		// MUST be indexed by swapchain imageIndex, not frame-in-flight: when
		// frames-in-flight != imageCount the two counters drift, and a
		// per-frame semaphore can be re-signalled by a new submit while
		// vkQueuePresentKHR is still consuming it for a different image that
		// hasn't been re-acquired yet (Vulkan WSI hazard, VUID on semaphore
		// reuse). One per swapchain image removes that class of bug and is the
		// pattern the Vulkan docs recommend
		// (swapchain_semaphore_reuse.html). Allocated with imageCount entries in
		// VK_CreateFrameResources.
		VkSemaphore* renderFinishedSemaphores;
		VkFence inFlightFences[VK_MAX_FRAMES_IN_FLIGHT];
		VkFence* imageInFlightFences;
		uint32_t currentFrame;
		uint32_t imageIndex;
		qbool active;
	} frame;
	float clearColor[4];
} vk_options_t;

extern vk_options_t vk_options;

void VK_PrintGfxInfo(void);

#endif
