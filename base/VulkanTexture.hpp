/*
* Vulkan texture loader
*
* Copyright(C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "macros.h"
#include "VulkanDevice.hpp"
#include "texture.hpp"

#include <gli/gli.hpp>

#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

namespace vks
{
    /*class Texture {
    public:
        vks::VulkanDevice *device;
        VkImage image;
        VkImageLayout imageLayout;
        VkDeviceMemory deviceMemory;
        VkImageView view;
        uint32_t width, height;
        uint32_t mipLevels;
        uint32_t layerCount;
        VkDescriptorImageInfo descriptor;
        VkSampler sampler;

        void updateDescriptor()
        {
            descriptor.sampler = sampler;
            descriptor.imageView = view;
            descriptor.imageLayout = imageLayout;
        }

        void destroy()
        {
            vkDestroyImageView(device->logicalDevice, view, nullptr);
            vkDestroyImage(device->logicalDevice, image, nullptr);
            if (sampler)
            {
                vkDestroySampler(device->logicalDevice, sampler, nullptr);
            }
            vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
        }
    };*/

    class Texture2D : public Texture {
    public:
        void loadFromFile(
            std::string filename,
            VkFormat format,
            vks::VulkanDevice *_device,
            VkQueue copyQueue,
            VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
#if defined(__ANDROID__)
            // Textures are stored inside the apk on Android (compressed)
            // So they need to be loaded via the asset manager
            AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
            if (!asset) {
                LOGE("Could not load texture %s", filename.c_str());
                exit(-1);
            }
            size_t size = AAsset_getLength(asset);
            assert(size > 0);

            void *textureData = malloc(size);
            AAsset_read(asset, textureData, size);
            AAsset_close(asset);

            gli::texture2d tex2D(gli::load((const char*)textureData, size));

            free(textureData);
#else
            gli::texture2d tex2D(gli::load(filename.c_str()));
#endif
            assert(!tex2D.empty());

            this->device = _device;
            create(VK_IMAGE_TYPE_2D, format,
                   static_cast<uint32_t>(tex2D[0].extent().x),static_cast<uint32_t>(tex2D[0].extent().y),
                   imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VK_IMAGE_TILING_OPTIMAL,static_cast<uint32_t>(tex2D.levels()));

            copyTo(copyQueue, (unsigned char*)tex2D.data(), tex2D.size());

            createView(VK_IMAGE_VIEW_TYPE_2D);
            createSampler(VK_FILTER_LINEAR,VK_SAMPLER_ADDRESS_MODE_REPEAT,VK_SAMPLER_MIPMAP_MODE_LINEAR);

            updateDescriptor();
        }
    };

    class TextureCubeMap : public Texture {
    public:
        void loadFromFile(
            std::string filename,
            VkFormat format,
            vks::VulkanDevice *_device,
            VkQueue copyQueue,
            VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
#if defined(__ANDROID__)
            // Textures are stored inside the apk on Android (compressed)
            // So they need to be loaded via the asset manager
            AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
            if (!asset) {
                LOGE("Could not load texture %s", filename.c_str());
                exit(-1);
            }
            size_t size = AAsset_getLength(asset);
            assert(size > 0);

            void *textureData = malloc(size);
            AAsset_read(asset, textureData, size);
            AAsset_close(asset);

            gli::texture_cube texCube(gli::load((const char*)textureData, size));

            free(textureData);
#else
            gli::texture_cube texCube(gli::load(filename));
#endif
            assert(!texCube.empty());

            this->device = _device;
            create(VK_IMAGE_TYPE_2D, format,
                   static_cast<uint32_t>(texCube.extent().x),static_cast<uint32_t>(texCube[0].extent().y),
                   imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VK_IMAGE_TILING_OPTIMAL,static_cast<uint32_t>(texCube.levels()), 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

            vks::Buffer stagingBuffer;
            device->createBuffer (VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &stagingBuffer, texCube.size(), texCube.data());

            descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            size_t offset = 0;

            std::vector<VkBufferImageCopy> bufferCopyRegions;
            for (uint32_t face = 0; face < 6; face++) {
                for (uint32_t level = 0; level < infos.mipLevels; level++) {
                    VkBufferImageCopy bufferCopyRegion = {};
                    bufferCopyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, face, 1};
                    bufferCopyRegion.imageExtent = {texCube[face][level].extent().x,texCube[face][level].extent().y,1};
                    bufferCopyRegion.bufferOffset = offset;

                    bufferCopyRegions.push_back(bufferCopyRegion);
                    offset += texCube[face][level].size();
                }
            }
            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = infos.mipLevels;
            subresourceRange.layerCount = 6;

            setImageLayout (copyCmd,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           subresourceRange,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

            vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

            setImageLayout(copyCmd,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           subresourceRange,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            device->flushCommandBuffer(copyCmd, copyQueue, true);
            stagingBuffer.destroy();

            createView(VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_ASPECT_COLOR_BIT,infos.mipLevels,6);
            createSampler(VK_FILTER_LINEAR,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_SAMPLER_MIPMAP_MODE_LINEAR);
            //samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;

            updateDescriptor();
        }
    };

}
