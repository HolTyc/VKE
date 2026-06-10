#pragma once

#include "core/Base.h"

// Graphics pipeline for 2D rendering. This is the advanced-user entry point
// for custom shaders: build one from your own SPIR-V files and hand it to
// Renderer2D::SetCustomPipeline(). The vertex layout (QuadVertex), descriptor
// set 0 (one combined image sampler) and the 64-byte vertex-stage push
// constant (view-projection matrix) are fixed contracts; everything else in
// the fragment shader is yours.
struct PipelineConfig {
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool AlphaBlend = true;
};

class Pipeline {
public:
    Pipeline(const std::string& vertSpvPath, const std::string& fragSpvPath,
             VkRenderPass renderPass, const PipelineConfig& config = {});
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    VkPipeline       GetHandle() const { return m_Pipeline; }
    VkPipelineLayout GetLayout() const { return m_Layout; }

private:
    static std::vector<char> ReadFile(const std::string& path);
    VkShaderModule CreateShaderModule(const std::vector<char>& code) const;

    VkPipeline       m_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
};
