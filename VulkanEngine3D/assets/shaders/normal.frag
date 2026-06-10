#version 450
// Debug shader: displays the interpolated world-space normal as color.

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
    vec4 params;
} push;

void main() {
    vec3 normalColor = normalize(fragNormal) * 0.5 + 0.5;
    outColor = vec4(normalColor * push.albedo.rgb * fragColor, push.albedo.a);
}
