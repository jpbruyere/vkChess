#pragma once

#include "vke.h"
#include "resource.hpp"

namespace vks
{
    struct Texture : public Resource {
        VkImage             image       = VK_NULL_HANDLE;
        VkImageView         view        = VK_NULL_HANDLE;
        VkSampler           sampler     = VK_NULL_HANDLE;

        VkImageCreateInfo   infos       = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkImageLayout       imageLayout;
        VkDescriptorImageInfo descriptor;

        void create (ptrVkDev _device,
                        VkImageType imageType, VkFormat format, uint32_t width, uint32_t height,
                        VkImageUsageFlags usage, VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
                        uint32_t  mipLevels = 1, uint32_t arrayLayers = 1,
                        VkImageCreateFlags flags = 0,
                        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE);
        Texture ();
        Texture (ptrVkDev  _device);
        Texture (ptrVkDev  _device,
                        VkImageType imageType, VkFormat format, uint32_t width, uint32_t height,
                        VkImageUsageFlags usage, VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
                        uint32_t  mipLevels = 1, uint32_t arrayLayers = 1,
                        VkImageCreateFlags flags = 0,
                        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE);
        Texture (ptrVkDev  _device,  VkQueue copyQueue, VkImageType imageType, VkFormat format,
                 std::vector<Texture> texArray, uint32_t width, uint32_t height,
                 VkImageUsageFlags _usage = VK_IMAGE_USAGE_SAMPLED_BIT);

        /** @brief Update image descriptor from current sampler, view and image layout */
        void updateDescriptor();

        virtual VkWriteDescriptorSet getWriteDescriptorSet (VkDescriptorSet ds, uint32_t binding, VkDescriptorType descriptorType);

        void destroy();
        void setImageLayout(
            VkCommandBuffer cmdbuffer,
            VkImageLayout oldImageLayout,
            VkImageLayout newImageLayout,
            VkImageSubresourceRange subresourceRange,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask);

        // Fixed sub resource on first mip level and layer
        void setImageLayout(
            VkCommandBuffer cmdbuffer,
            VkImageAspectFlags aspectMask,
            VkImageLayout oldImageLayout,
            VkImageLayout newImageLayout,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask);

        void copyTo (VkQueue copyQueue, unsigned char* buffer, VkDeviceSize bufferSize);
        void createView (VkImageViewType viewType, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         uint32_t levelCount = 1, uint32_t layerCount = 1, VkComponentMapping components =
                         {VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A });

        void createSampler (VkFilter filter = VK_FILTER_NEAREST, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                            VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                            VkCompareOp compareOp = VK_COMPARE_OP_NEVER);
        void buildMipmaps (VkQueue copyQueue, VkCommandBuffer _blitCmd = VK_NULL_HANDLE);
        void loadStbLinearNoSampling (std::string filename,
            ptrVkDev device,
            VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            bool flipY = true);
    };
}
