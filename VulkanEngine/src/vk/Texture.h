#pragma once

#include "core/Base.h"

// 2D RGBA8 texture. Creation handles staging upload, layout transitions and
// descriptor set allocation internally — user code only ever calls
// Texture::Create(...) and stores the shared_ptr in a SpriteComponent.
class Texture {
public:
    // Load from an image file (png/jpg/bmp/tga... — anything stb_image reads).
    static std::shared_ptr<Texture> Create(const std::string& path);
    // Create from raw RGBA8 pixel data (tightly packed, width*height*4 bytes).
    static std::shared_ptr<Texture> Create(uint32_t width, uint32_t height,
                                           const void* rgba8Pixels,
                                           const std::string& debugName = "procedural");

    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    uint32_t GetWidth() const  { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    const std::string& GetName() const { return m_Name; }
    VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }

private:
    Texture() = default;
    void Init(uint32_t width, uint32_t height, const void* pixels);

    uint32_t        m_Width = 0;
    uint32_t        m_Height = 0;
    std::string     m_Name;
    VkImage         m_Image = VK_NULL_HANDLE;
    VkDeviceMemory  m_Memory = VK_NULL_HANDLE;
    VkImageView     m_View = VK_NULL_HANDLE;
    VkSampler       m_Sampler = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
};
