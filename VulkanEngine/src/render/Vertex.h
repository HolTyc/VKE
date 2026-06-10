#pragma once

#include "core/Base.h"
#include <glm/glm.hpp>

#include <array>

// Vertex layout shared by the built-in sprite shader and any custom shader
// injected through Renderer2D::SetCustomPipeline. Custom GLSL must declare:
//   layout(location = 0) in vec2 inPosition;
//   layout(location = 1) in vec2 inUV;
//   layout(location = 2) in vec4 inColor;
struct QuadVertex {
    glm::vec2 Position;
    glm::vec2 UV;
    glm::vec4 Color;

    static VkVertexInputBindingDescription GetBindingDescription() {
        return { 0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX };
    }

    static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions() {
        return {{
            { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(QuadVertex, Position) },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(QuadVertex, UV) },
            { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadVertex, Color) },
        }};
    }
};
