#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vke {

class VulkanContext;

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec3 color{1.0f};
    glm::vec2 uv{0.0f};

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions();
};

// GPU mesh: device-local vertex and index buffers uploaded through a staging buffer.
class Mesh {
public:
    Mesh(VulkanContext& ctx,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;

    uint32_t indexCount() const { return indexCount_; }
    uint32_t vertexCount() const { return vertexCount_; }

    // Loads a Wavefront .obj file (triangulated; normals computed if missing).
    static std::shared_ptr<Mesh> loadOBJ(VulkanContext& ctx, const std::string& path);

    // Built-in primitives (unit-sized, centered at the origin).
    static std::shared_ptr<Mesh> createCube(VulkanContext& ctx);
    static std::shared_ptr<Mesh> createPlane(VulkanContext& ctx);
    static std::shared_ptr<Mesh> createSphere(VulkanContext& ctx, int stacks = 24, int slices = 32);

private:
    VulkanContext& ctx_;

    VkBuffer       vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer       indexBuffer_  = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_  = VK_NULL_HANDLE;

    uint32_t vertexCount_ = 0;
    uint32_t indexCount_  = 0;
};

} // namespace vke
