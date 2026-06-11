#include "vke/Mesh.hpp"

#include "vke/vulkan/Context.hpp"

#include <tiny_obj_loader.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
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

struct DecodedImage {
    int width = 0;
    int height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{nullptr, stbi_image_free};

    explicit operator bool() const { return pixels != nullptr && width > 0 && height > 0; }
};

DecodedImage loadBaseColorTexture(const cgltf_primitive& prim,
                                   const std::filesystem::path& modelDir) {
    DecodedImage image;
    if (!prim.material) return image;

    const cgltf_texture* texture =
        prim.material->pbr_metallic_roughness.base_color_texture.texture;
    if (!texture || !texture->image) return image;

    int channels = 0;
    if (const cgltf_buffer_view* view = texture->image->buffer_view) {
        const stbi_uc* bytes = static_cast<const stbi_uc*>(view->data);
        if (!bytes && view->buffer && view->buffer->data)
            bytes = reinterpret_cast<const stbi_uc*>(
                static_cast<const char*>(view->buffer->data) + view->offset);
        if (!bytes || view->size == 0 || view->size > static_cast<cgltf_size>(INT_MAX))
            return image;

        image.pixels.reset(stbi_load_from_memory(bytes, static_cast<int>(view->size),
                                                 &image.width, &image.height,
                                                 &channels, 4));
    } else if (texture->image->uri && texture->image->uri[0] != '\0') {
        std::filesystem::path imagePath = modelDir / texture->image->uri;
        image.pixels.reset(stbi_load(imagePath.string().c_str(), &image.width, &image.height,
                                     &channels, 4));
    }

    if (!image.pixels) {
        image.width = 0;
        image.height = 0;
    }
    return image;
}

glm::vec3 sampleBaseColor(const DecodedImage& image, glm::vec2 uv, const float factor[4]) {
    glm::vec3 color{factor[0], factor[1], factor[2]};
    if (!image) return color;

    float u = uv.x - std::floor(uv.x);
    float v = uv.y - std::floor(uv.y);
    int x = std::clamp(static_cast<int>(u * static_cast<float>(image.width - 1)),
                       0, image.width - 1);
    int y = std::clamp(static_cast<int>(v * static_cast<float>(image.height - 1)),
                       0, image.height - 1);

    const stbi_uc* pixel = image.pixels.get() + (y * image.width + x) * 4;
    return color * glm::vec3{pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f};
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

// ------------------------------------------------------------------ GLB load

std::shared_ptr<Mesh> Mesh::loadGLB(VulkanContext& ctx, const std::string& path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
        throw std::runtime_error("Failed to load GLB '" + path + "'");

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh) continue;

        // Skeletal stub: skins, joints, weights and animations are parsed by
        // cgltf but deliberately not consumed — the mesh is baked at bind
        // pose. Per the glTF spec a skinned node's own transform is ignored.
        glm::mat4 world{1.0f};
        if (!node.skin) {
            float m[16];
            cgltf_node_transform_world(&node, m);
            world = glm::make_mat4(m);
        }
        glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(world)));

        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
            const cgltf_primitive& prim = node.mesh->primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            const cgltf_accessor* pos = nullptr;
            const cgltf_accessor* nrm = nullptr;
            const cgltf_accessor* uv  = nullptr;
            const cgltf_accessor* col = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& attr = prim.attributes[a];
                if (attr.index != 0) continue; // only set 0 (skip JOINTS_1, ...)
                switch (attr.type) {
                case cgltf_attribute_type_position: pos = attr.data; break;
                case cgltf_attribute_type_normal:   nrm = attr.data; break;
                case cgltf_attribute_type_texcoord: uv  = attr.data; break;
                case cgltf_attribute_type_color:    col = attr.data; break;
                default: break; // joints/weights/tangents — skeletal stub
                }
            }
            if (!pos) continue;

            const DecodedImage baseColorTexture = loadBaseColorTexture(prim, std::filesystem::path(path).parent_path());
            const float defaultBaseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            const float* baseColorFactor = prim.material
                ? prim.material->pbr_metallic_roughness.base_color_factor
                : defaultBaseColor;

            const uint32_t base = static_cast<uint32_t>(vertices.size());
            for (cgltf_size i = 0; i < pos->count; ++i) {
                Vertex v{};
                float f[4] = {0, 0, 0, 1};
                cgltf_accessor_read_float(pos, i, f, 3);
                v.position = glm::vec3(world * glm::vec4(f[0], f[1], f[2], 1.0f));
                if (nrm) {
                    cgltf_accessor_read_float(nrm, i, f, 3);
                    v.normal = glm::normalize(normalMat * glm::vec3(f[0], f[1], f[2]));
                }
                if (uv) {
                    cgltf_accessor_read_float(uv, i, f, 2);
                    v.uv = {f[0], f[1]};
                }
                if (col) {
                    cgltf_accessor_read_float(col, i, f, 4);
                    v.color = {f[0], f[1], f[2]};
                }
                v.color *= sampleBaseColor(baseColorTexture, v.uv, baseColorFactor);
                vertices.push_back(v);
            }

            if (prim.indices) {
                for (cgltf_size i = 0; i < prim.indices->count; ++i)
                    indices.push_back(base + static_cast<uint32_t>(
                                                 cgltf_accessor_read_index(prim.indices, i)));
            } else {
                for (cgltf_size i = 0; i < pos->count; ++i)
                    indices.push_back(base + static_cast<uint32_t>(i));
            }
        }
    }

    cgltf_free(data);
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
