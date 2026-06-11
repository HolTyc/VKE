#version 450

// Fullscreen pass: a single triangle covering the screen, generated from
// gl_VertexIndex — no vertex buffer is bound (see PostProcess::draw, 3 verts).

layout(location = 0) out vec2 vUV;

void main() {
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
