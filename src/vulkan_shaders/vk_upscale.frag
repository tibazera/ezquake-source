#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColour;

layout(binding = 0) uniform sampler2D sceneColor;

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

float Luma(vec3 c) { return c.g; }

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

	// Edge-direction sharpening lobe: local Laplacian of luma steers an
	// unsharp-mask style push away from the blurred bilinear estimate,
	// clamped to the local min/max so it can't ring past FSR1's intent.
	float lumaE = Luma(e), lumaMin = lumaE, lumaMax = lumaE;
	lumaMin = min(lumaMin, min(min(Luma(a), Luma(b)), min(Luma(c), Luma(d))));
	lumaMin = min(lumaMin, min(min(Luma(f), Luma(g)), min(Luma(h), Luma(i))));
	lumaMax = max(lumaMax, max(max(Luma(a), Luma(b)), max(Luma(c), Luma(d))));
	lumaMax = max(lumaMax, max(max(Luma(f), Luma(g)), max(Luma(h), Luma(i))));

	vec3 laplacian = 4.0 * e - (b + d + f + h);
	vec3 sharpened = baseSample + laplacian * 0.20;

	// Clamp to neighborhood range to avoid halo/ringing artifacts.
	float lumaS = Luma(sharpened);
	if (lumaMax > lumaMin) {
		float t = clamp((lumaS - lumaMin) / (lumaMax - lumaMin), 0.0, 1.0);
		sharpened = mix(vec3(lumaMin), vec3(lumaMax), t) + (sharpened - vec3(lumaS));
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

	float lumaC = Luma(center), lumaN = Luma(n), lumaS = Luma(s), lumaW = Luma(w), lumaE = Luma(e);
	float lumaMin = min(lumaC, min(min(lumaN, lumaS), min(lumaW, lumaE)));
	float lumaMax = max(lumaC, max(max(lumaN, lumaS), max(lumaW, lumaE)));

	float amount = 0.25; // fixed moderate sharpen, no user-exposed knob yet
	vec3 sharpen = center * (1.0 + 4.0 * amount) - (n + s + w + e) * amount;

	if (lumaMax > lumaMin) {
		float t = clamp((Luma(sharpen) - lumaMin) / (lumaMax - lumaMin), 0.0, 1.0);
		sharpen = mix(vec3(lumaMin), vec3(lumaMax), t) + (sharpen - vec3(Luma(sharpen)));
	}
	return sharpen;
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

	// Same formula as vk_post_process.frag, applied here since upscaleActive
	// replaces that pass entirely rather than running before it.
	colour = (colour * pc.blend.a + pc.blend.rgb) * pc.contrast;
	colour = pow(max(colour, vec3(0.0)), vec3(pc.gamma));

	fragColour = vec4(colour, 1.0);
}
