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

#include "r_renderer.h"
#include "tr_types.h"
#include "vk_local.h"

vk_options_t vk_options;

void VK_DrawImage(float x, float y, float width, float height, float tex_s, float tex_t, float tex_width, float tex_height, byte* color, int flags);
void VK_DrawRectangle(float x, float y, float width, float height, byte* color);
void VK_AdjustImages(int first, int last, float x_offset);
void VK_TextureLoadCubemapFace(texture_ref cubemap, r_cubemap_direction_id direction, const byte* data, int width, int height);
void VK_CreateLightmapTextures(void);
void VK_UploadLightmap(int textureUnit, int lightmapnum);
void VK_BuildLightmap(int lightmapnum);
void VK_DrawAlias3Model(entity_t* ent, qbool outline, qbool additive_pass);
void VK_DeleteVAOs(void);
void VK_GenVertexArray(r_vao_id vao, const char* name);
void VK_BindVertexArray(r_vao_id vao);
void VK_BindVertexArrayElementBuffer(r_vao_id vao, r_buffer_id ref);
qbool VK_VertexArrayCreated(r_vao_id vao);
qbool VK_InitialiseVAOHandling(void);

static void VK_NoOperation(void)
{
}

static void VK_NoOperationCvar(cvar_t* cvar)
{
	(void)cvar;
}

static void VK_NoOperationBool(qbool value)
{
	(void)value;
}

static void VK_NoOperationState(r_state_id state)
{
	R_ApplyRenderingState(state);
}

static void VK_NoOperationEntity(entity_t* ent)
{
	(void)ent;
}

static void VK_NoOperationFloat4(float v[4])
{
	(void)v;
}

static void VK_NoOperationViewport(int x, int y, int width, int height)
{
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}

static void VK_NoOperationModel(qbool vid_restart)
{
	(void)vid_restart;
}

static void VK_NoOperationAliasModel(model_t* model, aliashdr_t* hdr)
{
	(void)model;
	(void)hdr;
}

static void VK_NoOperationAliasFrame(entity_t* ent, model_t* model, int pose1, int pose2, texture_ref texture, texture_ref fb_texture, qbool outline, int effects, int render_effects, float lerpfrac)
{
	(void)ent;
	(void)model;
	(void)pose1;
	(void)pose2;
	(void)texture;
	(void)fb_texture;
	(void)outline;
	(void)effects;
	(void)render_effects;
	(void)lerpfrac;
}

static void VK_NoOperationSimpleItem(model_t* model, int skin, vec3_t origin, float scale, vec3_t up, vec3_t right)
{
	(void)model;
	(void)skin;
	(void)origin;
	(void)scale;
	(void)up;
	(void)right;
}

static void VK_NoOperationParticles(int count)
{
	(void)count;
}

static void VK_NoOperationLightmapSurface(msurface_t* surf, qbool world)
{
	(void)surf;
	(void)world;
}

static void VK_NoOperationBrushChain(model_t* model, entity_t* ent)
{
	(void)model;
	(void)ent;
}

static void VK_NoOperationBrushModel(entity_t* ent, qbool polygonOffset, qbool caustics)
{
	(void)ent;
	(void)polygonOffset;
	(void)caustics;
}

static int VK_BrushModelCopyVertToBuffer(model_t* mod, void* vbo_buffer_, int position, float* source, int lightmap, int material, float scaleS, float scaleT, msurface_t* surf, qbool has_fb_texture, qbool has_luma_texture)
{
	(void)mod;
	(void)vbo_buffer_;
	(void)source;
	(void)lightmap;
	(void)material;
	(void)scaleS;
	(void)scaleT;
	(void)surf;
	(void)has_fb_texture;
	(void)has_luma_texture;
	return position;
}

static qbool VK_False(void)
{
	return false;
}

static qbool VK_FalseFramebuffer(framebuffer_id id, int width, int height)
{
	(void)id;
	(void)width;
	(void)height;
	return false;
}

static const char* VK_DescriptiveString(void)
{
	return "Vulkan";
}

static void VK_Screenshot(byte* buffer, size_t size)
{
	memset(buffer, 0, size);
}

static size_t VK_ScreenshotWidth(void)
{
	return glConfig.vidWidth;
}

static size_t VK_ScreenshotHeight(void)
{
	return glConfig.vidHeight;
}

static void VK_ClearRenderingSurface(qbool clear_color)
{
	(void)clear_color;
}

static void VK_DestroyFrameResources(void)
{
	if (vk_options.logicalDevice == VK_NULL_HANDLE) {
		return;
	}

	if (vk_options.frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
		vkDestroySemaphore(vk_options.logicalDevice, vk_options.frame.imageAvailableSemaphore, NULL);
	}
	if (vk_options.frame.renderFinishedSemaphore != VK_NULL_HANDLE) {
		vkDestroySemaphore(vk_options.logicalDevice, vk_options.frame.renderFinishedSemaphore, NULL);
	}
	if (vk_options.frame.inFlightFence != VK_NULL_HANDLE) {
		vkDestroyFence(vk_options.logicalDevice, vk_options.frame.inFlightFence, NULL);
	}
	if (vk_options.frame.commandPool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(vk_options.logicalDevice, vk_options.frame.commandPool, NULL);
	}
	Q_free(vk_options.frame.commandBuffers);
	memset(&vk_options.frame, 0, sizeof(vk_options.frame));
}

static qbool VK_CreateFrameResources(void)
{
	VkCommandPoolCreateInfo poolInfo = { 0 };
	VkCommandBufferAllocateInfo allocInfo = { 0 };
	VkSemaphoreCreateInfo semaphoreInfo = { 0 };
	VkFenceCreateInfo fenceInfo = { 0 };

	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = VK_PhysicalDeviceGraphicsQueueFamilyIndex();
	if (vkCreateCommandPool(vk_options.logicalDevice, &poolInfo, NULL, &vk_options.frame.commandPool) != VK_SUCCESS) {
		return false;
	}

	vk_options.frame.commandBuffers = Q_malloc(vk_options.swapChain.imageCount * sizeof(vk_options.frame.commandBuffers[0]));
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = vk_options.frame.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = vk_options.swapChain.imageCount;
	if (vkAllocateCommandBuffers(vk_options.logicalDevice, &allocInfo, vk_options.frame.commandBuffers) != VK_SUCCESS) {
		VK_DestroyFrameResources();
		return false;
	}

	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	if (vkCreateSemaphore(vk_options.logicalDevice, &semaphoreInfo, NULL, &vk_options.frame.imageAvailableSemaphore) != VK_SUCCESS ||
		vkCreateSemaphore(vk_options.logicalDevice, &semaphoreInfo, NULL, &vk_options.frame.renderFinishedSemaphore) != VK_SUCCESS) {
		VK_DestroyFrameResources();
		return false;
	}

	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	if (vkCreateFence(vk_options.logicalDevice, &fenceInfo, NULL, &vk_options.frame.inFlightFence) != VK_SUCCESS) {
		VK_DestroyFrameResources();
		return false;
	}

	return true;
}

static qbool VK_RecreateSwapChain(void)
{
	if (vk_options.window == NULL || vk_options.logicalDevice == VK_NULL_HANDLE || vk_options.surface == VK_NULL_HANDLE) {
		return false;
	}

	vkDeviceWaitIdle(vk_options.logicalDevice);

	VK_DestroyFrameResources();
	VK_DestroySwapChain();

	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_options.physicalDevice, vk_options.surface, &vk_options.physicalDeviceSurfaceCapabilities) != VK_SUCCESS) {
		return false;
	}

	if (!VK_CreateSwapChain(vk_options.window, vk_options.instance, vk_options.surface)) {
		return false;
	}
	if (!VK_CreateSwapChainFramebuffers()) {
		return false;
	}
	if (!VK_CreateFrameResources()) {
		return false;
	}

	VK_HudSwapchainChanged();
	return true;
}

void VK_BeginFrame(void)
{
	VkResult result;
	VkCommandBufferBeginInfo beginInfo = { 0 };
	VkRenderPassBeginInfo renderPassInfo = { 0 };
	VkClearValue clearValue = { 0 };
	VkCommandBuffer commandBuffer;

	if (vk_options.logicalDevice == VK_NULL_HANDLE || vk_options.swapChain.handle == VK_NULL_HANDLE || vk_options.frame.active) {
		return;
	}

	vkWaitForFences(vk_options.logicalDevice, 1, &vk_options.frame.inFlightFence, VK_TRUE, UINT64_MAX);

	result = vkAcquireNextImageKHR(vk_options.logicalDevice, vk_options.swapChain.handle, UINT64_MAX, vk_options.frame.imageAvailableSemaphore, VK_NULL_HANDLE, &vk_options.frame.imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		VK_RecreateSwapChain();
		return;
	}
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		Con_DPrintf("vulkan: vkAcquireNextImageKHR failed: %d\n", result);
		return;
	}

	commandBuffer = vk_options.frame.commandBuffers[vk_options.frame.imageIndex];
	vkResetCommandBuffer(commandBuffer, 0);

	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		Con_DPrintf("vulkan: vkBeginCommandBuffer failed\n");
		return;
	}

	clearValue.color.float32[0] = vk_options.clearColor[0];
	clearValue.color.float32[1] = vk_options.clearColor[1];
	clearValue.color.float32[2] = vk_options.clearColor[2];
	clearValue.color.float32[3] = vk_options.clearColor[3] ? vk_options.clearColor[3] : 1.0f;

	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = VK_MainRenderPass();
	renderPassInfo.framebuffer = vk_options.swapChain.framebuffers[vk_options.frame.imageIndex];
	renderPassInfo.renderArea.offset.x = 0;
	renderPassInfo.renderArea.offset.y = 0;
	renderPassInfo.renderArea.extent = vk_options.swapChain.imageSize;
	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	vk_options.frame.active = true;
}

VkCommandBuffer VK_CurrentCommandBuffer(void)
{
	if (!vk_options.frame.active || !vk_options.frame.commandBuffers) {
		return VK_NULL_HANDLE;
	}
	return vk_options.frame.commandBuffers[vk_options.frame.imageIndex];
}

void VK_EndFrame(void)
{
	VkCommandBuffer commandBuffer;
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSubmitInfo submitInfo = { 0 };
	VkPresentInfoKHR presentInfo = { 0 };
	VkResult result;

	if (!vk_options.frame.active) {
		return;
	}

	commandBuffer = vk_options.frame.commandBuffers[vk_options.frame.imageIndex];
	vkCmdEndRenderPass(commandBuffer);
	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		vk_options.frame.active = false;
		Con_DPrintf("vulkan: vkEndCommandBuffer failed\n");
		return;
	}

	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &vk_options.frame.imageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &vk_options.frame.renderFinishedSemaphore;

	vkResetFences(vk_options.logicalDevice, 1, &vk_options.frame.inFlightFence);
	if (vkQueueSubmit(vk_options.graphicsQueue, 1, &submitInfo, vk_options.frame.inFlightFence) != VK_SUCCESS) {
		vk_options.frame.active = false;
		Con_DPrintf("vulkan: vkQueueSubmit failed\n");
		return;
	}

	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &vk_options.frame.renderFinishedSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &vk_options.swapChain.handle;
	presentInfo.pImageIndices = &vk_options.frame.imageIndex;

	result = vkQueuePresentKHR(vk_options.presentQueue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		VK_RecreateSwapChain();
	}
	else if (result != VK_SUCCESS) {
		Con_DPrintf("vulkan: vkQueuePresentKHR failed: %d\n", result);
	}

	vk_options.frame.active = false;
}

qbool VK_Initialise(SDL_Window* window)
{
	memset(&vk_options, 0, sizeof(vk_options));
	vk_options.window = window;
	vk_options.clearColor[3] = 1.0f;

	if (!VK_CreateInstance(window, &vk_options.instance)) {
		return false;
	}

	if (!VK_CreateWindowSurface(window, vk_options.instance, &vk_options.surface)) {
		VK_Shutdown(r_shutdown_full);
		return false;
	}

	if (!VK_SelectPhysicalDevice(vk_options.instance, vk_options.surface)) {
		VK_Shutdown(r_shutdown_full);
		return false;
	}

	if (!VK_CreateLogicalDevice(vk_options.instance)) {
		VK_Shutdown(r_shutdown_full);
		return false;
	}

	if (!VK_CreateSwapChain(window, vk_options.instance, vk_options.surface)) {
		VK_Shutdown(r_shutdown_full);
		return false;
	}

	if (!VK_RenderPassCreate()) {
		VK_Shutdown(r_shutdown_full);
		return false;
	}

	if (!VK_CreateSwapChainFramebuffers()) {
		VK_Shutdown(r_shutdown_full);
		return false;
	}

	if (!VK_CreateFrameResources()) {
		VK_Shutdown(r_shutdown_full);
		return false;
	}

	Con_Printf("Vulkan initialised successfully\n");
	return true;
}

void VK_Shutdown(r_shutdown_mode_t mode)
{
	if (mode != r_shutdown_reload) {
		if (vk_options.logicalDevice != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(vk_options.logicalDevice);
		}

		VK_HudResourcesShutdown();
		VK_TextureShutdown();
		VK_DestroyImmediateCommandPool();
		VK_DestroyFrameResources();

		VK_DestroySwapChain();

		VK_RenderPassDelete();

		if (vk_options.logicalDevice != VK_NULL_HANDLE) {
			vkDestroyDevice(vk_options.logicalDevice, NULL);
			vk_options.logicalDevice = VK_NULL_HANDLE;
		}

		if (vk_options.instance) {
			VK_DestroyWindowSurface(vk_options.instance, vk_options.surface);
			VK_ShutdownDebugCallback(vk_options.instance);
			vkDestroyInstance(vk_options.instance, NULL);
		}

		memset(&vk_options, 0, sizeof(vk_options));
	}

	// FIXME
}

void VK_PopulateConfig(void)
{
	memset(&renderer, 0, sizeof(renderer));

	glConfig.renderer_string = (const unsigned char*)vk_options.physicalDeviceProperties.deviceName;
	glConfig.vendor_string = (const unsigned char*)"Vulkan";
	glConfig.version_string = (const unsigned char*)"Vulkan 1.0";
	glConfig.glsl_version = (const unsigned char*)"SPIR-V";
	glConfig.majorVersion = VK_VERSION_MAJOR(vk_options.physicalDeviceProperties.apiVersion);
	glConfig.minorVersion = VK_VERSION_MINOR(vk_options.physicalDeviceProperties.apiVersion);
	glConfig.texture_units = 16;
	glConfig.max_texture_depth = 256;
	glConfig.max_3d_texture_size = 2048;
	glConfig.supported_features =
		R_SUPPORT_FRAMEBUFFERS |
		R_SUPPORT_RENDERING_SHADERS |
		R_SUPPORT_PRIMITIVERESTART |
		R_SUPPORT_MULTITEXTURING |
		R_SUPPORT_TEXTURE_SAMPLERS |
		R_SUPPORT_CUBE_MAPS |
		R_SUPPORT_FRAMEBUFFERS_SRGB;

#define VK_CvarForceRecompile             VK_NoOperationCvar
#define VK_PrintGfxInfo                   VK_PrintGfxInfo
#define VK_DescriptiveString              VK_DescriptiveString
#define VK_Viewport                       VK_NoOperationViewport
#define VK_InvalidateViewport             VK_NoOperation
#define VK_ApplyRenderingState            VK_NoOperationState
#define VK_PrepareModelRendering          VK_NoOperationModel
#define VK_PrepareAliasModel              VK_NoOperationAliasModel
#define VK_DrawSky                        VK_NoOperation
#define VK_DrawWorld                      VK_NoOperation
#define VK_DrawAliasFrame                 VK_NoOperationAliasFrame
#define VK_DrawAliasModelShadow           VK_NoOperationEntity
#define VK_DrawAliasModelPowerupShell     VK_NoOperationEntity
#define VK_DrawAlias3ModelPowerupShell    VK_NoOperationEntity
#define VK_DrawSpriteModel                VK_NoOperationEntity
#define VK_DrawSimpleItem                 VK_NoOperationSimpleItem
#define VK_DrawClassicParticles           VK_NoOperationParticles
#define VK_DrawDisc                       VK_NoOperation
#define VK_LightmapFrameInit              VK_NoOperation
#define VK_RenderDynamicLightmaps         VK_NoOperationLightmapSurface
#define VK_InvalidateLightmapTextures     VK_NoOperation
#define VK_LightmapShutdown               VK_NoOperation
#define VK_SetupGL                        VK_NoOperation
#define VK_ChainBrushModelSurfaces        VK_NoOperationBrushChain
#define VK_DrawBrushModel                 VK_NoOperationBrushModel
#define VK_BrushModelCopyVertToBuffer     VK_BrushModelCopyVertToBuffer
#define VK_ClearRenderingSurface          VK_ClearRenderingSurface
#define VK_DrawWaterSurfaces              VK_NoOperation
#define VK_ScreenDrawStart                VK_NoOperation
#define VK_EnsureFinished                 VK_NoOperation
#define VK_Begin2DRendering               VK_NoOperation
#define VK_IsFramebufferEnabled3D         VK_False
#define VK_RenderView                     VK_NoOperation
#define VK_PreRenderView                  VK_NoOperation
#define VK_PostProcessScreen              VK_NoOperation
#define VK_BrightenScreen                 VK_NoOperation
#define VK_PolyBlend                      VK_NoOperationFloat4
#define VK_TimeRefresh                    VK_NoOperation
#define VK_Screenshot                     VK_Screenshot
#define VK_ScreenshotWidth                VK_ScreenshotWidth
#define VK_ScreenshotHeight               VK_ScreenshotHeight
#define VK_Prepare3DSprites               VK_NoOperation
#define VK_Draw3DSprites                  VK_NoOperation
#define VK_Draw3DSpritesInline            VK_NoOperation
#define VK_RenderFramebuffers             VK_NoOperation
#define VK_FramebufferCreate              VK_FalseFramebuffer
#define VK_ProgramsInitialise             VK_NoOperation
#define VK_ProgramsShutdown               VK_NoOperationBool

#define RENDERER_METHOD(returntype, name, ...) renderer.name = VK_ ## name;
#include "r_renderer_structure.h"
#undef RENDERER_METHOD

	renderer.vaos_supported = VK_InitialiseVAOHandling();
}

#endif
