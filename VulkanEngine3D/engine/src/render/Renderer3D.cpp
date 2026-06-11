#include "vke/Renderer3D.hpp"

#include "vke/Window.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>

namespace vke {

std::string Renderer3D::resolvePath(const std::string& path) const {
    if (path.empty() || path.front() == '/') return path;
    if (!assetRoot_.empty()) {
        std::filesystem::path inRoot = std::filesystem::path(assetRoot_) / path;
        if (std::filesystem::exists(inRoot)) return inRoot.string();
    }
    return assetPath(path);
}

Renderer3D::Renderer3D(Window& window) : window_(window) {
    ctx_ = std::make_unique<VulkanContext>(window_);
    swapchain_ = std::make_unique<Swapchain>(*ctx_, window_.framebufferExtent());

    createDescriptorResources();

    // Shared pipeline layout: one global UBO set + push constants per draw.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushData);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &globalSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(ctx_->device, &layoutInfo, nullptr, &pipelineLayout_));

    // Command buffers, one per frame in flight.
    commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = ctx_->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    VK_CHECK(vkAllocateCommandBuffers(ctx_->device, &allocInfo, commandBuffers_.data()));

    registerShader("basic", "shaders/basic.vert.spv", "shaders/basic.frag.spv");
}

Renderer3D::~Renderer3D() {
    waitIdle();
    post_.reset();
    primitives_.clear();
    models_.clear();
    pipelines_.clear();
    vkDestroyPipelineLayout(ctx_->device, pipelineLayout_, nullptr);
    for (size_t i = 0; i < uboBuffers_.size(); ++i) {
        vkDestroyBuffer(ctx_->device, uboBuffers_[i], nullptr);
        vkFreeMemory(ctx_->device, uboMemory_[i], nullptr);
    }
    vkDestroyDescriptorPool(ctx_->device, descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_->device, globalSetLayout_, nullptr);
}

void Renderer3D::waitIdle() const {
    ctx_->waitIdle();
}

void Renderer3D::createDescriptorResources() {
    // Set layout: binding 0 = global UBO, visible to both shader stages.
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    VK_CHECK(vkCreateDescriptorSetLayout(ctx_->device, &layoutInfo, nullptr, &globalSetLayout_));

    // Per-frame UBO buffers, persistently mapped.
    uboBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    uboMemory_.resize(MAX_FRAMES_IN_FLIGHT);
    uboMapped_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        ctx_->createBuffer(sizeof(GlobalUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           uboBuffers_[i], uboMemory_[i]);
        VK_CHECK(vkMapMemory(ctx_->device, uboMemory_[i], 0, sizeof(GlobalUBO), 0, &uboMapped_[i]));
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    VK_CHECK(vkCreateDescriptorPool(ctx_->device, &poolInfo, nullptr, &descriptorPool_));

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, globalSetLayout_);
    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = descriptorPool_;
    setAlloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    setAlloc.pSetLayouts = layouts.data();
    globalSets_.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(ctx_->device, &setAlloc, globalSets_.data()));

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo bufferInfo{uboBuffers_[i], 0, sizeof(GlobalUBO)};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = globalSets_[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(ctx_->device, 1, &write, 0, nullptr);
    }
}

// -------------------------------------------------------------------- frame

VkCommandBuffer Renderer3D::beginFrame() {
    VkResult result = swapchain_->acquireNextImage(frameIndex_, &imageIndex_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return VK_NULL_HANDLE;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    VkCommandBuffer cmd = commandBuffers_[frameIndex_];
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    VkClearValue clears[2];
    clears[0].color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    clears[1].depthStencil = {1.0f, 0};

    // With post-processing on, the scene pass targets the offscreen texture;
    // applyPostProcess() later composites it into the swapchain pass.
    postPassActive_ = postEnabled_ && post_ != nullptr;

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = postPassActive_ ? post_->scenePass() : swapchain_->renderPass();
    rpInfo.framebuffer = postPassActive_ ? post_->framebuffer(frameIndex_)
                                         : swapchain_->framebuffer(imageIndex_);
    rpInfo.renderArea = {{0, 0}, swapchain_->extent()};
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchain_->extent().width);
    viewport.height = static_cast<float>(swapchain_->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, swapchain_->extent()};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    frameStarted_ = true;
    return cmd;
}

void Renderer3D::renderScene(Scene& scene) {
    if (!frameStarted_) return;
    VkCommandBuffer cmd = commandBuffers_[frameIndex_];

    // ---- build the global UBO -------------------------------------------
    GlobalUBO ubo{};
    float fov = 60.0f, nearClip = 0.1f, farClip = 500.0f;
    glm::vec3 camPos{4.0f, 4.0f, 8.0f};

    Entity* overrideCam = cameraOverride_ ? scene.find(cameraOverride_) : nullptr;
    if (overrideCam && !overrideCam->has<CameraComponent>()) overrideCam = nullptr;
    if (Entity* camEntity = overrideCam ? overrideCam : scene.primaryCamera()) {
        const auto& t = camEntity->transform();
        const auto* cam = camEntity->get<CameraComponent>();
        ubo.view = glm::inverse(t.matrix());
        fov = cam->fov;
        nearClip = cam->nearClip;
        farClip = cam->farClip;
        camPos = t.position;
    } else {
        ubo.view = glm::lookAt(camPos, glm::vec3{0.0f}, glm::vec3{0, 1, 0});
    }

    ubo.proj = glm::perspective(glm::radians(fov), swapchain_->aspectRatio(), nearClip, farClip);
    ubo.proj[1][1] *= -1.0f; // GLM is GL-style; Vulkan clip space flips Y
    ubo.camPos = glm::vec4(camPos, 1.0f);
    ubo.ambient = glm::vec4(ambientLight, 1.0f);

    int lightCount = 0;
    scene.forEach<LightComponent>([&](Entity& e, LightComponent& light) {
        if (lightCount >= MAX_LIGHTS) return;
        GpuLight& g = ubo.lights[lightCount++];
        g.position = glm::vec4(e.transform().position, static_cast<float>(light.type));
        g.color = glm::vec4(light.color, light.intensity);
        g.direction = glm::vec4(e.transform().forward(), light.range);
        g.cone = glm::vec4(std::cos(glm::radians(light.innerAngle)),
                           std::cos(glm::radians(light.outerAngle)), 0.0f, 0.0f);
    });
    ubo.lightCount = lightCount;

    std::memcpy(uboMapped_[frameIndex_], &ubo, sizeof(ubo));

    // ---- draw all mesh renderers, grouped by shader ----------------------
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &globalSets_[frameIndex_], 0, nullptr);

    const std::string* boundShader = nullptr;
    scene.forEach<MeshRendererComponent>([&](Entity& e, MeshRendererComponent& mr) {
        if (!mr.mesh) return;

        auto it = pipelines_.find(mr.material.shader);
        if (it == pipelines_.end()) it = pipelines_.find("basic");
        if (!boundShader || *boundShader != it->first) {
            it->second->bind(cmd);
            boundShader = &it->first;
        }

        PushData push{};
        push.model = e.transform().matrix();
        push.albedo = mr.material.albedo;
        push.params = glm::vec4(mr.material.shininess, mr.material.specular, 0.0f, 0.0f);
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushData), &push);

        mr.mesh->bind(cmd);
        mr.mesh->draw(cmd);
    });
}

void Renderer3D::applyPostProcess() {
    if (!frameStarted_ || !postPassActive_) return;
    postPassActive_ = false;

    VkCommandBuffer cmd = commandBuffers_[frameIndex_];
    vkCmdEndRenderPass(cmd); // offscreen scene pass; color is now sampleable

    VkClearValue clears[2];
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // fully covered by the quad
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = swapchain_->renderPass();
    rpInfo.framebuffer = swapchain_->framebuffer(imageIndex_);
    rpInfo.renderArea = {{0, 0}, swapchain_->extent()};
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    PostFXPush push{};
    push.timeRes = {postProcessTime, postProcessStrength,
                    static_cast<float>(swapchain_->extent().width),
                    static_cast<float>(swapchain_->extent().height)};
    push.params = postProcessParams;
    post_->draw(cmd, frameIndex_, push);
}

void Renderer3D::endFrame() {
    if (!frameStarted_) return;
    applyPostProcess(); // safety net for apps driving the renderer manually
    VkCommandBuffer cmd = commandBuffers_[frameIndex_];

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkResult result = swapchain_->submitAndPresent(cmd, frameIndex_, imageIndex_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        window_.wasResized()) {
        window_.clearResized();
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }

    frameStarted_ = false;
    frameIndex_ = (frameIndex_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer3D::recreateSwapchain() {
    VkExtent2D extent = window_.framebufferExtent();
    while (extent.width == 0 || extent.height == 0) {
        Window::waitEvents(); // minimized: sleep until restored
        extent = window_.framebufferExtent();
    }
    ctx_->waitIdle();
    swapchain_->recreate(extent);
    if (post_) post_->resize(swapchain_->extent());
}

// ----------------------------------------------------------------- resources

std::shared_ptr<Mesh> Renderer3D::primitive(Primitive p) {
    auto it = primitives_.find(static_cast<int>(p));
    if (it != primitives_.end()) return it->second;

    std::shared_ptr<Mesh> mesh;
    switch (p) {
        case Primitive::Cube:   mesh = Mesh::createCube(*ctx_);   mesh->source = "primitive:cube";   break;
        case Primitive::Sphere: mesh = Mesh::createSphere(*ctx_); mesh->source = "primitive:sphere"; break;
        case Primitive::Plane:  mesh = Mesh::createPlane(*ctx_);  mesh->source = "primitive:plane";  break;
    }
    primitives_[static_cast<int>(p)] = mesh;
    return mesh;
}

std::shared_ptr<Mesh> Renderer3D::loadModel(const std::string& modelPath) {
    if (auto it = models_.find(modelPath); it != models_.end()) return it->second;

    const std::string resolved = resolvePath(modelPath);
    std::shared_ptr<Mesh> mesh;
    if (resolved.size() >= 4 && resolved.compare(resolved.size() - 4, 4, ".glb") == 0)
        mesh = Mesh::loadGLB(*ctx_, resolved);
    else
        mesh = Mesh::loadOBJ(*ctx_, resolved);
    mesh->source = "model:" + modelPath;
    models_[modelPath] = mesh;
    return mesh;
}

void Renderer3D::registerShader(const std::string& name,
                                const std::string& vertSpvPath,
                                const std::string& fragSpvPath) {
    pipelines_[name] = std::make_unique<Pipeline>(*ctx_, swapchain_->renderPass(),
                                                  pipelineLayout_,
                                                  resolvePath(vertSpvPath),
                                                  resolvePath(fragSpvPath));
}

void Renderer3D::setPostProcessShader(const std::string& vertSpvPath,
                                      const std::string& fragSpvPath) {
    waitIdle(); // a previous post pipeline/target may still be in flight
    post_ = std::make_unique<PostProcess>(*ctx_, swapchain_->renderPass(),
                                          swapchain_->imageFormat(),
                                          swapchain_->extent(),
                                          resolvePath(vertSpvPath),
                                          resolvePath(fragSpvPath));
    postEnabled_ = true;
}

std::vector<std::string> Renderer3D::shaderNames() const {
    std::vector<std::string> names;
    names.reserve(pipelines_.size());
    for (const auto& [name, _] : pipelines_)
        names.push_back(name);
    return names;
}

} // namespace vke
