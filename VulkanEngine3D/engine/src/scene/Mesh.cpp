#include "vke/Mesh.hpp"

#include "vke/vulkan/Context.hpp"

#include <tiny_obj_loader.h>

#include <glm/gtc/constants.hpp>

#include <cstring>
#include <map>
#include <stdexcept>

namespace vke {

VkVertexInputBindingDescription Vertex::bindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 4> Vertex::attributeDescriptions() {
    return {{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
    }};
}

namespace {

// Uploads data into a device-local buffer through a host-visible staging buffer.
void uploadDeviceLocal(VulkanContext& ctx, const void* data, VkDeviceSize size,
                       VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    ctx.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     staging, stagingMemory);

    void* mapped = nullptr;
    vkMapMemory(ctx.device, stagingMemory, 0, size, 0, &mapped);
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(ctx.device, stagingMemory);

    ctx.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, memory);
    ctx.copyBuffer(staging, buffer, size);

    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, stagingMemory, nullptr);
}

} // namespace

Mesh::Mesh(VulkanContext& ctx,
           const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : ctx_(ctx),
      vertexCount_(static_cast<uint32_t>(vertices.size())),
      indexCount_(static_cast<uint32_t>(indices.size())) {
    if (vertices.empty() || indices.empty())
        throw std::runtime_error("Mesh requires non-empty vertex and index data");

    uploadDeviceLocal(ctx_, vertices.data(), sizeof(Vertex) * vertices.size(),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer_, vertexMemory_);
    uploadDeviceLocal(ctx_, indices.data(), sizeof(uint32_t) * indices.size(),
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer_, indexMemory_);
}

Mesh::~Mesh() {
    vkDestroyBuffer(ctx_.device, indexBuffer_, nullptr);
    vkFreeMemory(ctx_.device, indexMemory_, nullptr);
    vkDestroyBuffer(ctx_.device, vertexBuffer_, nullptr);
    vkFreeMemory(ctx_.device, vertexMemory_, nullptr);
}

void Mesh::bind(VkCommandBuffer cmd) const {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
}

// ------------------------------------------------------------------ OBJ load

std::shared_ptr<Mesh> Mesh::loadOBJ(VulkanContext& ctx, const std::string& path) {
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config))
        throw std::runtime_error("Failed to load OBJ '" + path + "': " + reader.Error());

    const auto& attrib = reader.GetAttrib();
    const bool hasNormals = !attrib.normals.empty();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto makeVertex = [&](const tinyobj::index_t& idx) {
        Vertex v{};
        v.position = {attrib.vertices[3 * idx.vertex_index + 0],
                      attrib.vertices[3 * idx.vertex_index + 1],
                      attrib.vertices[3 * idx.vertex_index + 2]};
        if (idx.vertex_index < static_cast<int>(attrib.colors.size() / 3))
            v.color = {attrib.colors[3 * idx.vertex_index + 0],
                       attrib.colors[3 * idx.vertex_index + 1],
                       attrib.colors[3 * idx.vertex_index + 2]};
        if (hasNormals && idx.normal_index >= 0)
            v.normal = {attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]};
        if (idx.texcoord_index >= 0)
            v.uv = {attrib.texcoords[2 * idx.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]};
        return v;
    };

    if (hasNormals) {
        // Deduplicate on the (position, normal, uv) index triple.
        std::map<std::array<int, 3>, uint32_t> unique;
        for (const auto& shape : reader.GetShapes()) {
            for (const auto& idx : shape.mesh.indices) {
                std::array<int, 3> key = {idx.vertex_index, idx.normal_index, idx.texcoord_index};
                auto it = unique.find(key);
                if (it == unique.end()) {
                    it = unique.emplace(key, static_cast<uint32_t>(vertices.size())).first;
                    vertices.push_back(makeVertex(idx));
                }
                indices.push_back(it->second);
            }
        }
    } else {
        // No normals in the file: emit flat-shaded triangles with face normals.
        for (const auto& shape : reader.GetShapes()) {
            for (size_t i = 0; i + 2 < shape.mesh.indices.size(); i += 3) {
                Vertex a = makeVertex(shape.mesh.indices[i]);
                Vertex b = makeVertex(shape.mesh.indices[i + 1]);
                Vertex c = makeVertex(shape.mesh.indices[i + 2]);
                glm::vec3 n = glm::normalize(
                    glm::cross(b.position - a.position, c.position - a.position));
                a.normal = b.normal = c.normal = n;
                uint32_t base = static_cast<uint32_t>(vertices.size());
                vertices.insert(vertices.end(), {a, b, c});
                indices.insert(indices.end(), {base, base + 1, base + 2});
            }
        }
    }

    return std::make_shared<Mesh>(ctx, vertices, indices);
}

// ----------------------------------------------------------------- primitives

std::shared_ptr<Mesh> Mesh::createCube(VulkanContext& ctx) {
    // 24 vertices: 4 per face with proper face normals. Side length 1.
    const float h = 0.5f;
    struct Face {
        glm::vec3 normal, u, v;
    };
    const Face faces[6] = {
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   // front
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, // back
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  // right
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  // left
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  // top
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},  // bottom
    };

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const auto& f : faces) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        const glm::vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int i = 0; i < 4; ++i) {
            Vertex v{};
            v.position = f.normal * h + (f.u * corners[i].x + f.v * corners[i].y) * h;
            v.normal = f.normal;
            v.uv = uvs[i];
            vertices.push_back(v);
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return std::make_shared<Mesh>(ctx, vertices, indices);
}

std::shared_ptr<Mesh> Mesh::createPlane(VulkanContext& ctx) {
    // 1x1 quad in the XZ plane, normal +Y.
    std::vector<Vertex> vertices = {
        {{-0.5f, 0, 0.5f}, {0, 1, 0}, {1, 1, 1}, {0, 1}},
        {{0.5f, 0, 0.5f}, {0, 1, 0}, {1, 1, 1}, {1, 1}},
        {{0.5f, 0, -0.5f}, {0, 1, 0}, {1, 1, 1}, {1, 0}},
        {{-0.5f, 0, -0.5f}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return std::make_shared<Mesh>(ctx, vertices, indices);
}

std::shared_ptr<Mesh> Mesh::createSphere(VulkanContext& ctx, int stacks, int slices) {
    // UV sphere, diameter 1.
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    const float pi = glm::pi<float>();

    for (int st = 0; st <= stacks; ++st) {
        float phi = pi * static_cast<float>(st) / static_cast<float>(stacks);
        for (int sl = 0; sl <= slices; ++sl) {
            float theta = 2.0f * pi * static_cast<float>(sl) / static_cast<float>(slices);
            glm::vec3 n = {std::sin(phi) * std::cos(theta), std::cos(phi),
                           std::sin(phi) * std::sin(theta)};
            Vertex v{};
            v.position = n * 0.5f;
            v.normal = n;
            v.uv = {static_cast<float>(sl) / slices, static_cast<float>(st) / stacks};
            vertices.push_back(v);
        }
    }

    const uint32_t cols = static_cast<uint32_t>(slices) + 1;
    for (int st = 0; st < stacks; ++st) {
        for (int sl = 0; sl < slices; ++sl) {
            uint32_t i0 = st * cols + sl;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + cols;
            uint32_t i3 = i2 + 1;
            indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    return std::make_shared<Mesh>(ctx, vertices, indices);
}

} // namespace vke
