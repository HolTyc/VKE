#include "vk/Texture.h"
#include "vk/VulkanContext.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

std::shared_ptr<Texture> Texture::Create(const std::string& path) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        ENGINE_ERROR("Failed to load texture '%s': %s", path.c_str(), stbi_failure_reason());
        return nullptr;
    }
    auto texture = std::shared_ptr<Texture>(new Texture());
    texture->m_Name = path;
    texture->Init((uint32_t)w, (uint32_t)h, pixels);
    stbi_image_free(pixels);
    return texture;
}

std::shared_ptr<Texture> Texture::Create(uint32_t width, uint32_t height,
                                         const void* rgba8Pixels, const std::string& debugName) {
    auto texture = std::shared_ptr<Texture>(new Texture());
    texture->m_Name = debugName;
    texture->Init(width, height, rgba8Pixels);
    return texture;
}

void Texture::Init(uint32_t width, uint32_t height, const void* pixels) {
    VulkanContext& ctx = *VulkanContext::Get();
    VkDevice device = ctx.GetDevice();
    m_Width = width;
    m_Height = height;

    VkDeviceSize size = (VkDeviceSize)width * height * 4;

    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    ctx.CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     staging, stagingMemory);
    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, size, 0, &mapped);
    std::memcpy(mapped, pixels, (size_t)size);
    vkUnmapMemory(device, stagingMemory);

    ctx.CreateImage2D(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      m_Image, m_Memory);

    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(cmd, staging, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    ctx.EndSingleTimeCommands(cmd);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &m_View));

    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.maxAnisotropy = 1.0f;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler));

    VkDescriptorSetLayout layout = ctx.GetTextureSetLayout();
    VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc.descriptorPool = ctx.GetDescriptorPool();
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;
    VK_CHECK(vkAllocateDescriptorSets(device, &alloc, &m_DescriptorSet));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_Sampler;
    imageInfo.imageView = m_View;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_DescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

Texture::~Texture() {
    VulkanContext* ctx = VulkanContext::Get();
    if (!ctx) {
        ENGINE_WARN("Texture '%s' destroyed after the Vulkan context — leaking GPU resources. "
                    "Release texture references before the Application is destroyed.", m_Name.c_str());
        return;
    }
    VkDevice device = ctx->GetDevice();
    vkFreeDescriptorSets(device, ctx->GetDescriptorPool(), 1, &m_DescriptorSet);
    vkDestroySampler(device, m_Sampler, nullptr);
    vkDestroyImageView(device, m_View, nullptr);
    vkDestroyImage(device, m_Image, nullptr);
    vkFreeMemory(device, m_Memory, nullptr);
}
