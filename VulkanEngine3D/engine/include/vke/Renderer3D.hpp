#pragma once

#include "Core.hpp"
#include "Mesh.hpp"
#include "Scene.hpp"
#include "vulkan/Context.hpp"
#include "vulkan/Pipeline.hpp"
#include "vulkan/PostProcess.hpp"
#include "vulkan/Swapchain.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vke {

class Window;

// GPU-side light, std140-compatible with the GLSL Light struct.
struct GpuLight {
    glm::vec4 position{0.0f};            // xyz = position, w = type (0 dir, 1 point, 2 spot)
    glm::vec4 color{1.0f};               // rgb = color, w = intensity
    glm::vec4 direction{0, -1, 0, 10};   // xyz = forward, w = range
    glm::vec4 cone{0.0f};                // x = cos(innerAngle), y = cos(outerAngle) (spot only)
};

struct GlobalUBO {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec4 camPos{0.0f};
    glm::vec4 ambient{0.05f, 0.05f, 0.07f, 1.0f};
    GpuLight  lights[MAX_LIGHTS];
    alignas(16) int lightCount = 0;
};

struct PushData {
    glm::mat4 model{1.0f};
    glm::vec4 albedo{1.0f};
    glm::vec4 params{32.0f, 0.5f, 0.0f, 0.0f}; // x = shininess, y = specular
};

enum class Primitive { Cube, Sphere, Plane };

// Forward renderer with depth buffering. Hides the swapchain, command buffers
// and pipelines; exposes beginFrame / renderScene / endFrame plus resource
// creation (meshes, custom shaders).
class Renderer3D {
public:
    explicit Renderer3D(Window& window);
    ~Renderer3D();

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    // Returns VK_NULL_HANDLE when the frame must be skipped (swapchain rebuild).
    VkCommandBuffer beginFrame();
    void renderScene(Scene& scene);
    void endFrame();
    void waitIdle() const;

    // ---- resources -----------------------------------------------------
    std::shared_ptr<Mesh> primitive(Primitive p);
    // Loads a Wavefront .obj or binary glTF .glb model (chosen by extension).
    // Paths are relative to assets/ unless absolute.
    std::shared_ptr<Mesh> loadModel(const std::string& modelPath);

    // Registers a custom shader pair (compiled SPIR-V). Paths are relative to the
    // asset directory unless absolute. Materials reference the shader by name.
    void registerShader(const std::string& name,
                        const std::string& vertSpvPath,
                        const std::string& fragSpvPath);
    std::vector<std::string> shaderNames() const;

    // ---- post-processing -------------------------------------------------
    // Registers a fullscreen post-process shader pair (compiled SPIR-V, paths
    // resolved like registerShader) and enables it. While enabled the scene
    // renders into an offscreen texture which the shader composites onto the
    // swapchain; ImGui always draws on top, unfiltered.
    void setPostProcessShader(const std::string& vertSpvPath,
                              const std::string& fragSpvPath);
    void setPostProcessEnabled(bool enabled) { postEnabled_ = enabled; }
    bool postProcessEnabled() const { return postEnabled_ && post_ != nullptr; }

    // Ends the offscreen scene pass and draws the fullscreen quad into the
    // swapchain pass. Application::run calls this between renderScene and the
    // ImGui render; endFrame also invokes it as a safety net. No-op while
    // post-processing is disabled.
    void applyPostProcess();

    float postProcessTime     = 0.0f;  // seconds; drive from the game loop
    float postProcessStrength = 1.0f;  // 0 = clean image, 1 = full effect
    glm::vec4 postProcessParams{0.0f}; // extra shader-defined tuning values

    glm::vec4 clearColor{0.045f, 0.05f, 0.07f, 1.0f};
    glm::vec3 ambientLight{0.06f, 0.06f, 0.08f};

    // ---- advanced access (custom passes, ImGui, ...) --------------------
    VulkanContext& context() { return *ctx_; }
    Swapchain&     swapchain() { return *swapchain_; }
    VkCommandBuffer currentCommandBuffer() const { return commandBuffers_[frameIndex_]; }
    VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }

private:
    void createDescriptorResources();
    void recreateSwapchain();

    Window& window_;
    std::unique_ptr<VulkanContext> ctx_;
    std::unique_ptr<Swapchain> swapchain_;

    VkDescriptorSetLayout globalSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descriptorPool_  = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_  = VK_NULL_HANDLE;

    std::vector<VkBuffer>        uboBuffers_;
    std::vector<VkDeviceMemory>  uboMemory_;
    std::vector<void*>           uboMapped_;
    std::vector<VkDescriptorSet> globalSets_;
    std::vector<VkCommandBuffer> commandBuffers_;

    std::unordered_map<std::string, std::unique_ptr<Pipeline>> pipelines_;
    std::unordered_map<int, std::shared_ptr<Mesh>> primitives_;

    std::unique_ptr<PostProcess> post_;
    bool postEnabled_    = false;
    bool postPassActive_ = false; // this frame's scene pass renders offscreen

    uint32_t frameIndex_ = 0;
    uint32_t imageIndex_ = 0;
    bool frameStarted_ = false;
};

} // namespace vke
