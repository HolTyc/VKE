#include "render/Renderer2D.h"
#include "vk/VulkanContext.h"
#include "core/Window.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

glm::mat4 Camera2D::GetViewProjection(float aspectRatio) const {
    float halfH = OrthoSize / Zoom;
    float halfW = halfH * aspectRatio;
    glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
    proj[1][1] *= -1.0f; // Vulkan clip space is Y-down; flip so world Y is up
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-Position, 0.0f));
    return proj * view;
}

Renderer2D::Renderer2D(VulkanContext& context, Window& window)
    : m_Context(context), m_Window(window) {
    m_Swapchain = std::make_unique<Swapchain>(context, window);

    m_DefaultPipeline = std::make_unique<Pipeline>(
        "shaders/sprite.vert.spv", "shaders/sprite.frag.spv", m_Swapchain->GetRenderPass());

    uint32_t white = 0xFFFFFFFF;
    m_WhiteTexture = Texture::Create(1, 1, &white, "white");

    VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc.commandPool = m_Context.GetCommandPool();
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = FramesInFlight;
    VK_CHECK(vkAllocateCommandBuffers(m_Context.GetDevice(), &alloc, m_CommandBuffers));

    CreateSyncObjects();
    CreateBatchResources();
}

Renderer2D::~Renderer2D() {
    m_Context.WaitIdle();
    VkDevice device = m_Context.GetDevice();

    for (uint32_t i = 0; i < FramesInFlight; i++) {
        vkUnmapMemory(device, m_VertexMemory[i]);
        vkDestroyBuffer(device, m_VertexBuffers[i], nullptr);
        vkFreeMemory(device, m_VertexMemory[i], nullptr);
    }
    vkDestroyBuffer(device, m_IndexBuffer, nullptr);
    vkFreeMemory(device, m_IndexMemory, nullptr);

    DestroySyncObjects();
}

void Renderer2D::CreateSyncObjects() {
    VkDevice device = m_Context.GetDevice();
    VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < FramesInFlight; i++) {
        VK_CHECK(vkCreateSemaphore(device, &semInfo, nullptr, &m_ImageAvailable[i]));
        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]));
    }
    m_RenderFinished.resize(m_Swapchain->GetImageCount());
    for (auto& sem : m_RenderFinished)
        VK_CHECK(vkCreateSemaphore(device, &semInfo, nullptr, &sem));
}

void Renderer2D::DestroySyncObjects() {
    VkDevice device = m_Context.GetDevice();
    for (uint32_t i = 0; i < FramesInFlight; i++) {
        vkDestroySemaphore(device, m_ImageAvailable[i], nullptr);
        vkDestroyFence(device, m_InFlightFences[i], nullptr);
    }
    for (auto sem : m_RenderFinished)
        vkDestroySemaphore(device, sem, nullptr);
    m_RenderFinished.clear();
}

void Renderer2D::CreateBatchResources() {
    VkDevice device = m_Context.GetDevice();

    VkDeviceSize vertexSize = (VkDeviceSize)MaxQuads * 4 * sizeof(QuadVertex);
    for (uint32_t i = 0; i < FramesInFlight; i++) {
        m_Context.CreateBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               m_VertexBuffers[i], m_VertexMemory[i]);
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(device, m_VertexMemory[i], 0, vertexSize, 0, &mapped));
        m_VertexMapped[i] = (QuadVertex*)mapped;
    }

    // Fixed index pattern: every quad is (0,1,2)(2,3,0) offset by 4*quadIndex.
    std::vector<uint32_t> indices(MaxQuads * 6);
    for (uint32_t q = 0; q < MaxQuads; q++) {
        uint32_t base = q * 4;
        indices[q * 6 + 0] = base + 0;
        indices[q * 6 + 1] = base + 1;
        indices[q * 6 + 2] = base + 2;
        indices[q * 6 + 3] = base + 2;
        indices[q * 6 + 4] = base + 3;
        indices[q * 6 + 5] = base + 0;
    }
    VkDeviceSize indexSize = indices.size() * sizeof(uint32_t);

    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    m_Context.CreateBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           staging, stagingMemory);
    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, indexSize, 0, &mapped);
    std::memcpy(mapped, indices.data(), (size_t)indexSize);
    vkUnmapMemory(device, stagingMemory);

    m_Context.CreateBuffer(indexSize,
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_IndexBuffer, m_IndexMemory);
    m_Context.CopyBuffer(staging, m_IndexBuffer, indexSize);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

void Renderer2D::RecreateSwapchain() {
    // If minimized, block until the window has a size again.
    VkExtent2D extent = m_Window.GetFramebufferExtent();
    while (extent.width == 0 || extent.height == 0) {
        m_Window.WaitEvents(0.5);
        extent = m_Window.GetFramebufferExtent();
        if (m_Window.ShouldClose())
            return;
    }
    m_Swapchain->Recreate();
    m_Window.ResetResized();

    // Image count may have changed; rebuild the per-image semaphores.
    m_Context.WaitIdle();
    DestroySyncObjects();
    CreateSyncObjects();
}

bool Renderer2D::BeginFrame() {
    VkDevice device = m_Context.GetDevice();
    vkWaitForFences(device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = m_Swapchain->AcquireNextImage(m_ImageAvailable[m_CurrentFrame], &m_ImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        ENGINE_ERROR("vkAcquireNextImageKHR failed (%d)", (int)result);
        std::abort();
    }

    vkResetFences(device, 1, &m_InFlightFences[m_CurrentFrame]);

    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    VkClearValue clear{};
    clear.color = {{ m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a }};

    VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp.renderPass = m_Swapchain->GetRenderPass();
    rp.framebuffer = m_Swapchain->GetFramebuffer(m_ImageIndex);
    rp.renderArea.extent = m_Swapchain->GetExtent();
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkExtent2D extent = m_Swapchain->GetExtent();
    VkViewport viewport{ 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
    VkRect2D scissor{ { 0, 0 }, extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    m_FrameActive = true;
    return true;
}

void Renderer2D::EndFrame() {
    if (!m_FrameActive)
        return;
    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &m_ImageAvailable[m_CurrentFrame];
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_RenderFinished[m_ImageIndex];
    VK_CHECK(vkQueueSubmit(m_Context.GetGraphicsQueue(), 1, &submit, m_InFlightFences[m_CurrentFrame]));

    VkResult result = m_Swapchain->Present(m_RenderFinished[m_ImageIndex], m_ImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_Window.WasResized()) {
        RecreateSwapchain();
    } else if (result != VK_SUCCESS) {
        ENGINE_ERROR("vkQueuePresentKHR failed (%d)", (int)result);
        std::abort();
    }

    m_FrameActive = false;
    m_CurrentFrame = (m_CurrentFrame + 1) % FramesInFlight;
}

void Renderer2D::BeginScene(const Camera2D& camera) {
    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
    m_BoundPipeline = m_CustomPipeline ? m_CustomPipeline.get() : m_DefaultPipeline.get();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BoundPipeline->GetHandle());

    VkExtent2D extent = m_Swapchain->GetExtent();
    float aspect = extent.height > 0 ? (float)extent.width / (float)extent.height : 1.0f;
    glm::mat4 viewProj = camera.GetViewProjection(aspect);
    vkCmdPushConstants(cmd, m_BoundPipeline->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(glm::mat4), &viewProj);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_VertexBuffers[m_CurrentFrame], &offset);
    vkCmdBindIndexBuffer(cmd, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    m_QuadCount = 0;
    m_BatchStartQuad = 0;
    m_BatchTexture = m_WhiteTexture;
    m_WarnedFull = false;
    m_Stats = {};
    m_SceneActive = true;
}

void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, float rotationRadians,
                          const glm::vec4& color, const std::shared_ptr<Texture>& texture) {
    if (!m_SceneActive)
        return;

    const std::shared_ptr<Texture>& tex = texture ? texture : m_WhiteTexture;
    if (tex != m_BatchTexture) {
        Flush();
        m_BatchTexture = tex;
    }

    if (m_QuadCount >= MaxQuads) {
        if (!m_WarnedFull) {
            ENGINE_WARN("Renderer2D quad limit (%u) reached; extra quads dropped this frame", MaxQuads);
            m_WarnedFull = true;
        }
        return;
    }

    float c = std::cos(rotationRadians);
    float s = std::sin(rotationRadians);
    glm::vec2 half = size * 0.5f;

    static constexpr glm::vec2 kCorners[4] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
    static constexpr glm::vec2 kUVs[4]     = { {0,1},  {1,1},  {1,0}, {0,0} };

    QuadVertex* v = m_VertexMapped[m_CurrentFrame] + (size_t)m_QuadCount * 4;
    for (int i = 0; i < 4; i++) {
        glm::vec2 local = kCorners[i] * half;
        v[i].Position = { position.x + local.x * c - local.y * s,
                          position.y + local.x * s + local.y * c };
        v[i].UV = kUVs[i];
        v[i].Color = color;
    }
    m_QuadCount++;
}

void Renderer2D::Flush() {
    uint32_t count = m_QuadCount - m_BatchStartQuad;
    if (count == 0)
        return;

    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
    VkDescriptorSet set = m_BatchTexture->GetDescriptorSet();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BoundPipeline->GetLayout(),
                            0, 1, &set, 0, nullptr);
    vkCmdDrawIndexed(cmd, count * 6, 1, m_BatchStartQuad * 6, 0, 0);

    m_Stats.DrawCalls++;
    m_BatchStartQuad = m_QuadCount;
}

void Renderer2D::EndScene() {
    if (!m_SceneActive)
        return;
    Flush();
    m_Stats.QuadCount = m_QuadCount;
    m_SceneActive = false;
}
