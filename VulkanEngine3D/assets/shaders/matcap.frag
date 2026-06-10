#version 450
// Matcap-style shader: shades from view-space normal direction instead of scene lights.

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

struct Light {
    vec4 position;
    vec4 color;
    vec4 direction;
    vec4 cone;
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
    vec4 params; // x = contrast, y = highlight strength
} push;

void main() {
    vec3 viewNormal = normalize(mat3(ubo.view) * normalize(fragNormal));
    vec2 matcapUv = viewNormal.xy * 0.5 + 0.5;

    vec3 base = push.albedo.rgb * fragColor;
    vec3 coolShadow = base * vec3(0.25, 0.32, 0.45);
    vec3 warmLight = mix(base, vec3(1.0, 0.92, 0.75), 0.35);

    float verticalLight = smoothstep(0.05, 1.0, matcapUv.y);
    float sideLight = smoothstep(0.1, 0.95, matcapUv.x) * 0.35;
    float contrast = max(push.params.x, 0.2);
    vec3 color = mix(coolShadow, warmLight, pow(verticalLight + sideLight, contrast));

    vec2 highlightCenter = vec2(0.36, 0.72);
    float highlight = 1.0 - smoothstep(0.0, 0.24, length(matcapUv - highlightCenter));
    color += vec3(1.0, 0.9, 0.65) * highlight * max(push.params.y, 0.15);

    outColor = vec4(pow(color, vec3(1.0 / 2.2)), push.albedo.a);
}
