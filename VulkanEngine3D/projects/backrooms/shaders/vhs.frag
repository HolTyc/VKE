#version 450

// VHS tape look, composited over the offscreen scene texture:
//   * tracking distortion: a noise band crawling up the frame that shears
//     scanlines sideways and dissolves into static
//   * per-line tape jitter + a slow horizontal wave (worn transport)
//   * chromatic aberration (R/B pulled apart horizontally)
//   * chroma bleed: NTSC-style low chroma bandwidth (YIQ, I/Q blurred
//     horizontally) + desaturation and lifted blacks
//   * scanlines + interlace flicker
//   * film grain / static keyed off uTime, white speckle dropouts
//   * head-switching noise bar at the bottom of the frame, soft vignette
//
// push.timeRes: x = time (s), y = master strength 0..1, z/w = resolution.
// push.params:  x = extra glitch burst 0..1 (Captain Clark interference).

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform Push {
    vec4 timeRes; // x = time, y = strength, z = width, w = height
    vec4 params;  // x = glitch burst
} push;

float hash11(float p) {
    return fract(sin(p * 127.1) * 43758.5453);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Value noise, for the smooth wobble of the tracking band.
float vnoise(float x) {
    float i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash11(i), hash11(i + 1.0), f);
}

vec3 rgb2yiq(vec3 c) {
    return vec3(dot(c, vec3(0.299,  0.587,  0.114)),
                dot(c, vec3(0.596, -0.274, -0.322)),
                dot(c, vec3(0.211, -0.523,  0.312)));
}

vec3 yiq2rgb(vec3 c) {
    return vec3(c.x + 0.956 * c.y + 0.621 * c.z,
                c.x - 0.272 * c.y - 0.647 * c.z,
                c.x - 1.106 * c.y + 1.703 * c.z);
}

void main() {
    float time     = push.timeRes.x;
    float strength = clamp(push.timeRes.y + push.params.x, 0.0, 1.5);
    vec2  res      = push.timeRes.zw;
    float texel    = 1.0 / res.x;

    if (strength <= 0.001) {
        outColor = vec4(texture(uScene, vUV).rgb, 1.0);
        return;
    }

    vec2 uv = vUV;
    float line = uv.y * res.y; // scanline index

    // ---- tracking band: crawls up the screen, repeats every few seconds ----
    float bandPos   = 1.0 - fract(time * 0.13 + 7.0 * hash11(floor(time * 0.13)));
    float band      = exp(-abs(uv.y - bandPos) * 90.0); // narrow spike around the band
    float bigGlitch = step(0.997, hash11(floor(time * 8.0))) // rare full-frame tear
                      + push.params.x;

    // ---- horizontal displacement -------------------------------------------
    float wave   = sin(uv.y * 9.0 + time * 1.7) * 0.0012;          // tape wander
    float jitter = (hash21(vec2(floor(line), floor(time * 60.0))) - 0.5)
                   * 0.0018;                                       // per-line jitter
    float tear   = band * (vnoise(uv.y * 200.0 + time * 30.0) - 0.5) * 0.12;
    float shear  = bigGlitch * (hash21(vec2(floor(line / 6.0), floor(time * 24.0))) - 0.5)
                   * 0.05;
    uv.x += (wave + jitter + tear + shear) * strength;

    // Head-switching noise: the bottom ~2% of a VHS frame is always torn.
    float headSwitch = smoothstep(0.985, 1.0, uv.y);
    uv.x += headSwitch * (hash21(vec2(floor(line), floor(time * 47.0))) - 0.5)
            * 0.25 * strength;

    // ---- chromatic aberration + chroma bleed (YIQ) --------------------------
    float ca = (1.6 + 2.0 * band + 4.0 * bigGlitch) * texel * strength;
    float y  = rgb2yiq(vec3(texture(uScene, uv).r,
                            texture(uScene, uv + vec2(ca, 0.0)).g,
                            texture(uScene, uv + vec2(-ca, ca * 0.5)).b)).x;

    // Chroma sampled at a fraction of luma bandwidth: average a few taps
    // smeared to the left, like the ~0.4 MHz chroma channel of composite tape.
    vec2 chroma = vec2(0.0);
    const float taps = 5.0;
    for (float i = 0.0; i < taps; ++i) {
        vec2 cuv = uv - vec2(i * 2.5 * texel * strength, 0.0);
        chroma += rgb2yiq(texture(uScene, cuv).rgb).yz * (1.0 - i / taps);
    }
    chroma /= (taps + 1.0) * 0.5;

    // Desaturate and shift slightly toward warm tape phosphor.
    chroma *= mix(1.0, 0.72, strength);
    vec3 color = yiq2rgb(vec3(y, chroma));
    color = mix(color, color * vec3(1.04, 1.0, 0.92), 0.5 * strength);

    // ---- luma mangling -------------------------------------------------------
    // Lifted blacks + soft knee on highlights: tape never reaches true black.
    color = mix(color, color * 0.92 + 0.035, strength);

    // Scanlines locked to display rows (a time term inside sin(line*PI + t)
    // would only modulate the alternation's amplitude, periodically erasing
    // it) + 30 Hz interlace flicker for temporal life.
    float scan = 1.0 - 0.13 * step(0.5, fract(line * 0.5));
    float interlace = 1.0 - 0.04 * step(0.5, fract(line * 0.5 + floor(time * 29.97) * 0.5));
    color *= mix(1.0, scan * interlace, 0.6 * strength);

    // ---- noise ---------------------------------------------------------------
    float grain = hash21(vUV * res + vec2(fract(time * 13.7) * 311.0)) - 0.5;
    color += grain * (0.05 + 0.45 * band + 0.6 * headSwitch + 0.3 * bigGlitch) * strength;

    // Sparse white speckles (dropouts on the tape).
    float speckle = hash21(vec2(floor(line), floor(vUV.x * res.x * 0.25) + floor(time * 90.0)));
    color += step(0.9994 - 0.0015 * strength, speckle) * 0.85;

    // ---- vignette ------------------------------------------------------------
    vec2 d = vUV - 0.5;
    color *= 1.0 - dot(d, d) * 0.55 * strength;

    // Every term above is already scaled by strength; the final blend uses a
    // steeper curve so it acts as a fade-out near zero, not a second dilution.
    outColor = vec4(mix(texture(uScene, vUV).rgb, color, clamp(strength * 1.6, 0.0, 1.0)), 1.0);
}
