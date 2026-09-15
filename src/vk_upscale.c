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
} vk_upscale_push_t;

extern const unsigned char vk_post_process_vert_spv[];
extern const unsigned int vk_post_process_vert_spv_len;
extern const unsigned char vk_upscale_frag_spv[];
extern const unsigned int vk_upscale_frag_spv_len;

static VkSampler upscaleSampler = VK_NULL_HANDLE;
static VkDescriptorSetLayout upscaleDescriptorSetLayout = VK_NULL_HANDLE;
static VkPipelineLayout upscalePipelineLayout = VK_NULL_HANDLE;
static VkPipeline upscalePipeline = VK_NULL_HANDLE;

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
	VkDescriptorSetLayoutBinding binding;
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

	if (upscaleDescriptorSetLayout == VK_NULL_HANDLE) {
		VK_InitialiseStructure(binding);
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VK_InitialiseStructure(layoutInfo);
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;
		if (vkCreateDescriptorSetLayout(vk_options.logicalDevice, &layoutInfo, NULL, &upscaleDescriptorSetLayout) != VK_SUCCESS) {
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

static VkDescriptorSet VK_UpscaleDescriptorSet(uint32_t imageIndex)
{
	VkDescriptorSet set;
	VkDescriptorSetAllocateInfo allocInfo;
	VkDescriptorImageInfo imageInfo;
	VkWriteDescriptorSet write;

	if (imageIndex >= vk_options.swapChain.imageCount || !vk_options.swapChain.postProcessColorImageViews) {
		return VK_NULL_HANDLE;
	}

	if (upscaleDescriptorSetCount != vk_options.swapChain.imageCount) {
		Q_free(upscaleDescriptorSets);
		upscaleDescriptorSets = (VkDescriptorSet*)Q_calloc(vk_options.swapChain.imageCount, sizeof(VkDescriptorSet));
		upscaleDescriptorSetCount = vk_options.swapChain.imageCount;
	}

	set = upscaleDescriptorSets[imageIndex];
	if (set != VK_NULL_HANDLE) {
		return set;
	}

	VK_InitialiseStructure(allocInfo);
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = vk_options.swapChain.postProcessDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &upscaleDescriptorSetLayout;

	if (vkAllocateDescriptorSets(vk_options.logicalDevice, &allocInfo, &set) != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}

	VK_InitialiseStructure(imageInfo);
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = vk_options.swapChain.postProcessColorImageViews[imageIndex];
	imageInfo.sampler = upscaleSampler;

	VK_InitialiseStructure(write);
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.dstBinding = 0;
	write.dstArrayElement = 0;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.descriptorCount = 1;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(vk_options.logicalDevice, 1, &write, 0, NULL);

	upscaleDescriptorSets[imageIndex] = set;
	return set;
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
void VK_UpscaleComposite(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	extern cvar_t v_gamma, v_contrast;
	extern cvar_t vid_software_palette;
	VkDescriptorSet descriptorSet;
	vk_upscale_push_t push;
	VkExtent2D srcSize = vk_options.swapChain.sceneSize;
	VkExtent2D dstSize = vk_options.swapChain.imageSize;
	qbool paletteActive = vid_software_palette.integer != 0;

	if (!VK_UpscaleCreatePipeline()) {
		return;
	}

	descriptorSet = VK_UpscaleDescriptorSet(imageIndex);
	if (descriptorSet == VK_NULL_HANDLE) {
		return;
	}

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

	VK_HudSetViewportScissor(commandBuffer);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, upscalePipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, upscalePipelineLayout, 0, 1, &descriptorSet, 0, NULL);
	vkCmdPushConstants(commandBuffer, upscalePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}
