#version 450

// NOTE: this file is currently NOT wired into the build/pipeline -- see
// vk_upscale.frag's inlined ReprojectToPreviousFrame for the actual live
// version of this algorithm (kept in sync manually; this standalone file is
// the reference/fallback if a separate motion-vector target is ever needed
// again, e.g. for a future true-per-object-velocity pass).
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

layout(push_constant) uniform PushConstants {
	mat4 invViewProj;   // this frame's (jittered) view-projection, inverted -- already Vulkan clip space (see VK_ToVulkanClipSpace in vk_main.c)
	mat4 prevViewProj;  // previous frame's (unjittered) view-projection -- same, already Vulkan clip space
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
	vec4 ndc = vec4(texCoord.x * 2.0 - 1.0, texCoord.y * 2.0 - 1.0, depth, 1.0);
	vec4 worldPos = pc.invViewProj * ndc;
	worldPos /= worldPos.w;

	vec4 prevClip = pc.prevViewProj * worldPos;
	if (prevClip.w <= 0.0) {
		// Behind the previous frame's camera (e.g. just spawned into view,
		// or a very fast turn) -- no sane reprojection, report no motion
		// rather than an inverted/garbage vector.
		motionVector = vec2(0.0);
		return;
	}
	prevClip /= prevClip.w;

	vec2 prevUV = prevClip.xy * 0.5 + 0.5;
	motionVector = texCoord - prevUV;
}
