#version 450
// Example of a user-injected custom shader (see Renderer3D::registerShader).

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragUV;

struct Light {
    vec4 position;
    vec4 color;
    vec4 direction;
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
    vec4 params;
} push;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos  = worldPos.xyz;
    fragNormal    = normalize(mat3(transpose(inverse(push.model))) * inNormal);
    fragColor     = inColor;
    fragUV        = inUV;
    gl_Position   = ubo.proj * ubo.view * worldPos;
}
