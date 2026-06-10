#pragma once

#include "Context.hpp"

#include <string>
#include <vector>

namespace vke {

// A graphics pipeline built from a pair of SPIR-V shader modules.
// Viewport and scissor are dynamic, so pipelines survive window resizes.
class Pipeline {
public:
    Pipeline(VulkanContext& ctx,
             VkRenderPass renderPass,
             VkPipelineLayout layout,
             const std::string& vertSpvPath,
             const std::string& fragSpvPath);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void bind(VkCommandBuffer cmd) const;

private:
    static std::vector<char> readFile(const std::string& path);
    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    VulkanContext& ctx_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace vke
