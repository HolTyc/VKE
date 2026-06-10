#version 450

// Backrooms wallpaper: two-tone yellow vertical stripes + grime, world-space
// mapped so pooled/recycled wall segments tile seamlessly.

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

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; ++i) { v += a * vnoise(p); p *= 2.13; a *= 0.5; }
    return v;
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
    // Planar projection along the wall's facing axis keeps the pattern stable.
    vec2 uv = abs(N.x) > 0.5 ? vec2(fragWorldPos.z, fragWorldPos.y)
                             : vec2(fragWorldPos.x, fragWorldPos.y);

    // Pale cream wallpaper with vertical columns of chevron motifs in a muted
    // grey-green — matching the right-hand wall of the original 2002 photo.
    vec3 base = vec3(0.82, 0.76, 0.50);
    float col   = fract(uv.x / 0.17);                       // pattern columns, ~17 cm
    float band  = smoothstep(0.10, 0.22, col) * (1.0 - smoothstep(0.78, 0.90, col));
    float chev  = fract(uv.y / 0.26 + abs(col - 0.5) * 1.4); // chevrons within a column
    float motif = smoothstep(0.40, 0.50, chev) * (1.0 - smoothstep(0.60, 0.70, chev));
    base = mix(base, base * vec3(0.78, 0.82, 0.76), band * motif * 0.85);

    // Light mottling, slight scuffing near the floor, faint ceiling shadow line.
    base *= 0.90 + 0.10 * fbm(uv * 1.7);
    base *= mix(0.74, 1.0, smoothstep(0.0, 0.45, fragWorldPos.y));
    base *= mix(1.0, 0.90, smoothstep(2.6, 3.0, fragWorldPos.y));
    base *= push.albedo.rgb;

    vec3 V = normalize(ubo.camPos.xyz - fragWorldPos);
    vec3 result = shade(base, N, V);

    // Distance fog into murky yellow-brown darkness sells the endless space.
    float d = length(ubo.camPos.xyz - fragWorldPos);
    result = mix(result, vec3(0.060, 0.054, 0.028), smoothstep(6.0, 30.0, d));

    outColor = vec4(pow(result, vec3(1.0 / 2.2)), 1.0);
}
