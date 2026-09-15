// Vulkan renderer: spatial upscale pass (FSR2-style EASU+RCAS), used by
// VK_PostProcessComposite (vk_draw.c) in place of a plain blit when the
// scene is rendered at less than native resolution (vid_vulkan_renderscale
// < 1, see VK_ResolveSceneSize in vk_swapchain.c).
//
// This is deliberately named/structured to eventually host DLSS too (see
// vid_vulkan_upscaler), but only the FSR2-style spatial path is implemented
// here -- true temporal FSR2/DLSS need per-pixel motion vectors and a
// jittered projection matrix threaded through every 3D draw call
// (world/aliasmodel/sprite3d/particles), which this renderer doesn't
// produce yet. vk_upscale.frag's header comment has the full reasoning.
// DLSS additionally needs NVIDIA's proprietary NGX SDK (binary DLLs +
// developer registration), which isn't vendored in this tree, so
// vid_vulkan_upscaler 2 currently just runs the same FSR2-style shader
// (VK_UpscaleActive doesn't distinguish 1 vs 2) until real NGX integration
// lands.
//
// Future integration candidates named by the project owner (not vendored or
// inspected here -- verify licensing/API shape before pulling either in):
//   - https://github.com/GPUOpen-Effects/FidelityFX-FSR2 (real temporal
//     FSR2 -- swap in once motion vectors + jittered projection exist)
//   - https://github.com/NIGos/dlss5-bridge (proposed DLSS integration path
//     for vid_vulkan_upscaler 2)
//   - https://github.com/NVIDIA/DLSS (official NGX SDK, needs vendoring +
//     dev registration)
//   - https://github.com/nvpro-samples/vk_streamline (NVIDIA's reference
//     Vulkan integration of DLSS/Reflex -- closest existing example of how
//     the NGX calls should plug into a Vulkan frame loop like this one's)
//   - https://github.com/NVIDIA-RTX/Streamline (current umbrella SDK:
//     DLSS + Reflex + XeSS behind one plugin API -- likely the actual
//     integration point rather than raw NGX, worth evaluating first)

#include "quakedef.h"
#include "vk_local.h"

extern cvar_t vid_vulkan_upscaler;

// Layout must match vk_upscale.frag's PushConstants block exactly (vec4
// first, then vec2s, then scalars -- same ordering discipline as
// vk_post_process_push_t in vk_draw.c).
typedef struct vk_upscale_push_s {
	float blend[4];
	float srcSize[2];
	float dstSize[2];
	float invSrcSize[2];
	float gamma;
	float contrast;
	int sharpness;
	int temporalActive;
} vk_upscale_push_t;

// The two reprojection matrices don't fit in the push constant block above
// alongside everything else (128 bytes is the only size every Vulkan
// implementation is guaranteed to support, and two mat4 alone is already
// that much) -- a small UBO instead, binding 3 in the same descriptor set,
// updated once per frame via vkCmdUpdateBuffer right before the draw (128
// bytes fits vkCmdUpdateBuffer's own "small updates only" 65536-byte limit
// trivially, and this only happens once per frame, not per-object, so the
// usual per-draw-call UBO cost concern doesn't apply here).
typedef struct vk_upscale_matrices_s {
	float invViewProj[16];
	float prevViewProj[16];
} vk_upscale_matrices_t;

extern const unsigned char vk_post_process_vert_spv[];
extern const unsigned int vk_post_process_vert_spv_len;
extern const unsigned char vk_upscale_frag_spv[];
extern const unsigned int vk_upscale_frag_spv_len;

static VkSampler upscaleSampler = VK_NULL_HANDLE;
// Depth needs its own sampler (VK_FILTER_NEAREST -- bilinear-filtering a
// depth buffer produces meaningless blended-Z values at edges) and its own
// binding, separate from upscaleSampler above (used for sceneColor/history,
// both real color data where linear filtering is correct).
static VkSampler upscaleDepthSampler = VK_NULL_HANDLE;
static VkDescriptorSetLayout upscaleDescriptorSetLayout = VK_NULL_HANDLE;
static VkPipelineLayout upscalePipelineLayout = VK_NULL_HANDLE;
static VkPipeline upscalePipeline = VK_NULL_HANDLE;

// Persistent history buffer: the previous frame's final upscaled color, at
// native (imageSize) resolution -- NOT indexed by swapchain image index
// (that can reorder frame to frame, see the comment on
// VK_UpscaleUpdateHistory below) or frame-in-flight index, just one single
// image copied into every frame from that frame's final composite output.
static VkImage historyImage = VK_NULL_HANDLE;
static VkDeviceMemory historyImageMemory = VK_NULL_HANDLE;
static VkImageView historyImageView = VK_NULL_HANDLE;
static VkExtent2D historyImageSize;
// False for exactly one frame after the history image is (re)created --
// its contents are undefined until the first copy into it, so the shader
// must not blend against it yet. Mirrors vk_jitter_prevViewProjValid's
// same-shaped problem in vk_main.c.
static qbool historyValid;

// Per-frame reprojection matrices UBO (see vk_upscale_matrices_t) -- a
// single persistently-mapped host-visible buffer, updated once per frame
// via vkCmdUpdateBuffer, not per-swapchain-image like the color/depth
// samplers above (there's only one "current frame", so no double-buffering
// concern the way postProcessColorImages[] has to handle across images in
// flight).
static VkBuffer matricesBuffer = VK_NULL_HANDLE;
static VkDeviceMemory matricesBufferMemory = VK_NULL_HANDLE;

qbool VK_UpscaleActive(void)
{
	return vk_options.swapChain.upscaleActive && vid_vulkan_upscaler.integer != 0;
}

static qbool VK_UpscaleCreatePipeline(void)
{
	VkShaderModule vertShaderModule;
	VkShaderModule fragShaderModule;
	VkPipelineShaderStageCreateInfo shaderStages[2];
	VkPipelineVertexInputStateCreateInfo vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	VkPipelineViewportStateCreateInfo viewportState;
	VkPipelineRasterizationStateCreateInfo rasterizer;
	VkPipelineMultisampleStateCreateInfo multisampling;
	VkPipelineDepthStencilStateCreateInfo depthStencil;
	VkPipelineColorBlendStateCreateInfo colorBlending;
	VkPipelineColorBlendAttachmentState blending;
	VkPipelineDynamicStateCreateInfo dynamicState;
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPushConstantRange pushConstantRange;
	VkPipelineLayoutCreateInfo pipelineLayoutInfo;
	VkGraphicsPipelineCreateInfo pipelineInfo;
	VkSamplerCreateInfo samplerInfo;
	VkDescriptorSetLayoutBinding bindings[4];
	VkDescriptorSetLayoutCreateInfo layoutInfo;

	if (upscalePipeline != VK_NULL_HANDLE) {
		return true;
	}

	if (upscaleSampler == VK_NULL_HANDLE) {
		VK_InitialiseStructure(samplerInfo);
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		if (vkCreateSampler(vk_options.logicalDevice, &samplerInfo, NULL, &upscaleSampler) != VK_SUCCESS) {
			return false;
		}
	}

	if (upscaleDepthSampler == VK_NULL_HANDLE) {
		VK_InitialiseStructure(samplerInfo);
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		if (vkCreateSampler(vk_options.logicalDevice, &samplerInfo, NULL, &upscaleDepthSampler) != VK_SUCCESS) {
			return false;
		}
	}

	if (upscaleDescriptorSetLayout == VK_NULL_HANDLE) {
		// binding 0: sceneColor (low-res, sceneSize) -- EASU+RCAS spatial
		// upscale source, same as before temporal existed.
		VK_InitialiseStructure(bindings[0]);
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// binding 1: sceneDepth (low-res, sceneSize) -- motion-vector
		// reconstruction source. Only actually sampled when temporalActive
		// (see vk_upscale.frag), but always bound: a descriptor set can't
		// leave a binding empty once its layout declares it, and this keeps
		// the layout static rather than needing two variants.
		VK_InitialiseStructure(bindings[1]);
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// binding 2: history (native res, imageSize) -- previous frame's
		// final upscaled color, see historyImage above.
		VK_InitialiseStructure(bindings[2]);
		bindings[2].binding = 2;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// binding 3: reprojection matrices UBO (see vk_upscale_matrices_t).
		VK_InitialiseStructure(bindings[3]);
		bindings[3].binding = 3;
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[3].descriptorCount = 1;
		bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VK_InitialiseStructure(layoutInfo);
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 4;
		layoutInfo.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(vk_options.logicalDevice, &layoutInfo, NULL, &upscaleDescriptorSetLayout) != VK_SUCCESS) {
			return false;
		}
	}

	if (matricesBuffer == VK_NULL_HANDLE) {
		if (!VK_CreateBufferResource(sizeof(vk_upscale_matrices_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &matricesBuffer, &matricesBufferMemory)) {
			return false;
		}
	}

	vertShaderModule = VK_HudCreateShaderModule(vk_post_process_vert_spv, vk_post_process_vert_spv_len);
	fragShaderModule = VK_HudCreateShaderModule(vk_upscale_frag_spv, vk_upscale_frag_spv_len);
	if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
		if (vertShaderModule != VK_NULL_HANDLE) {
			vkDestroyShaderModule(vk_options.logicalDevice, vertShaderModule, NULL);
		}
		if (fragShaderModule != VK_NULL_HANDLE) {
			vkDestroyShaderModule(vk_options.logicalDevice, fragShaderModule, NULL);
		}
		return false;
	}

	VK_InitialiseStructure(shaderStages[0]);
	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].module = vertShaderModule;
	shaderStages[0].pName = "main";

	VK_InitialiseStructure(shaderStages[1]);
	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].module = fragShaderModule;
	shaderStages[1].pName = "main";

	VK_InitialiseStructure(vertexInputInfo);
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VK_InitialiseStructure(inputAssembly);
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VK_InitialiseStructure(viewportState);
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VK_InitialiseStructure(rasterizer);
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

	VK_InitialiseStructure(multisampling);
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VK_InitialiseStructure(depthStencil);
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;

	VK_BlendingConfigure(&colorBlending, &blending, r_blendfunc_overwrite);

	VK_InitialiseStructure(dynamicState);
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]);
	dynamicState.pDynamicStates = dynamicStates;

	if (upscalePipelineLayout == VK_NULL_HANDLE) {
		VK_InitialiseStructure(pushConstantRange);
		pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(vk_upscale_push_t);

		VK_InitialiseStructure(pipelineLayoutInfo);
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &upscaleDescriptorSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
		if (vkCreatePipelineLayout(vk_options.logicalDevice, &pipelineLayoutInfo, NULL, &upscalePipelineLayout) != VK_SUCCESS) {
			vkDestroyShaderModule(vk_options.logicalDevice, fragShaderModule, NULL);
			vkDestroyShaderModule(vk_options.logicalDevice, vertShaderModule, NULL);
			return false;
		}
	}

	VK_InitialiseStructure(pipelineInfo);
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = upscalePipelineLayout;
	pipelineInfo.renderPass = VK_PostProcessRenderPass();
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(vk_options.logicalDevice, vk_options.pipelineCache, 1, &pipelineInfo, NULL, &upscalePipeline) != VK_SUCCESS) {
		upscalePipeline = VK_NULL_HANDLE;
	}

	vkDestroyShaderModule(vk_options.logicalDevice, fragShaderModule, NULL);
	vkDestroyShaderModule(vk_options.logicalDevice, vertShaderModule, NULL);
	return upscalePipeline != VK_NULL_HANDLE;
}

// One descriptor set per swapchain image, same lifetime/ownership pattern as
// vk_options.swapChain.postProcessDescriptorSets -- allocated from that same
// pool (VK_CreatePostProcessResources already sizes it for one extra set per
// image; see vk_swapchain.c) and destroyed along with it, so no separate
// pool/array is needed here.
static VkDescriptorSet* upscaleDescriptorSets = NULL;
static uint32_t upscaleDescriptorSetCount = 0;

// Re-writes all 4 bindings every call rather than caching "already written"
// like the plain-color-only version of this function used to -- binding 1
// (sceneDepth) and binding 2 (history) point at images that can be
// recreated independently of the swapchain-image-indexed descriptor set
// itself (VK_UpscaleEnsureHistoryBuffer resizes historyImage on its own
// schedule, not tied to VK_CreatePostProcessResources), so a stale cached
// descriptor set could otherwise point at a destroyed VkImageView. This is
// a fixed 4-write vkUpdateDescriptorSets call once per frame, not a
// per-object cost.
static VkDescriptorSet VK_UpscaleDescriptorSet(uint32_t imageIndex)
{
	VkDescriptorSet set;
	VkDescriptorSetAllocateInfo allocInfo;
	VkDescriptorImageInfo colorImageInfo;
	VkDescriptorImageInfo depthImageInfo;
	VkDescriptorImageInfo historyImageInfo;
	VkDescriptorBufferInfo bufferInfo;
	VkWriteDescriptorSet writes[4];

	if (imageIndex >= vk_options.swapChain.imageCount || !vk_options.swapChain.postProcessColorImageViews) {
		return VK_NULL_HANDLE;
	}
	if (vk_options.swapChain.sceneDepthImageView == VK_NULL_HANDLE || historyImageView == VK_NULL_HANDLE || matricesBuffer == VK_NULL_HANDLE) {
		return VK_NULL_HANDLE;
	}

	if (upscaleDescriptorSetCount != vk_options.swapChain.imageCount) {
		Q_free(upscaleDescriptorSets);
		upscaleDescriptorSets = (VkDescriptorSet*)Q_calloc(vk_options.swapChain.imageCount, sizeof(VkDescriptorSet));
		upscaleDescriptorSetCount = vk_options.swapChain.imageCount;
	}

	set = upscaleDescriptorSets[imageIndex];
	if (set == VK_NULL_HANDLE) {
		VK_InitialiseStructure(allocInfo);
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = vk_options.swapChain.postProcessDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &upscaleDescriptorSetLayout;

		if (vkAllocateDescriptorSets(vk_options.logicalDevice, &allocInfo, &set) != VK_SUCCESS) {
			return VK_NULL_HANDLE;
		}
		upscaleDescriptorSets[imageIndex] = set;
	}

	VK_InitialiseStructure(colorImageInfo);
	colorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	colorImageInfo.imageView = vk_options.swapChain.postProcessColorImageViews[imageIndex];
	colorImageInfo.sampler = upscaleSampler;

	VK_InitialiseStructure(depthImageInfo);
	depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	depthImageInfo.imageView = vk_options.swapChain.sceneDepthImageView;
	depthImageInfo.sampler = upscaleDepthSampler;

	VK_InitialiseStructure(historyImageInfo);
	historyImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	historyImageInfo.imageView = historyImageView;
	historyImageInfo.sampler = upscaleSampler;

	VK_InitialiseStructure(bufferInfo);
	bufferInfo.buffer = matricesBuffer;
	bufferInfo.offset = 0;
	bufferInfo.range = sizeof(vk_upscale_matrices_t);

	VK_InitialiseStructure(writes[0]);
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[0].pImageInfo = &colorImageInfo;

	VK_InitialiseStructure(writes[1]);
	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[1].pImageInfo = &depthImageInfo;

	VK_InitialiseStructure(writes[2]);
	writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet = set;
	writes[2].dstBinding = 2;
	writes[2].descriptorCount = 1;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[2].pImageInfo = &historyImageInfo;

	VK_InitialiseStructure(writes[3]);
	writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[3].dstSet = set;
	writes[3].dstBinding = 3;
	writes[3].descriptorCount = 1;
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[3].pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(vk_options.logicalDevice, 4, writes, 0, NULL);

	return set;
}

static void VK_UpscaleDestroyHistoryBuffer(void)
{
	if (historyImageView != VK_NULL_HANDLE) {
		vkDestroyImageView(vk_options.logicalDevice, historyImageView, NULL);
		historyImageView = VK_NULL_HANDLE;
	}
	if (historyImage != VK_NULL_HANDLE) {
		vkDestroyImage(vk_options.logicalDevice, historyImage, NULL);
		historyImage = VK_NULL_HANDLE;
	}
	if (historyImageMemory != VK_NULL_HANDLE) {
		vkFreeMemory(vk_options.logicalDevice, historyImageMemory, NULL);
		historyImageMemory = VK_NULL_HANDLE;
	}
	historyImageSize.width = historyImageSize.height = 0;
	historyValid = false;
}

// (Re)creates historyImage at vk_options.swapChain.imageSize if it doesn't
// already exist at that size -- called every frame from
// VK_UpscaleComposite, cheap no-op once sized correctly. Sized at native
// imageSize (not sceneSize): the history holds the final upscaled result,
// same resolution the next frame's shader blends it against at output
// resolution.
static qbool VK_UpscaleEnsureHistoryBuffer(void)
{
	VkImageViewCreateInfo viewInfo;

	if (historyImage != VK_NULL_HANDLE &&
		historyImageSize.width == vk_options.swapChain.imageSize.width &&
		historyImageSize.height == vk_options.swapChain.imageSize.height) {
		return true;
	}

	VK_UpscaleDestroyHistoryBuffer();

	if (!VK_CreateImageResource(
			vk_options.swapChain.imageSize.width,
			vk_options.swapChain.imageSize.height,
			1,
			VK_SAMPLE_COUNT_1_BIT,
			vk_options.physicalDeviceSurfaceFormat.format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&historyImage,
			&historyImageMemory)) {
		return false;
	}

	VK_InitialiseStructure(viewInfo);
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = historyImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = vk_options.physicalDeviceSurfaceFormat.format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(vk_options.logicalDevice, &viewInfo, NULL, &historyImageView) != VK_SUCCESS) {
		VK_UpscaleDestroyHistoryBuffer();
		return false;
	}

	historyImageSize = vk_options.swapChain.imageSize;
	// Freshly created: VK_IMAGE_LAYOUT_UNDEFINED, contents undefined. The
	// first vkCmdCopyImage into it (VK_UpscaleUpdateHistory) transitions it
	// properly; historyValid staying false until after that copy is what
	// stops the shader from sampling it before then.
	return true;
}

// Copies this frame's final composited swapchain image into historyImage,
// so next frame's VK_UpscaleComposite has something to reproject against.
// Must run OUTSIDE any render pass instance (vkCmdCopyImage isn't valid
// inside one) -- called from VK_EndWorldPassAndComposite/VK_EndFrame right
// after vkCmdEndRenderPass ends the composite pass, before the HUD pass
// begins. Deliberately copies the pre-HUD composite output, not the final
// post-HUD frame: reprojecting HUD/console pixels next frame would smear
// UI elements that don't actually move with the camera.
void VK_UpscaleUpdateHistory(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	VkImageMemoryBarrier toTransferSrc;
	VkImageMemoryBarrier toTransferDst;
	VkImageMemoryBarrier toShaderRead;
	VkImageMemoryBarrier backToPresent;
	VkImageCopy region;

	if (!VK_UpscaleActive() || !VK_UpscaleEnsureHistoryBuffer()) {
		return;
	}
	if (imageIndex >= vk_options.swapChain.imageCount || !vk_options.swapChain.images) {
		return;
	}

	// Swapchain image: PRESENT_SRC_KHR (this render pass's finalLayout, see
	// VK_PostProcessRenderPassCreate) -> TRANSFER_SRC_OPTIMAL for the copy
	// read, then back to PRESENT_SRC_KHR afterwards (the HUD pass that
	// follows this call expects to LOAD it in that layout, same reasoning
	// as VK_PostProcessTransitionForSampling elsewhere in this file).
	VK_InitialiseStructure(toTransferSrc);
	toTransferSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toTransferSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	toTransferSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	toTransferSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransferSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransferSrc.image = vk_options.swapChain.images[imageIndex];
	toTransferSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toTransferSrc.subresourceRange.levelCount = 1;
	toTransferSrc.subresourceRange.layerCount = 1;
	toTransferSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	toTransferSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

	// historyImage: whatever it was left in last frame (SHADER_READ_ONLY
	// after that frame's VK_UpscaleComposite sampled it, or UNDEFINED if
	// just created) -> TRANSFER_DST_OPTIMAL for this frame's copy write.
	VK_InitialiseStructure(toTransferDst);
	toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toTransferDst.oldLayout = historyValid ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransferDst.image = historyImage;
	toTransferDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toTransferDst.subresourceRange.levelCount = 1;
	toTransferDst.subresourceRange.layerCount = 1;
	toTransferDst.srcAccessMask = 0;
	toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	{
		VkImageMemoryBarrier barriers[2] = { toTransferSrc, toTransferDst };
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, barriers);
	}

	VK_InitialiseStructure(region);
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.layerCount = 1;
	region.extent.width = vk_options.swapChain.imageSize.width;
	region.extent.height = vk_options.swapChain.imageSize.height;
	region.extent.depth = 1;
	vkCmdCopyImage(commandBuffer, vk_options.swapChain.images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, historyImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	VK_InitialiseStructure(toShaderRead);
	toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toShaderRead.image = historyImage;
	toShaderRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toShaderRead.subresourceRange.levelCount = 1;
	toShaderRead.subresourceRange.layerCount = 1;
	toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	VK_InitialiseStructure(backToPresent);
	backToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	backToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	backToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	backToPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	backToPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	backToPresent.image = vk_options.swapChain.images[imageIndex];
	backToPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	backToPresent.subresourceRange.levelCount = 1;
	backToPresent.subresourceRange.layerCount = 1;
	backToPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	backToPresent.dstAccessMask = 0;

	{
		VkImageMemoryBarrier barriers[2] = { toShaderRead, backToPresent };
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 2, barriers);
	}

	historyValid = true;
}

qbool VK_CreateUpscaleResources(void)
{
	return VK_UpscaleCreatePipeline();
}

// Called by VK_DestroyPostProcessDescriptors (vk_swapchain.c) right before
// it destroys postProcessDescriptorPool -- the pool destroy implicitly frees
// every set allocated from it (ours included), so this only needs to drop
// our host-side tracking array, not call vkFreeDescriptorSets.
void VK_UpscaleForgetDescriptorSets(void)
{
	Q_free(upscaleDescriptorSets);
	upscaleDescriptorSets = NULL;
	upscaleDescriptorSetCount = 0;
}

void VK_DestroyUpscaleResources(void)
{
	VK_UpscaleDestroyHistoryBuffer();
	if (matricesBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(vk_options.logicalDevice, matricesBuffer, NULL);
		matricesBuffer = VK_NULL_HANDLE;
	}
	if (matricesBufferMemory != VK_NULL_HANDLE) {
		vkFreeMemory(vk_options.logicalDevice, matricesBufferMemory, NULL);
		matricesBufferMemory = VK_NULL_HANDLE;
	}
	if (upscalePipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(vk_options.logicalDevice, upscalePipeline, NULL);
		upscalePipeline = VK_NULL_HANDLE;
	}
	if (upscalePipelineLayout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(vk_options.logicalDevice, upscalePipelineLayout, NULL);
		upscalePipelineLayout = VK_NULL_HANDLE;
	}
	if (upscaleDescriptorSetLayout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(vk_options.logicalDevice, upscaleDescriptorSetLayout, NULL);
		upscaleDescriptorSetLayout = VK_NULL_HANDLE;
	}
	if (upscaleSampler != VK_NULL_HANDLE) {
		vkDestroySampler(vk_options.logicalDevice, upscaleSampler, NULL);
		upscaleSampler = VK_NULL_HANDLE;
	}
	if (upscaleDepthSampler != VK_NULL_HANDLE) {
		vkDestroySampler(vk_options.logicalDevice, upscaleDepthSampler, NULL);
		upscaleDepthSampler = VK_NULL_HANDLE;
	}
	// Descriptor sets themselves are freed when postProcessDescriptorPool is
	// destroyed (VK_DestroyPostProcessDescriptors calls VK_UpscaleForgetDescriptorSets
	// for the host-side tracking array); nothing else to do with them here.
	VK_UpscaleForgetDescriptorSets();
}

// Draws the EASU+RCAS upscale of postProcessColorImages[imageIndex]
// (sceneSize) into whatever render pass/framebuffer the caller has already
// begun (the post-process composite pass, at imageSize) -- same calling
// convention as VK_PostProcessComposite. Falls back to doing nothing (caller
// should use VK_PostProcessComposite's plain-blit path instead) if the
// pipeline failed to build.
// True when the motion-vector/temporal-history path (not just the EASU+RCAS
// spatial upscale) should run this frame: needs a previous-frame matrix to
// reproject against (VK_PrevViewProjMatrix, false for exactly one frame
// after vid_restart or after upscaling first turns on), a valid history
// buffer with real content in it (not the frame it was just (re)created),
// and no MSAA (sampling multisampled depth needs a resolve step not
// implemented here, see the comment on sceneDepthImage's USAGE flags in
// vk_swapchain.c).
static qbool VK_TemporalUpscaleActive(void)
{
	return VK_PrevViewProjMatrix() != NULL && historyValid && vk_options.msaaSamples <= VK_SAMPLE_COUNT_1_BIT;
}

// Set by VK_UpscaleUpdateMatrices (called before the composite render pass
// begins) and read by VK_UpscaleComposite (called from inside it) -- see
// VK_UpscaleUpdateMatrices's own comment for why the matrix upload can't
// happen from inside VK_UpscaleComposite directly. Reset to false at the
// start of every VK_UpscaleUpdateMatrices call, so a frame where temporal
// upscaling isn't applicable (no history yet, MSAA on, etc.) correctly
// falls back to the spatial-only path without stale data from an earlier
// frame leaking through.
static qbool vk_upscale_matricesUpdatedThisFrame;

// Writes this frame's reprojection matrices into matricesBuffer via
// vkCmdUpdateBuffer -- must run outside any render pass instance (the spec
// forbids vkCmdUpdateBuffer inside one), so called from
// VK_EndWorldPassAndComposite/VK_EndFrame in vk_main.c BEFORE they begin the
// composite render pass, not from inside VK_UpscaleComposite itself (which
// always runs inside that render pass). Returns false (and does nothing)
// if temporal upscaling isn't actually going to run this frame -- the
// descriptor's binding 3 still points at matricesBuffer regardless, but
// VK_UpscaleComposite's temporalActive push constant gates the shader from
// ever reading stale/zero data in that case.
qbool VK_UpscaleUpdateMatrices(VkCommandBuffer commandBuffer)
{
	vk_upscale_matrices_t matrices;
	VkBufferMemoryBarrier toUniformRead;
	VkImageMemoryBarrier depthToShaderRead;

	vk_upscale_matricesUpdatedThisFrame = false;

	if (!VK_TemporalUpscaleActive() || matricesBuffer == VK_NULL_HANDLE) {
		return false;
	}
	if (!VK_CurrentInvViewProjMatrix(matrices.invViewProj)) {
		return false;
	}
	memcpy(matrices.prevViewProj, VK_PrevViewProjMatrix(), sizeof(matrices.prevViewProj));

	// sceneDepthImage's main-render-pass finalLayout is DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	// (VK_RenderPassCreateVariant, vk_renderpass.c) -- transition to
	// DEPTH_STENCIL_READ_ONLY_OPTIMAL for VK_UpscaleComposite's sampler read
	// below. No barrier back afterwards: like VK_PostProcessTransitionForSampling's
	// swapchain-image case, next frame's main pass attachment has
	// initialLayout=UNDEFINED and loadOp=CLEAR, so whatever layout this is
	// in when that pass starts doesn't matter.
	VK_InitialiseStructure(depthToShaderRead);
	depthToShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	depthToShaderRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthToShaderRead.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	depthToShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	depthToShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	depthToShaderRead.image = vk_options.swapChain.sceneDepthImage;
	depthToShaderRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthToShaderRead.subresourceRange.levelCount = 1;
	depthToShaderRead.subresourceRange.layerCount = 1;
	depthToShaderRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	depthToShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &depthToShaderRead);

	vkCmdUpdateBuffer(commandBuffer, matricesBuffer, 0, sizeof(matrices), &matrices);

	VK_InitialiseStructure(toUniformRead);
	toUniformRead.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	toUniformRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toUniformRead.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
	toUniformRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toUniformRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toUniformRead.buffer = matricesBuffer;
	toUniformRead.size = sizeof(matrices);
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 1, &toUniformRead, 0, NULL);

	vk_upscale_matricesUpdatedThisFrame = true;
	return true;
}

// Must be called BEFORE VK_UpscaleUpdateHistory overwrites historyValid's
// meaning for next frame -- this call's historyValid check is about
// whether THIS frame's composite can use last frame's history, separate
// from VK_UpscaleUpdateHistory setting historyValid true for the NEXT
// frame's use afterwards.
void VK_UpscaleComposite(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	extern cvar_t v_gamma, v_contrast;
	extern cvar_t vid_software_palette;
	VkDescriptorSet descriptorSet;
	vk_upscale_push_t push;
	VkExtent2D srcSize = vk_options.swapChain.sceneSize;
	VkExtent2D dstSize = vk_options.swapChain.imageSize;
	qbool paletteActive = vid_software_palette.integer != 0;
	qbool temporalActive;

	if (!VK_UpscaleCreatePipeline() || !VK_UpscaleEnsureHistoryBuffer()) {
		return;
	}

	descriptorSet = VK_UpscaleDescriptorSet(imageIndex);
	if (descriptorSet == VK_NULL_HANDLE) {
		return;
	}

	// vk_upscale_matricesUpdatedThisFrame, not VK_TemporalUpscaleActive()
	// again here -- must match exactly whether VK_UpscaleUpdateMatrices
	// actually succeeded in writing matricesBuffer earlier this frame
	// (before the composite render pass began; vkCmdUpdateBuffer can't run
	// inside one, which is why that call had to happen outside this
	// function, from VK_EndWorldPassAndComposite/VK_EndFrame in vk_main.c).
	// Re-deriving VK_TemporalUpscaleActive() here instead could theoretically
	// disagree with that earlier call's result and read uninitialised/stale
	// UBO contents.
	temporalActive = vk_upscale_matricesUpdatedThisFrame;

	// Same gate/formula as VK_PostProcessComposite -- this pass replaces
	// that one entirely when upscaleActive, so gamma/contrast has to happen
	// here instead (see the comment at VK_PostProcessComposite's call site).
	push.blend[0] = push.blend[1] = push.blend[2] = 0.0f;
	push.blend[3] = 1.0f;
	push.gamma = paletteActive ? bound(0.3f, v_gamma.value, 3.0f) : 1.0f;
	push.contrast = paletteActive ? bound(1.0f, v_contrast.value, 3.0f) : 1.0f;
	push.srcSize[0] = (float)max(1, srcSize.width);
	push.srcSize[1] = (float)max(1, srcSize.height);
	push.dstSize[0] = (float)max(1, dstSize.width);
	push.dstSize[1] = (float)max(1, dstSize.height);
	push.invSrcSize[0] = 1.0f / push.srcSize[0];
	push.invSrcSize[1] = 1.0f / push.srcSize[1];
	push.sharpness = 1;
	push.temporalActive = temporalActive ? 1 : 0;

	VK_HudSetViewportScissor(commandBuffer);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, upscalePipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, upscalePipelineLayout, 0, 1, &descriptorSet, 0, NULL);
	vkCmdPushConstants(commandBuffer, upscalePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}
