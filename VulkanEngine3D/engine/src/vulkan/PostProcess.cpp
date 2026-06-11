#include "vke/vulkan/PostProcess.hpp"

#include <array>
#include <fstream>

namespace vke {

namespace {

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Failed to open shader file: " + path);

    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
    return module;
}

} // namespace

PostProcess::PostProcess(VulkanContext& ctx, VkRenderPass presentPass,
                         VkFormat colorFormat, VkExtent2D extent,
                         const std::string& vertSpvPath,
                         const std::string& fragSpvPath)
    : ctx_(ctx), extent_(extent), colorFormat_(colorFormat) {
    depthFormat_ = ctx_.findDepthFormat();
    createScenePass();
    createTargets();
    createDescriptors();
    updateDescriptors();
    createPipeline(presentPass, vertSpvPath, fragSpvPath);
}

PostProcess::~PostProcess() {
    destroyTargets();
    vkDestroyPipeline(ctx_.device, pipeline_, nullptr);
    vkDestroyPipelineLayout(ctx_.device, pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(ctx_.device, pool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device, setLayout_, nullptr);
    vkDestroySampler(ctx_.device, sampler_, nullptr);
    vkDestroyRenderPass(ctx_.device, scenePass_, nullptr);
}

void PostProcess::resize(VkExtent2D extent) {
    extent_ = extent;
    destroyTargets();
    createTargets();
    updateDescriptors();
}

// Same attachment formats/sample counts as the swapchain pass (=> compatible
// with all scene pipelines); the color attachment ends up SHADER_READ_ONLY so
// the composite pass can sample it without an explicit barrier.
void PostProcess::createScenePass() {
    VkAttachmentDescription color{};
    color.format = colorFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // NOTE: byte-identical to Swapchain::createRenderPass — compatibility
    // between the two passes (scene pipelines are built against the swapchain
    // pass but recorded in this one) requires matching subpass dependencies.
    // Reuse of a target across frames needs no extra sync: there is one
    // target per frame in flight and the frame fence has already been waited.
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // Scene color writes -> composite pass sampling.
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {color, depth};
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(deps.size());
    info.pDependencies = deps.data();

    VK_CHECK(vkCreateRenderPass(ctx_.device, &info, nullptr, &scenePass_));
}

void PostProcess::createTargets() {
    colorImages_.resize(MAX_FRAMES_IN_FLIGHT);
    colorMemory_.resize(MAX_FRAMES_IN_FLIGHT);
    colorViews_.resize(MAX_FRAMES_IN_FLIGHT);
    depthImages_.resize(MAX_FRAMES_IN_FLIGHT);
    depthMemory_.resize(MAX_FRAMES_IN_FLIGHT);
    depthViews_.resize(MAX_FRAMES_IN_FLIGHT);
    framebuffers_.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        ctx_.createImage(extent_.width, extent_.height, colorFormat_,
                         VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         colorImages_[i], colorMemory_[i]);
        colorViews_[i] = ctx_.createImageView(colorImages_[i], colorFormat_,
                                              VK_IMAGE_ASPECT_COLOR_BIT);

        ctx_.createImage(extent_.width, extent_.height, depthFormat_,
                         VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         depthImages_[i], depthMemory_[i]);
        depthViews_[i] = ctx_.createImageView(depthImages_[i], depthFormat_,
                                              VK_IMAGE_ASPECT_DEPTH_BIT);

        std::array<VkImageView, 2> attachments = {colorViews_[i], depthViews_[i]};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = scenePass_;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = extent_.width;
        fbInfo.height = extent_.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_.device, &fbInfo, nullptr, &framebuffers_[i]));
    }
}

void PostProcess::destroyTargets() {
    for (auto fb : framebuffers_) vkDestroyFramebuffer(ctx_.device, fb, nullptr);
    framebuffers_.clear();
    for (auto v : colorViews_) vkDestroyImageView(ctx_.device, v, nullptr);
    colorViews_.clear();
    for (auto img : colorImages_) vkDestroyImage(ctx_.device, img, nullptr);
    colorImages_.clear();
    for (auto mem : colorMemory_) vkFreeMemory(ctx_.device, mem, nullptr);
    colorMemory_.clear();
    for (auto v : depthViews_) vkDestroyImageView(ctx_.device, v, nullptr);
    depthViews_.clear();
    for (auto img : depthImages_) vkDestroyImage(ctx_.device, img, nullptr);
    depthImages_.clear();
    for (auto mem : depthMemory_) vkFreeMemory(ctx_.device, mem, nullptr);
    depthMemory_.clear();
}

void PostProcess::createDescriptors() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(ctx_.device, &samplerInfo, nullptr, &sampler_));

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(ctx_.device, &layoutInfo, nullptr, &setLayout_));

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  MAX_FRAMES_IN_FLIGHT};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    VK_CHECK(vkCreateDescriptorPool(ctx_.device, &poolInfo, nullptr, &pool_));

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, setLayout_);
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc.pSetLayouts = layouts.data();
    sets_.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(ctx_.device, &alloc, sets_.data()));
}

void PostProcess::updateDescriptors() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorImageInfo imageInfo{sampler_, colorViews_[i],
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets_[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
    }
}

// Fullscreen pipeline: no vertex input (the vertex shader generates a single
// screen-covering triangle from gl_VertexIndex), no depth test, drawn inside
// the swapchain's present pass.
void PostProcess::createPipeline(VkRenderPass presentPass,
                                 const std::string& vertSpvPath,
                                 const std::string& fragSpvPath) {
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PostFXPush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(ctx_.device, &layoutInfo, nullptr, &pipelineLayout_));

    VkShaderModule vertModule = createShaderModule(ctx_.device, readFile(vertSpvPath));
    VkShaderModule fragModule = createShaderModule(ctx_.device, readFile(fragSpvPath));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamicState;
    info.layout = pipelineLayout_;
    info.renderPass = presentPass;
    info.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &info,
                                       nullptr, &pipeline_));

    vkDestroyShaderModule(ctx_.device, vertModule, nullptr);
    vkDestroyShaderModule(ctx_.device, fragModule, nullptr);
}

void PostProcess::draw(VkCommandBuffer cmd, uint32_t frame, const PostFXPush& push) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &sets_[frame], 0, nullptr);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PostFXPush), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace vke
