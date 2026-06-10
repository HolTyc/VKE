#pragma once

#include "core/Base.h"
#include "render/Vertex.h"
#include "vk/Pipeline.h"
#include "vk/Swapchain.h"
#include "vk/Texture.h"

#include <glm/glm.hpp>

class VulkanContext;
class Window;

// Simple 2D orthographic camera. World units are arbitrary; OrthoSize is the
// half-height of the visible area, width follows the window aspect ratio.
struct Camera2D {
    glm::vec2 Position{ 0.0f, 0.0f };
    float Zoom = 1.0f;       // > 1 zooms in
    float OrthoSize = 5.0f;  // half of the vertical extent in world units

    glm::mat4 GetViewProjection(float aspectRatio) const;
};

// Batched quad renderer + per-frame Vulkan orchestration (command buffers,
// sync, swapchain recreation). Casual users only touch BeginScene/DrawQuad/
// EndScene through the Scene; advanced users can grab the active command
// buffer and render pass to record their own draws between EndScene() and
// EndFrame(), or inject a custom Pipeline for all sprite batches.
class Renderer2D {
public:
    static constexpr uint32_t MaxQuads = 20000;
    static constexpr uint32_t FramesInFlight = 2;

    struct Stats {
        uint32_t DrawCalls = 0;
        uint32_t QuadCount = 0;
    };

    Renderer2D(VulkanContext& context, Window& window);
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // ---- Frame lifecycle (driven by Application) --------------------------
    bool BeginFrame();   // false => swapchain out of date, skip this frame
    void EndFrame();

    // ---- Scene rendering ---------------------------------------------------
    void BeginScene(const Camera2D& camera);
    void EndScene();

    void DrawQuad(const glm::vec2& position, const glm::vec2& size, float rotationRadians,
                  const glm::vec4& color, const std::shared_ptr<Texture>& texture = nullptr);

    void SetClearColor(const glm::vec4& color) { m_ClearColor = color; }
    const glm::vec4& GetClearColor() const     { return m_ClearColor; }

    const Stats& GetStats() const { return m_Stats; }

    // ---- Advanced access ----------------------------------------------------
    // Replace the sprite shader for every batch (nullptr restores the default).
    // The pipeline must follow the QuadVertex / push-constant contract.
    void SetCustomPipeline(std::shared_ptr<Pipeline> pipeline) { m_CustomPipeline = std::move(pipeline); }

    VkCommandBuffer GetActiveCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }
    VkRenderPass    GetRenderPass() const          { return m_Swapchain->GetRenderPass(); }
    Swapchain&      GetSwapchain()                 { return *m_Swapchain; }
    const std::shared_ptr<Texture>& GetWhiteTexture() const { return m_WhiteTexture; }

private:
    void CreateSyncObjects();
    void DestroySyncObjects();
    void CreateBatchResources();
    void RecreateSwapchain();
    void Flush();

    VulkanContext& m_Context;
    Window&        m_Window;
    std::unique_ptr<Swapchain> m_Swapchain;
    std::unique_ptr<Pipeline>  m_DefaultPipeline;
    std::shared_ptr<Pipeline>  m_CustomPipeline;
    Pipeline*                  m_BoundPipeline = nullptr;
    std::shared_ptr<Texture>   m_WhiteTexture;

    // Per frame-in-flight
    VkCommandBuffer m_CommandBuffers[FramesInFlight]{};
    VkSemaphore     m_ImageAvailable[FramesInFlight]{};
    VkFence         m_InFlightFences[FramesInFlight]{};
    // Per swapchain image (avoids reusing a semaphore the presenter still owns)
    std::vector<VkSemaphore> m_RenderFinished;

    // Batch data: one persistently-mapped vertex buffer per frame in flight,
    // one shared device-local index buffer with a fixed quad pattern.
    VkBuffer       m_VertexBuffers[FramesInFlight]{};
    VkDeviceMemory m_VertexMemory[FramesInFlight]{};
    QuadVertex*    m_VertexMapped[FramesInFlight]{};
    VkBuffer       m_IndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_IndexMemory = VK_NULL_HANDLE;

    uint32_t m_CurrentFrame = 0;
    uint32_t m_ImageIndex = 0;
    bool     m_FrameActive = false;
    bool     m_SceneActive = false;

    uint32_t m_QuadCount = 0;
    uint32_t m_BatchStartQuad = 0;
    std::shared_ptr<Texture> m_BatchTexture;
    bool m_WarnedFull = false;

    glm::vec4 m_ClearColor{ 0.08f, 0.08f, 0.10f, 1.0f };
    Stats m_Stats;
};
