#version 450

// Backrooms ceiling: faint 1 m tile seams, plus emissive 2x4 ft fluorescent
// troffers in rows (8 m pitch in x, 4 m in z). The panel layout / dead /
// flicker math MUST stay in sync with src/LevelGen.hpp (the game places
// matching point lights and the audio hum from the same formulas).
//
// push.albedo.a carries elapsed time in seconds (wrapped at 600) — the engine
// has no time uniform, so the game smuggles it through the material alpha
// (rendering is opaque-only, alpha is otherwise unused).

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

struct Light {
    vec4 position;   // xyz = world position, w = type (0 dir, 1 point, 2 spot)
    vec4 color;      // rgb = color, w = intensity
    vec4 direction;  // xyz = forward direction, w = range
    vec4 cone;       // x = cos(innerAngle), y = cos(outerAngle) (spot lights)
};

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 ambient;
    Light lights[8];
    int lightCount;
} ubo;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 albedo;
    vec4 params; // x = shininess, y = specular strength
} push;

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1, 0));
    float c = hash21(i + vec2(0, 1));
    float d = hash21(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Keep these three in sync with LevelGen.hpp (panelDead / panelHash / flicker).
float panelDead(vec2 c)  { return fract(sin(c.x * 39.3468 + c.y * 11.135) * 14375.5453); }
float panelHash(vec2 c)  { return fract(sin(c.x * 12.9898 + c.y * 78.233) * 43758.5453); }

float flicker(float t, float h) {
    if (h > 0.86) { // faulty tube: hard stutter
        float s = sin(t * 9.0 + h * 40.0) * sin(t * 23.0 + h * 70.0) * sin(t * 4.7 + h * 13.0);
        return s > -0.3 ? 1.0 : 0.12;
    }
    return 0.97 + 0.03 * sin(t * 7.0 + h * 30.0); // healthy tube: faint shimmer
}

vec3 shade(vec3 albedo, vec3 N, vec3 V) {
    vec3 result = ubo.ambient.rgb * albedo;
    for (int i = 0; i < ubo.lightCount; ++i) {
        Light l = ubo.lights[i];
        vec3 L;
        float atten = 1.0;
        if (l.position.w < 0.5) {
            L = normalize(-l.direction.xyz);
        } else {
            vec3 toLight = l.position.xyz - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);
            float range = max(l.direction.w, 1e-3);
            atten = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
            atten *= atten;
            if (l.position.w > 1.5) // spot: fade between inner and outer cone
                atten *= smoothstep(l.cone.y, l.cone.x, dot(-L, normalize(l.direction.xyz)));
        }
        float ndotl = max(dot(N, L), 0.0);
        vec3  H     = normalize(L + V);
        float spec  = pow(max(dot(N, H), 0.0), max(push.params.x, 1.0)) * push.params.y;
        result += (albedo * ndotl + vec3(spec) * ndotl) * l.color.rgb * l.color.w * atten;
    }
    return result;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec2 uv = fragWorldPos.xz;

    // Smooth pale ceiling with faint 1 m tile seams (the photo's ceiling is
    // nearly featureless white).
    vec2 tile = abs(fract(uv) - 0.5);
    float seam = smoothstep(0.475, 0.498, max(tile.x, tile.y));
    vec3 base = vec3(0.88, 0.86, 0.78) * (0.92 + 0.08 * vnoise(uv * 7.0));
    base = mix(base, base * 0.72, seam);
    base *= push.albedo.rgb;

    vec3 V = normalize(ubo.camPos.xyz - fragWorldPos);
    vec3 result = shade(base, N, V);

    // 2x4 ft troffers in rows: 8 m pitch in x (centres == 2 mod 8),
    // 4 m pitch in z (centres == 2 mod 4) — matches LevelGen.hpp.
    vec2 pc = vec2(floor((uv.x - 2.0) / 8.0 + 0.5) * 8.0 + 2.0,
                   floor((uv.y - 2.0) / 4.0 + 0.5) * 4.0 + 2.0);
    vec2 dp = uv - pc;
    if (abs(dp.x) < 0.62 && abs(dp.y) < 0.31) {
        // mod 512 keeps the sine-hash argument small enough for float32 to
        // agree with the C++ side far from the origin (pattern repeats @512 m).
        vec2 hc = mod(pc, 512.0);
        if (panelDead(hc) < 0.07) {
            result = vec3(0.05, 0.05, 0.055); // dead tube: dark panel
        } else {
            float t  = push.albedo.a;
            float fl = flicker(t, panelHash(hc));
            vec3 glow = vec3(1.55, 1.52, 1.38) * fl; // bright near-white tubes
            glow *= 0.93 + 0.07 * sin(dp.y * 22.0);  // two-lamp diffuser shading
            // thin frame around the lit area
            float frame = smoothstep(0.54, 0.62, abs(dp.x)) + smoothstep(0.25, 0.31, abs(dp.y));
            result = mix(glow, vec3(0.14, 0.14, 0.14), clamp(frame, 0.0, 1.0));
        }
    }

    float d = length(ubo.camPos.xyz - fragWorldPos);
    result = mix(result, vec3(0.030, 0.027, 0.014), smoothstep(6.0, 30.0, d));

    outColor = vec4(pow(result, vec3(1.0 / 2.2)), 1.0);
}
