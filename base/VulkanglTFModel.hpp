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

namespace vkglTF
{
    /*
        glTF texture loading class
    */
    struct Texture {
        vks::VulkanDevice *device;
        VkImage image;
        VkImageLayout imageLayout;
        VkDeviceMemory deviceMemory;
        VkImageCreateInfo infos = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkImageView view;
        VkSampler sampler;
        VkDescriptorImageInfo descriptor;

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
            {
                vkDestroySampler(device->logicalDevice, sampler, VK_NULL_HANDLE);
            }
            if (image){
                vkDestroyImage(device->logicalDevice, image, VK_NULL_HANDLE);
                vkFreeMemory(device->logicalDevice, deviceMemory, VK_NULL_HANDLE);
            }
        }
        // Create an image memory barrier for changing the layout of
        // an image and put it into an active command buffer
        // See chapter 11.4 "Image Layout" for details

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

        void buildMipmaps (VkQueue copyQueue) {
            // Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
            VkCommandBuffer blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            for (uint32_t i = 1; i < infos.mipLevels; i++) {
                VkImageBlit imageBlit{};

                imageBlit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
                imageBlit.srcOffsets[1] = {int32_t(infos.extent.width >> (i - 1)),int32_t(infos.extent.height >> (i - 1)),1};

                imageBlit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
                imageBlit.dstOffsets[1] = {int32_t(infos.extent.width >> i),int32_t(infos.extent.height >> i),1};

                VkImageSubresourceRange mipSubRange = {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1};

                setImageLayout(blitCmd, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipSubRange,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

                vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

                setImageLayout(blitCmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mipSubRange,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            }

            setImageLayout(blitCmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, infos.mipLevels, 0, 1},
                                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);


            device->flushCommandBuffer(blitCmd, copyQueue, true);
        }
        /*
            Load a texture from a glTF image (stored as vector of chars loaded via stb_image)
            Also generates the mip chain as glTF images are stored as jpg or png without any mips
        */
        void fromglTfImage(tinygltf::Image &gltfimage, vks::VulkanDevice *device, VkQueue copyQueue, int targetSize = -1)
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

            VkFormatProperties formatProperties;

            infos.imageType     = VK_IMAGE_TYPE_2D;
            infos.format        = VK_FORMAT_R8G8B8A8_UNORM;
            infos.extent        = {gltfimage.width, gltfimage.height, 1};
            infos.mipLevels     = static_cast<uint32_t>(floor(log2(std::max(infos.extent.width, infos.extent.height))) + 1.0);
            infos.arrayLayers   = 1;
            infos.samples       = VK_SAMPLE_COUNT_1_BIT;
            infos.tiling        = VK_IMAGE_TILING_OPTIMAL;
            infos.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
            infos.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            infos.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            infos.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

            descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            vkGetPhysicalDeviceFormatProperties(device->physicalDevice, infos.format, &formatProperties);
            assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
            assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            VkMemoryRequirements memReqs{};
            VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &infos, nullptr, &image));
            vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
            memAllocInfo.allocationSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
            VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

            vks::Buffer stagingBuffer;
            device->createBuffer (VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &stagingBuffer, bufferSize, buffer);

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

            buildMipmaps(copyQueue);


            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
            samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            samplerInfo.maxAnisotropy = 1.0;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxLod = (float)infos.mipLevels;
            samplerInfo.maxAnisotropy = 8.0f;
            samplerInfo.anisotropyEnable = VK_TRUE;
            VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = infos.format;
            viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.layerCount = 1;
            viewInfo.subresourceRange.levelCount = infos.mipLevels;
            VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));

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

    /*
        glTF model loading and rendering class
    */
    struct Model {
        uint32_t textureSize = 512; //texture array size w/h

        struct Vertex {
            glm::vec3 pos;
            glm::vec3 normal;
            glm::vec2 uv;
        };

        vks::Buffer vertices;
        vks::Buffer indices;

        std::vector<Primitive> primitives;

        std::vector<Texture>            textures;
        std::vector<Material>           materials;
        std::vector<VkDescriptorSet>    descriptorSets;

        void destroy(VkDevice device)
        {
            vkDestroyBuffer(device, vertices.buffer, nullptr);
            vkFreeMemory(device, vertices.memory, nullptr);
            vkDestroyBuffer(device, indices.buffer, nullptr);
            vkFreeMemory(device, indices.memory, nullptr);
            for (auto texture : textures) {
                texture.destroy();
            }
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
                            vert.pos.y *= -1.0f;
                            vert.normal.y *= -1.0f;
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

        void loadImages(tinygltf::Model &gltfModel, vks::VulkanDevice *device, VkQueue transferQueue)
        {
            for (tinygltf::Image &image : gltfModel.images) {
                vkglTF::Texture texture;
                texture.fromglTfImage(image, device, transferQueue);
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

        void loadFromFile(std::string filename, vks::VulkanDevice *device, VkQueue transferQueue, float scale = 1.0f)
        {
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
                loadImages(gltfModel, device, transferQueue);
                loadMaterials(gltfModel, device, transferQueue);
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

        void draw(VkCommandBuffer commandBuffer)
        {
            const VkDeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
            vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            for (auto primitive : primitives) {
                vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.indexBase, primitive.vertexBase, 0);
            }
        }
    };
}
