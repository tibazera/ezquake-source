#version 450

// Wired into the build/pipeline as of DLSS integration (vk_dlss.c) -- DLSS
// needs a real sampleable motion-vector buffer to tag (sl::kBufferTypeMotionVectors),
// unlike vk_upscale.frag's own temporal path which computes the equivalent
// reprojection inline without materializing a separate texture. Produced
// once per frame by VK_MotionVectorsComposite (vk_upscale.c) into a
// dedicated R16G16_SFLOAT target at sceneSize, only when DLSS is actually
// active (VK_DLSS_Active()) -- the FSR2-style temporal path's own inlined
// version (ReprojectToPreviousFrame in vk_upscale.frag) is unaffected and
// keeps working exactly as before whether or not this pass ever runs.
//
// Reconstructed motion vectors: reprojects each low-res scene pixel's world
// position (recovered from its depth value + this frame's inverse
// view-projection) back through the PREVIOUS frame's view-projection, and
// outputs the UV delta between where the pixel is now and where it was last
// frame. This is "camera reprojection" motion vectors, not true per-object
// motion vectors (which would need a previous-frame MVP per draw call, see
// vk_upscale.c's header comment for why that was ruled out) -- correct for
// the static world (the overwhelming majority of a Quake scene), off by
// however far a moving object (player, projectile) travelled that frame for
// pixels covering it. Good enough for a first temporal pass; a real
// per-object velocity buffer is the natural follow-up once this is proven
// out.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec2 motionVector;

layout(binding = 0) uniform sampler2D sceneDepth;
// Same UBO layout/binding as vk_upscale.frag's MatricesUBO -- this pass
// reads the identical per-frame reprojection matrices vk_upscale.c already
// maintains (VK_UpscaleUpdateMatrices), rather than duplicating them via a
// separate push-constant path.
layout(binding = 1) uniform MatricesUBO {
	mat4 invViewProj;
	mat4 prevViewProj;
} matrices;
layout(push_constant) uniform PushConstants {
	int reversedDepth; // mirrors glConfig.reversed_depth, see vk_upscale.frag's identical field
} pc;

void main()
{
	// Both matrices are pre-converted to Vulkan clip space on the C side
	// (VK_ToVulkanClipSpace), so texCoord/depth need no manual Y-flip or Z
	// remap here -- straight [0,1] screen UV to [-1,1] NDC xy, and the
	// sampled depth is already the real Vulkan [0,1] Z this pixel was
	// written with (correct for either reversed_depth or the regular
	// near/far range, since invViewProj was built from whatever the
	// projection matrix actually produced).
	float depth = texture(sceneDepth, texCoord).r;

	// Same reversed_depth-aware sky rejection as vk_upscale.frag's
	// ReprojectToPreviousFrame -- see its comment for why the threshold
	// flips between the two depth modes.
	if (pc.reversedDepth != 0) {
		if (depth <= 0.0) {
			motionVector = vec2(0.0);
			return;
		}
	}
	else {
		if (depth >= 1.0) {
			motionVector = vec2(0.0);
			return;
		}
	}

	vec4 ndc = vec4(texCoord.x * 2.0 - 1.0, texCoord.y * 2.0 - 1.0, depth, 1.0);
	vec4 worldPos = matrices.invViewProj * ndc;
	if (abs(worldPos.w) < 1e-6) {
		motionVector = vec2(0.0);
		return;
	}
	worldPos /= worldPos.w;

	vec4 prevClip = matrices.prevViewProj * worldPos;
	if (abs(prevClip.w) < 1e-6 || prevClip.w <= 0.0) {
		// Behind the previous frame's camera (e.g. just spawned into view,
		// or a very fast turn) -- no sane reprojection, report no motion
		// rather than an inverted/garbage vector.
		motionVector = vec2(0.0);
		return;
	}
	prevClip /= prevClip.w;

	vec2 prevUV = prevClip.xy * 0.5 + 0.5;
	vec2 result = texCoord - prevUV;

	// NaN/Inf guard, same reasoning as vk_upscale.frag's ReprojectToPreviousFrame.
	if (!(result.x == result.x) || !(result.y == result.y)) {
		motionVector = vec2(0.0);
		return;
	}
	motionVector = result;
}
