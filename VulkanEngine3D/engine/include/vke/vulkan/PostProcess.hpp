#pragma once

#include "Context.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace vke {

// Push constants for the post-process fragment shader (mirror in GLSL).
struct PostFXPush {
    glm::vec4 timeRes; // x = time (s), y = strength 0..1, z = width, w = height
    glm::vec4 params;  // free per-effect tuning values (shader-defined)
};

// Offscreen scene target + fullscreen composite pipeline.
//
// While enabled, Renderer3D records the 3D scene into a private color/depth
// target (one per frame in flight) via scenePass(), then samples the color
// image with a fullscreen pipeline inside the swapchain's present pass. The
// scene pass uses the same attachment formats / sample counts as the
// swapchain pass, so it is render-pass compatible and every existing scene
// pipeline works in it unchanged.
class PostProcess {
public:
    PostProcess(VulkanContext& ctx,
                VkRenderPass presentPass, // swapchain pass the fullscreen quad draws in
                VkFormat colorFormat,     // = swapchain image format
                VkExtent2D extent,
                const std::string& vertSpvPath,
                const std::string& fragSpvPath);
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    // Device must be idle (Renderer3D::recreateSwapchain guarantees this).
    void resize(VkExtent2D extent);

    VkRenderPass  scenePass() const { return scenePass_; }
    VkFramebuffer framebuffer(uint32_t frame) const { return framebuffers_[frame]; }

    // Binds the fullscreen pipeline + the frame's scene texture and draws.
    void draw(VkCommandBuffer cmd, uint32_t frame, const PostFXPush& push) const;

private:
    void createScenePass();
    void createTargets();
    void destroyTargets();
    void createDescriptors();
    void updateDescriptors();
    void createPipeline(VkRenderPass presentPass,
                        const std::string& vertSpvPath,
                        const std::string& fragSpvPath);

    VulkanContext& ctx_;
    VkExtent2D extent_{};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    VkRenderPass scenePass_ = VK_NULL_HANDLE;

    // One target per frame in flight: two frames may be in the pipe at once,
    // so frame N+1 must not scribble over the image frame N is still sampling.
    std::vector<VkImage>        colorImages_;
    std::vector<VkDeviceMemory> colorMemory_;
    std::vector<VkImageView>    colorViews_;
    std::vector<VkImage>        depthImages_;
    std::vector<VkDeviceMemory> depthMemory_;
    std::vector<VkImageView>    depthViews_;
    std::vector<VkFramebuffer>  framebuffers_;

    VkSampler             sampler_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      pool_      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;
};

} // namespace vke
