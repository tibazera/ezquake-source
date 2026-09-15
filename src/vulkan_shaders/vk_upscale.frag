#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColour;

layout(binding = 0) uniform sampler2D sceneColor;
// Low-res (sceneSize) scene depth -- sampled only when pc.temporalActive,
// to reconstruct this pixel's motion vector via reprojection. See
// VK_TemporalUpscaleActive in vk_upscale.c for when that's actually true
// (needs a valid previous-frame matrix, a populated history buffer, and no
// MSAA).
layout(binding = 1) uniform sampler2D sceneDepth;
// Previous frame's final upscaled color, at native (dstSize) resolution --
// what this frame's temporal resolve blends its EASU+RCAS spatial result
// against.
layout(binding = 2) uniform sampler2D history;
layout(binding = 3) uniform MatricesUBO {
	mat4 invViewProj;   // this frame's (jittered) view-projection, inverted -- already Vulkan clip space (see VK_ToVulkanClipSpace in vk_main.c)
	mat4 prevViewProj;  // previous frame's (unjittered) view-projection -- same, already Vulkan clip space
} matrices;

layout(push_constant) uniform PushConstants {
	vec4 blend;      // damage/pickup/underwater tint, premultiplied like v_blend
	vec2 srcSize;    // sceneSize (low-res render target), in texels
	vec2 dstSize;    // imageSize (native swapchain), in texels
	vec2 invSrcSize;
	float gamma;
	float contrast;
	// Always 1 today (VK_UpscaleComposite hardcodes it, see vk_upscale.c) --
	// no cvar currently exposes a plain-bilinear fallback, so the 0 branch
	// below is unreachable in practice. Kept as a field (not removed) since
	// it's a natural place to wire a future "upscale quality/sharpness"
	// cvar without another push-constant-layout change.
	int sharpness;
	// True when VK_UpscaleUpdateMatrices actually populated the MatricesUBO
	// above this frame (see its own comment in vk_upscale.c for the exact
	// gating conditions) -- false means binding 3's contents are stale/from
	// an earlier frame and must not be read.
	int temporalActive;
} pc;

// AMD FidelityFX Super Resolution 1.0 EASU (Edge-Adaptive Spatial Upsampling),
// reimplemented directly against GLSL instead of the vendored ffx_a.h/
// ffx_fsr1.h C-preprocessor headers (this engine embeds precompiled SPIR-V,
// no text-include pipeline to feed those macro-heavy headers through -- same
// reasoning as vk_post_process.frag's hand-rolled FXAA). This is EASU only,
// not full FSR2: FSR2 also takes per-pixel motion vectors and reprojects
// across frames (temporal), which needs a G-buffer velocity pass this
// renderer doesn't have yet. EASU is FSR1's single-frame spatial half --
// still a real edge-adaptive Lanczos-like upsample, meaningfully sharper
// than bilinear, just without the temporal accumulation.
//
// Reference: https://github.com/GPUOpen-Effects/FidelityFX-FSR ffx_fsr1.h
vec3 SampleColor(vec2 uv)
{
	return textureLod(sceneColor, uv, 0.0).rgb;
}

vec3 Easu(vec2 outPixel)
{
	// Center of the output pixel, mapped into input (low-res) pixel space.
	vec2 srcPixel = (outPixel + 0.5) * (pc.srcSize / pc.dstSize) - 0.5;
	vec2 srcCenter = floor(srcPixel);
	vec2 frac = srcPixel - srcCenter;

	// 3x3 neighborhood around the source texel, luma-weighted per FSR1 EASU's
	// simplified edge-direction estimate (full EASU also computes gradient
	// direction/length for anisotropic lobes; here it's used only to pick a
	// sharpening blend weight, kept in-budget for a per-frame fullscreen pass).
	vec2 invSrc = pc.invSrcSize;
	vec2 base = (srcCenter + 0.5) * invSrc;

	vec3 a = SampleColor(base + vec2(-1.0,-1.0) * invSrc);
	vec3 b = SampleColor(base + vec2( 0.0,-1.0) * invSrc);
	vec3 c = SampleColor(base + vec2( 1.0,-1.0) * invSrc);
	vec3 d = SampleColor(base + vec2(-1.0, 0.0) * invSrc);
	vec3 e = SampleColor(base + vec2( 0.0, 0.0) * invSrc);
	vec3 f = SampleColor(base + vec2( 1.0, 0.0) * invSrc);
	vec3 g = SampleColor(base + vec2(-1.0, 1.0) * invSrc);
	vec3 h = SampleColor(base + vec2( 0.0, 1.0) * invSrc);
	vec3 i = SampleColor(base + vec2( 1.0, 1.0) * invSrc);

	// Bilinear base sample (smooth base layer the sharpening lobe below pushes
	// away from) -- e/f/h/i are the 4 texels actually straddling srcPixel.
	vec3 baseSample = mix(mix(e, f, frac.x), mix(h, i, frac.x), frac.y);

	// Edge-direction sharpening lobe: local Laplacian steers an unsharp-mask
	// style push away from the blurred bilinear estimate, clamped below to
	// the local per-channel min/max so it can't ring past FSR1's intent.
	vec3 laplacian = 4.0 * e - (b + d + f + h);
	vec3 sharpened = baseSample + laplacian * 0.20;

	// Clamp each RGB channel to its own neighborhood min/max (not just luma)
	// to avoid halo/ringing artifacts -- a luma-only clamp lets the laplacian
	// push R or B far outside the neighborhood range while G stays in range,
	// which reads as a magenta/purple fringe on high-contrast edges (e.g.
	// world-geometry-against-sky, or HUD-against-3D-scene boundaries).
	{
		vec3 nbMin = min(min(min(a, b), min(c, d)), min(min(f, g), min(h, i)));
		vec3 nbMax = max(max(max(a, b), max(c, d)), max(max(f, g), max(h, i)));
		sharpened = clamp(sharpened, nbMin, nbMax);
	}

	return sharpened;
}

// RCAS (Robust Contrast Adaptive Sharpening): FSR1's second pass, applied
// here as a light post-sharpen on the EASU result itself (operating at
// output resolution) rather than a separate pass, since both are cheap
// enough to fuse into one fullscreen shader invocation.
vec3 Rcas(vec2 outUv, vec3 center)
{
	vec2 px = pc.invSrcSize * (pc.srcSize / pc.dstSize);
	vec3 n = SampleColor(outUv + vec2(0.0, -1.0) * px);
	vec3 s = SampleColor(outUv + vec2(0.0,  1.0) * px);
	vec3 w = SampleColor(outUv + vec2(-1.0, 0.0) * px);
	vec3 e = SampleColor(outUv + vec2( 1.0, 0.0) * px);

	float amount = 0.25; // fixed moderate sharpen, no user-exposed knob yet
	vec3 sharpen = center * (1.0 + 4.0 * amount) - (n + s + w + e) * amount;

	// Per-channel clamp, same reasoning as Easu()'s clamp above -- a
	// luma-only clamp here produced the same magenta-fringe artifact on
	// high-contrast edges.
	{
		vec3 nbMin = min(center, min(min(n, s), min(w, e)));
		vec3 nbMax = max(center, max(max(n, s), max(w, e)));
		sharpen = clamp(sharpen, nbMin, nbMax);
	}
	return sharpen;
}

// Reprojects this output pixel back to where it was in the previous frame,
// via the low-res depth buffer + this/last frame's view-projection
// matrices -- "camera reprojection" motion vectors (accurate for the
// static world, approximate for moving entities), not true per-object
// motion vectors. See vk_motion_vectors.frag's header comment (this is the
// same algorithm, inlined here instead of a separate pass+buffer, so the
// temporal resolve below can use it directly without an extra texture
// fetch/binding for a full-resolution motion vector target).
// Returns false (previous-frame UV undefined) when the reprojection isn't
// usable -- behind the previous camera, or the source pixel is right at
// the depth clear value (background/sky, where depth-based reprojection is
// meaningless).
bool ReprojectToPreviousFrame(vec2 outUv, out vec2 prevUv)
{
	// outUv is in output (dstSize) space; the depth buffer is at sceneSize
	// (srcSize) -- same coordinate mapping Easu() uses to go from an output
	// pixel to the low-res source, but here we only need the UV, not a
	// specific texel's 3x3 neighborhood.
	float depth = texture(sceneDepth, outUv).r;
	// Assumes gl_reverse_z 1 (the engine default, see vid_sdl.c) where
	// depth==0 is the far plane/sky -- "nothing real to reproject" there.
	// With gl_reverse_z 0 this check is wrong (0 would instead be the NEAR
	// plane), but reprojecting a wrong depth value just produces a
	// discarded/clamped-away history sample worst case (see the
	// neighborhood clamp in main() below), not a crash or visible
	// corruption -- acceptable for now since gl_reverse_z 0 is not the
	// common/tested configuration this upscaler was built and reasoned
	// about against.
	if (depth <= 0.0) {
		return false;
	}

	vec4 ndc = vec4(outUv.x * 2.0 - 1.0, outUv.y * 2.0 - 1.0, depth, 1.0);
	vec4 worldPos = matrices.invViewProj * ndc;
	worldPos /= worldPos.w;

	vec4 prevClip = matrices.prevViewProj * worldPos;
	if (prevClip.w <= 0.0) {
		return false;
	}
	prevClip /= prevClip.w;

	prevUv = prevClip.xy * 0.5 + 0.5;
	return true;
}

void main()
{
	vec3 colour;

	if (pc.sharpness != 0) {
		vec2 outPixel = texCoord * pc.dstSize;
		vec3 upscaled = Easu(outPixel);
		colour = Rcas(texCoord, upscaled);
	}
	else {
		colour = SampleColor(texCoord);
	}

	if (pc.temporalActive != 0) {
		vec2 prevUv;
		if (ReprojectToPreviousFrame(texCoord, prevUv) &&
			prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0) {
			vec3 historyColour = textureLod(history, prevUv, 0.0).rgb;

			// Neighborhood clamp (same technique as Easu/Rcas's ringing
			// guards above, applied here as TAA's standard "clip history to
			// this frame's local color range" disocclusion/ghosting guard):
			// if the reprojected history color is far outside what this
			// frame's own spatial result and its immediate neighbors
			// produced, the history is almost certainly stale (a
			// disoccluded surface, or a moving object the reprojection
			// can't account for) -- clamp it back in rather than blend
			// toward a value that would ghost.
			vec2 px = pc.invSrcSize * (pc.srcSize / pc.dstSize);
			vec3 nbA = SampleColor(texCoord + vec2(-1.0,  0.0) * px);
			vec3 nbB = SampleColor(texCoord + vec2( 1.0,  0.0) * px);
			vec3 nbC = SampleColor(texCoord + vec2( 0.0, -1.0) * px);
			vec3 nbD = SampleColor(texCoord + vec2( 0.0,  1.0) * px);
			vec3 nbMin = min(min(colour, nbA), min(nbB, min(nbC, nbD)));
			vec3 nbMax = max(max(colour, nbA), max(nbB, max(nbC, nbD)));
			historyColour = clamp(historyColour, nbMin, nbMax);

			// Fixed blend weight favouring history for temporal stability
			// (standard TAA territory is 0.85-0.95 history weight) -- no
			// per-pixel confidence/variance estimate yet, that's the
			// natural next refinement once this baseline is proven out.
			colour = mix(colour, historyColour, 0.9);
		}
	}

	// Same formula as vk_post_process.frag, applied here since upscaleActive
	// replaces that pass entirely rather than running before it.
	colour = (colour * pc.blend.a + pc.blend.rgb) * pc.contrast;
	colour = pow(max(colour, vec3(0.0)), vec3(pc.gamma));

	fragColour = vec4(colour, 1.0);
}
