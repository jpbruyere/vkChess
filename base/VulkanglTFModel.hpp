/*
* Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "VulkanDevice.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gli/gli.hpp>

#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

//when mipLevels is set to 0 => mipmaps with be generated
#define AUTOGEN_MIPMAPS 0

namespace vkglTF
{
    /*
        glTF texture loading class
    */
    struct Texture {
        vks::VulkanDevice*  device;
        VkImage             image       = VK_NULL_HANDLE;
        VkImageView         view        = VK_NULL_HANDLE;
        VkSampler           sampler     = VK_NULL_HANDLE;
        VkDeviceMemory      deviceMemory= VK_NULL_HANDLE;

        VkImageCreateInfo   infos       = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkImageLayout       imageLayout;
        VkDescriptorImageInfo descriptor;

        void create (VkImageType imageType, VkFormat format, uint32_t width, uint32_t height,
                        VkImageUsageFlags usage, VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
                        uint32_t  mipLevels = 1,
                        uint32_t arrayLayers = 1, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE) {
            infos.imageType = imageType;
            infos.format = format;
            infos.extent = {width, height, 1};
            infos.mipLevels = mipLevels == 0 ?
                    static_cast<uint32_t>(floor(log2(std::max(infos.extent.width, infos.extent.height))) + 1.0) : mipLevels;
            infos.arrayLayers = arrayLayers;
            infos.samples = samples;
            infos.tiling = tiling;
            infos.usage = usage;
            infos.sharingMode = sharingMode;
            infos.initialLayout = initialLayout;

            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            VkMemoryRequirements memReqs{};
            VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &infos, nullptr, &image));
            vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
            memAllocInfo.allocationSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, memProps);
            VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
            VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));
        }
        Texture (){}
        Texture (vks::VulkanDevice*  _device,
                 VkImageType imageType, VkFormat format, uint32_t width, uint32_t height,
                 VkImageUsageFlags usage, VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 uint32_t  mipLevels = 1, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
                 uint32_t arrayLayers = 1, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                 VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE)
        {
            device = _device;
            create(imageType, format, width, height, usage, memProps, tiling, mipLevels, arrayLayers,
                    samples, initialLayout, sharingMode);
        }
        Texture (vks::VulkanDevice*  _device,  VkQueue copyQueue, VkImageType imageType, VkFormat format,
                 std::vector<Texture> texArray, uint32_t width, uint32_t height,
                 VkImageUsageFlags _usage = VK_IMAGE_USAGE_SAMPLED_BIT) {

            device = _device;
            uint32_t mipLevels = (uint32_t)floor(log2(std::max(width, height))) + 1;

            create(imageType, format, width, height,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | _usage,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, mipLevels, texArray.size());

            VkCommandBuffer blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            //imageLayout = _imageLayout;
            for (int l = 0; l < texArray.size(); l++) {
                Texture* inTex = &texArray[l];

                VkImageBlit firstMipBlit{};

                firstMipBlit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                firstMipBlit.srcOffsets[1] = {inTex->infos.extent.width,inTex->infos.extent.height,1};

                firstMipBlit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, l, 1};
                firstMipBlit.dstOffsets[1] = {infos.extent.width,infos.extent.height,1};

                VkImageSubresourceRange mipSubRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, l, 1};

                inTex->setImageLayout(blitCmd, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                // Transiton current array level to transfer dest
                setImageLayout(blitCmd, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        mipSubRange,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

                vkCmdBlitImage(blitCmd, inTex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &firstMipBlit, VK_FILTER_LINEAR);

                setImageLayout(blitCmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mipSubRange,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

                /*setImageLayout(blitCmd, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        mipSubRange,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);*/
            }
            //device->flushCommandBuffer(blitCmd, copyQueue, true);
            buildMipmaps(copyQueue, blitCmd);

            device->flushCommandBuffer(blitCmd, copyQueue, true);

            createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, infos.mipLevels, infos.arrayLayers);
            createSampler(VK_FILTER_LINEAR,VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,VK_SAMPLER_MIPMAP_MODE_LINEAR,
                          (float)infos.mipLevels,VK_TRUE,8.0f,VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);

            updateDescriptor();
        }

        /** @brief Update image descriptor from current sampler, view and image layout */
        void updateDescriptor()
        {
            descriptor.sampler = sampler;
            descriptor.imageView = view;
        }

        /** @brief Release all Vulkan resources held by this texture */
        void destroy()
        {
            if (view)
                vkDestroyImageView(device->logicalDevice, view, VK_NULL_HANDLE);
            if (sampler)
                vkDestroySampler(device->logicalDevice, sampler, VK_NULL_HANDLE);
            if (image){
                vkDestroyImage(device->logicalDevice, image, VK_NULL_HANDLE);
                vkFreeMemory(device->logicalDevice, deviceMemory, VK_NULL_HANDLE);
            }
        }
        void setImageLayout(
            VkCommandBuffer cmdbuffer,
            VkImageLayout oldImageLayout,
            VkImageLayout newImageLayout,
            VkImageSubresourceRange subresourceRange,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask)
        {
            // Create an image barrier object
            VkImageMemoryBarrier imageMemoryBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            imageMemoryBarrier.oldLayout = oldImageLayout;
            imageMemoryBarrier.newLayout = newImageLayout;
            imageMemoryBarrier.image = image;
            imageMemoryBarrier.subresourceRange = subresourceRange;

            // Source layouts (old)
            // Source access mask controls actions that have to be finished on the old layout
            // before it will be transitioned to the new layout
            switch (oldImageLayout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                // Image layout is undefined (or does not matter)
                // Only valid as initial layout
                // No flags required, listed only for completeness
                imageMemoryBarrier.srcAccessMask = 0;
                break;

            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                // Image is preinitialized
                // Only valid as initial layout for linear images, preserves memory contents
                // Make sure host writes have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                // Image is a color attachment
                // Make sure any writes to the color buffer have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                // Image is a depth/stencil attachment
                // Make sure any writes to the depth/stencil buffer have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                // Image is a transfer source
                // Make sure any reads from the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                // Image is a transfer destination
                // Make sure any writes to the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                // Image is read by a shader
                // Make sure any shader reads from the image have been finished
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            default:
                // Other source layouts aren't handled (yet)
                break;
            }

            // Target layouts (new)
            // Destination access mask controls the dependency for the new image layout
            switch (newImageLayout)
            {
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                // Image will be used as a transfer destination
                // Make sure any writes to the image have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                // Image will be used as a transfer source
                // Make sure any reads from the image have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                // Image will be used as a color attachment
                // Make sure any writes to the color buffer have been finished
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                // Image layout will be used as a depth/stencil attachment
                // Make sure any writes to depth/stencil buffer have been finished
                imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                // Image will be read in a shader (sampler, input attachment)
                // Make sure any writes to the image have been finished
                if (imageMemoryBarrier.srcAccessMask == 0)
                {
                    imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                }
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            default:
                // Other source layouts aren't handled (yet)
                break;
            }

            // Put barrier inside setup command buffer
            vkCmdPipelineBarrier(
                cmdbuffer,
                srcStageMask,
                dstStageMask,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageMemoryBarrier);
        }

        // Fixed sub resource on first mip level and layer
        void setImageLayout(
            VkCommandBuffer cmdbuffer,
            VkImageAspectFlags aspectMask,
            VkImageLayout oldImageLayout,
            VkImageLayout newImageLayout,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask)
        {
            setImageLayout(cmdbuffer, oldImageLayout, newImageLayout, {aspectMask,0,1,0,1}, srcStageMask, dstStageMask);
        }

        void copyTo (VkQueue copyQueue, unsigned char* buffer, VkDeviceSize bufferSize) {
            vks::Buffer stagingBuffer;
            device->createBuffer (VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &stagingBuffer, bufferSize, buffer);

            descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            setImageLayout (copyCmd, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            bufferCopyRegion.imageExtent = infos.extent;

            vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

            setImageLayout(copyCmd, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

            device->flushCommandBuffer(copyCmd, copyQueue, true);

            stagingBuffer.destroy();
        }
        void createView (VkImageViewType viewType, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         uint32_t levelCount = 1, uint32_t layerCount = 1) {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image      = image;
            viewInfo.viewType   = viewType;
            viewInfo.format     = infos.format;
            viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
            viewInfo.subresourceRange.aspectMask = aspectMask;
            viewInfo.subresourceRange.levelCount = levelCount;
            viewInfo.subresourceRange.layerCount = layerCount;
            VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));
        }

        void createSampler (VkFilter filter = VK_FILTER_NEAREST, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                            float maxLod = 0.f,
                            VkBool32 anisotropyEnable = VK_FALSE,
                            float maxAnisotropy = 0.f,
                            VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                            VkCompareOp compareOp = VK_COMPARE_OP_NEVER) {

            VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            samplerInfo.magFilter = filter;
            samplerInfo.minFilter = filter;
            samplerInfo.mipmapMode = mipmapMode;
            samplerInfo.addressModeU = addressMode;
            samplerInfo.addressModeV = addressMode;
            samplerInfo.addressModeW = addressMode;
            samplerInfo.compareOp = compareOp;
            samplerInfo.borderColor = borderColor;
            samplerInfo.maxAnisotropy = maxAnisotropy;
            samplerInfo.anisotropyEnable = anisotropyEnable;
            samplerInfo.maxLod = maxLod;
            VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));
        }
        void buildMipmaps (VkQueue copyQueue, VkCommandBuffer _blitCmd = VK_NULL_HANDLE) {
            // Generate the mip chain (glTF uses jpg and png, so we need to create this manually)

            VkCommandBuffer blitCmd = VK_NULL_HANDLE;
            if (_blitCmd)
                blitCmd = _blitCmd;
            else
                blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            for (uint32_t i = 1; i < infos.mipLevels; i++) {
                VkImageBlit imageBlit{};

                imageBlit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, infos.arrayLayers};
                imageBlit.srcOffsets[1] = {int32_t(infos.extent.width >> (i - 1)),int32_t(infos.extent.height >> (i - 1)),1};

                imageBlit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, infos.arrayLayers};
                imageBlit.dstOffsets[1] = {int32_t(infos.extent.width >> i),int32_t(infos.extent.height >> i),1};

                VkImageSubresourceRange mipSubRange = {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, infos.arrayLayers};

                setImageLayout(blitCmd, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipSubRange,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

                vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

                setImageLayout(blitCmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mipSubRange,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            }

            setImageLayout(blitCmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, infos.mipLevels, 0, infos.arrayLayers},
                                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);


            if (_blitCmd)//if continuing an already existing cmd buff, cancel flush here
                return;
            device->flushCommandBuffer(blitCmd, copyQueue, true);
        }
        /*
            Load a texture from a glTF image (stored as vector of chars loaded via stb_image)
            Also generates the mip chain as glTF images are stored as jpg or png without any mips
        */
        //if load only is true, image is created with one mip without descriptor
        void fromglTfImage(tinygltf::Image &gltfimage, vks::VulkanDevice *device, VkQueue copyQueue, bool loadOnly = false)
        {
            this->device = device;

            unsigned char* buffer = nullptr;
            VkDeviceSize bufferSize = 0;
            bool deleteBuffer = false;
            if (gltfimage.component == 3) {
                // Most devices don't support RGB only on Vulkan so convert if necessary
                // TODO: Check actual format support and transform only if required
                bufferSize = gltfimage.width * gltfimage.height * 4;
                buffer = new unsigned char[bufferSize];
                unsigned char* rgba = buffer;
                unsigned char* rgb = gltfimage.image.data();
                for (size_t i = 0; i< gltfimage.width * gltfimage.height; ++i) {
                    for (int32_t j = 0; j < 3; ++j) {
                        rgba[j] = rgb[j];
                    }
                    rgba += 4;
                    rgb += 3;
                }
                deleteBuffer = true;
            }
            else {
                buffer = gltfimage.image.data();
                bufferSize = gltfimage.image.size();
            }

            //Texture test(device, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, gltfimage.width, gltfimage.height,
            //             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);


            VkFormatProperties formatProperties;
            vkGetPhysicalDeviceFormatProperties(device->physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
            assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
            assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

            uint32_t mipLevels = loadOnly ? 1 :
                        static_cast<uint32_t>(floor(log2(std::max(infos.extent.width, infos.extent.height))) + 1.0);

            create(VK_IMAGE_TYPE_2D,VK_FORMAT_R8G8B8A8_UNORM, gltfimage.width, gltfimage.height,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   VK_IMAGE_TILING_OPTIMAL, mipLevels);

            copyTo(copyQueue, buffer, bufferSize);

            if (loadOnly)
                return;

            buildMipmaps (copyQueue);

            createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, infos.mipLevels);
            createSampler(VK_FILTER_LINEAR,VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,VK_SAMPLER_MIPMAP_MODE_LINEAR,
                          (float)infos.mipLevels,VK_TRUE,8.0f,VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);

            updateDescriptor();
        }
    };

    /*
        glTF material class
    */
    enum AlphaMode : uint32_t { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };

    struct Material {
        AlphaMode alphaMode = ALPHAMODE_OPAQUE;
        float alphaCutoff = 1.0f;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;

        uint32_t baseColorTexture = 0;
        uint32_t metallicRoughnessTexture = 0;
        uint32_t normalTexture = 0;
        uint32_t occlusionTexture = 0;

        uint32_t emissiveTexture = 0;
        uint32_t pad1;
        uint32_t pad2;
        uint32_t pad3;
    };

    /*
        glTF primitive class
    */
    struct Primitive {
        std::string name;
        uint32_t    indexBase;
        uint32_t    indexCount;
        uint32_t    vertexBase;
        uint32_t    vertexCount;
        uint32_t    material;
    };

    struct InstanceData {
        uint32_t materialIndex = 0;
        glm::mat4 modelMat = glm::mat4();
    };

    /*
        glTF model loading and rendering class
    */
    struct Model {
        uint32_t textureSize = 1024; //texture array size w/h

        struct Vertex {
            glm::vec3 pos;
            glm::vec3 normal;
            glm::vec2 uv;
        };

        vks::VulkanDevice*  device;

        vks::Buffer vertices;
        vks::Buffer indices;
        vks::Buffer instancesBuff;
        vks::Buffer materialsBuff;
        Texture*    texArray = NULL;

        std::vector<Primitive> primitives;

        std::vector<Texture>            textures;
        std::vector<Material>           materials;
        std::vector<uint32_t>           instances;
        std::vector<InstanceData>       instanceDatas;
        //std::vector<VkDescriptorSet>    descriptorSets;

        void destroy()
        {
            vertices.destroy();
            indices.destroy();
            instancesBuff.destroy();
            materialsBuff.destroy();
            if (texArray) {
                texArray->destroy();
                delete (texArray);
            }

            for (Texture texture : textures)
                texture.destroy();
        }

        void loadNode(const tinygltf::Node &node, const glm::mat4 &parentMatrix, const tinygltf::Model &model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale)
        {
            // Generate local node matrix
            glm::vec3 translation = glm::vec3(0.0f);
            if (node.translation.size() == 3) {
                translation = glm::make_vec3(node.translation.data());
            }
            glm::mat4 rotation = glm::mat4(1.0f);
            if (node.rotation.size() == 4) {
                glm::quat q = glm::make_quat(node.rotation.data());
                rotation = glm::mat4(q);
            }
            glm::vec3 scale = glm::vec3(1.0f);
            if (node.scale.size() == 3) {
                scale = glm::make_vec3(node.scale.data());
            }
            glm::mat4 localNodeMatrix = glm::mat4(1.0f);
            if (node.matrix.size() == 16) {
                localNodeMatrix = glm::make_mat4x4(node.matrix.data());
            } else {
                // T * R * S
                localNodeMatrix = glm::translate(glm::mat4(1.0f), translation) * rotation * glm::scale(glm::mat4(1.0f), scale);
            }
            localNodeMatrix = parentMatrix * localNodeMatrix;

            // Parent node with children
            if (node.children.size() > 0) {
                for (auto i = 0; i < node.children.size(); i++) {
                    loadNode(model.nodes[node.children[i]], localNodeMatrix, model, indexBuffer, vertexBuffer, globalscale);
                }
            }

            // Node contains mesh data
            if (node.mesh > -1) {
                const tinygltf::Mesh mesh = model.meshes[node.mesh];
                for (size_t j = 0; j < mesh.primitives.size(); j++) {
                    const tinygltf::Primitive &primitive = mesh.primitives[j];
                    if (primitive.indices < 0) {
                        continue;
                    }
                    Primitive modPart;
                    modPart.indexBase = static_cast<uint32_t>(indexBuffer.size());
                    modPart.vertexBase = static_cast<uint32_t>(vertexBuffer.size());
                    modPart.material = primitive.material;
                    modPart.name = node.name;

                    // Vertices
                    {
                        const float *bufferPos = nullptr;
                        const float *bufferNormals = nullptr;
                        const float *bufferTexCoords = nullptr;

                        // Position attribute is required
                        assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

                        const tinygltf::Accessor &posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
                        const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
                        bufferPos = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));

                        if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                            const tinygltf::Accessor &normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
                            const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
                            bufferNormals = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
                        }

                        if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                            const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
                            const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
                            bufferTexCoords = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
                        }

                        modPart.vertexCount = posAccessor.count;
                        for (size_t v = 0; v < posAccessor.count; v++) {
                            Vertex vert{};
                            vert.pos = localNodeMatrix * glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
                            vert.pos *= globalscale;
                            vert.normal = glm::normalize(glm::mat3(localNodeMatrix) * glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
                            vert.uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
                            // Vulkan coordinate system
                            //vert.pos.y *= -1.0f;
                            //vert.normal.y *= -1.0f;
                            vertexBuffer.push_back(vert);
                        }
                    }
                    // Indices
                    {
                        const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
                        const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
                        const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

                        modPart.indexCount = static_cast<uint32_t>(accessor.count);

                        switch (accessor.componentType) {
                        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                            uint32_t *buf = new uint32_t[accessor.count];
                            memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indexBuffer.push_back(buf[index]);
                            }
                            break;
                        }
                        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                            uint16_t *buf = new uint16_t[accessor.count];
                            memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indexBuffer.push_back(buf[index]);
                            }
                            break;
                        }
                        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                            uint8_t *buf = new uint8_t[accessor.count];
                            memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indexBuffer.push_back(buf[index]);
                            }
                            break;
                        }
                        default:
                            std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
                            return;
                        }
                    }
                    primitives.push_back (modPart);
                }
            }
        }

        void loadImages(tinygltf::Model &gltfModel, vks::VulkanDevice *device, VkQueue transferQueue, bool loadOnly = false)
        {
            for (tinygltf::Image &image : gltfModel.images) {
                vkglTF::Texture texture;
                texture.fromglTfImage(image, device, transferQueue, loadOnly);
                textures.push_back(texture);
            }
        }

        void loadMaterials(tinygltf::Model &gltfModel, vks::VulkanDevice *device, VkQueue transferQueue)
        {
            for (tinygltf::Material &mat : gltfModel.materials) {
                vkglTF::Material material{};
                if (mat.values.find("baseColorTexture") != mat.values.end()) {
                    material.baseColorTexture = gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source + 1;
                }
                if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
                    material.metallicRoughnessTexture = gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source + 1;
                }
                if (mat.values.find("roughnessFactor") != mat.values.end()) {
                    material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
                }
                if (mat.values.find("metallicFactor") != mat.values.end()) {
                    material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
                }
                if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
                    material.normalTexture = gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source + 1;
                }
                if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
                    material.emissiveTexture = gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source + 1;
                }
                if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
                    material.occlusionTexture = gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source + 1;
                }
                if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
                    tinygltf::Parameter param = mat.additionalValues["alphaMode"];
                    if (param.string_value == "BLEND") {
                        material.alphaMode = ALPHAMODE_BLEND;
                    }
                    if (param.string_value == "MASK") {
                        material.alphaMode = ALPHAMODE_MASK;
                    }
                }
                if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
                    material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
                }
                materials.push_back(material);
            }
        }

        void loadFromFile(std::string filename, vks::VulkanDevice* _device, VkQueue transferQueue,
                          bool instancedRendering = false, float scale = 1.0f)
        {
            device = _device;//should be set in CTOR

            tinygltf::Model gltfModel;
            tinygltf::TinyGLTF gltfContext;
            std::string error;

#if defined(__ANDROID__)
            AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
            assert(asset);
            size_t size = AAsset_getLength(asset);
            assert(size > 0);
            char* fileData = new char[size];
            AAsset_read(asset, fileData, size);
            AAsset_close(asset);
            std::string baseDir;
            bool fileLoaded = gltfContext.LoadASCIIFromString(&gltfModel, &error, fileData, size, baseDir);
            free(fileData);
#else
            bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, filename.c_str());
#endif
            std::vector<uint32_t> indexBuffer;
            std::vector<Vertex> vertexBuffer;

            if (fileLoaded) {
                loadImages(gltfModel, device, transferQueue, instancedRendering);
                if (instancedRendering){
                    texArray = new Texture(device, transferQueue, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, textures, textureSize, textureSize);
                    for (Texture texture : textures){
                        texture.destroy();
                    }
                    textures.clear();
                }
                loadMaterials(gltfModel, device, transferQueue);
                if (instancedRendering){
                    buildMaterialBuffer();
                    updateMaterialBuffer();
                }
                const tinygltf::Scene &scene = gltfModel.scenes[gltfModel.defaultScene];
                for (size_t i = 0; i < scene.nodes.size(); i++) {
                    const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
                    loadNode(node, glm::mat4(1.0f), gltfModel, indexBuffer, vertexBuffer, scale);
                }
            }
            else {
                // TODO: throw
                std::cerr << "Could not load gltf file: " << error << std::endl;
                return;
            }

            size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
            size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);

            assert((vertexBufferSize > 0) && (indexBufferSize > 0));

            vks::Buffer vertexStaging, indexStaging;
            // Create staging buffers
            // Vertex data
            VK_CHECK_RESULT(device->createBuffer(
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &vertexStaging,
                vertexBufferSize,
                vertexBuffer.data()));
            // Index data
            VK_CHECK_RESULT(device->createBuffer(
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &indexStaging,
                indexBufferSize,
                indexBuffer.data()));

            // Create device local buffers
            // Vertex buffer
            VK_CHECK_RESULT(device->createBuffer(
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &vertices,
                vertexBufferSize));
            // Index buffer
            VK_CHECK_RESULT(device->createBuffer(
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &indices,
                indexBufferSize));

            // Copy from staging buffers
            VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            VkBufferCopy copyRegion = {};

            copyRegion.size = vertexBufferSize;
            vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vertices.buffer, 1, &copyRegion);

            copyRegion.size = indexBufferSize;
            vkCmdCopyBuffer(copyCmd, indexStaging.buffer, indices.buffer, 1, &copyRegion);

            device->flushCommandBuffer(copyCmd, transferQueue, true);

            vertexStaging.destroy();
            indexStaging.destroy();
        }

        uint32_t addInstance(uint32_t modelIdx, uint32_t partIdx,const glm::mat4& modelMat){
            uint32_t idx = instances.size();
            instances.push_back (partIdx);
            InstanceData id;
            id.materialIndex = primitives[partIdx].material;
            id.modelMat = modelMat;
            instanceDatas.push_back(id);
            return idx;
        }
        uint32_t addInstance(const std::string& name, const glm::mat4& modelMat) {
            for (int i=0; i<primitives.size(); i++) {
                if (name != primitives[i].name)
                    continue;
                return addInstance(0,i,modelMat);
            }
            return 0;
        }
        void buildCommandBuffer(VkCommandBuffer cmdBuff, bool drawInstanced = false){

            VkDeviceSize offsets[1] = { 0 };

            vkCmdBindVertexBuffers(cmdBuff, 0, 1, &vertices.buffer, offsets);
            vkCmdBindIndexBuffer(cmdBuff, indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            if (!drawInstanced) {
                for (auto primitive : primitives)
                    vkCmdDrawIndexed(cmdBuff, primitive.indexCount, 1, primitive.vertexBase, 0, 0);
                return;
            }

            vkCmdBindVertexBuffers(cmdBuff, 1, 1, &instancesBuff.buffer, offsets);

            //uint32_t modIdx = instances[0].modelIndex;
            uint32_t partIdx = instances[0];
            uint32_t instCount = 0;
            uint32_t instOffset = 0;

            for (int i = 0; i < instances.size(); i++){
                //if (modIdx != instances[i].modelIndex || partIdx != instances[i].partIndex) {
                if (partIdx != instances[i]) {
                    vkCmdDrawIndexed(cmdBuff,	primitives[partIdx].indexCount, instCount,
                                                primitives[partIdx].indexBase,
                                                primitives[partIdx].vertexBase, instOffset);

                    //modIdx = instances[i].modelIndex;
                    partIdx = instances[i];
                    instCount = 0;
                    instOffset = i;
                }
                instCount++;
            }
            if (instCount==0)
                return;

            vkCmdDrawIndexed(cmdBuff,	primitives[partIdx].indexCount, instCount,
                                        primitives[partIdx].indexBase,
                                        primitives[partIdx].vertexBase, instOffset);
        }

        void buildMaterialBuffer () {
            VK_CHECK_RESULT(device->createBuffer(
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &materialsBuff,
                sizeof(Material)*16));

            VK_CHECK_RESULT(materialsBuff.map());
            updateMaterialBuffer();
        }
        void buildInstanceBuffer (){
            if (instancesBuff.size > 0){
                vkDeviceWaitIdle(device->logicalDevice);
                instancesBuff.destroy();
            }

            instancesBuff.size = instanceDatas.size() * sizeof(InstanceData);

            VK_CHECK_RESULT(device->createBuffer(
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &instancesBuff,
                instancesBuff.size));

            VK_CHECK_RESULT(instancesBuff.map());
            updateInstancesBuffer();
        }

        void updateInstancesBuffer(){
            memcpy(instancesBuff.mapped, instanceDatas.data(), instanceDatas.size() * sizeof(InstanceData));
        }
        void updateMaterialBuffer(){
            memcpy(materialsBuff.mapped, materials.data(), sizeof(Material)*materials.size());
        }
    };
}
