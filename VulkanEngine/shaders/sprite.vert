#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform PushData {
    mat4 uViewProj;
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
    gl_Position = pc.uViewProj * vec4(inPosition, 0.0, 1.0);
    vUV = inUV;
    vColor = inColor;
}
