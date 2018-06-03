#pragma once

#include "VkEngine.h"
#include "stb_image.h"

namespace vks
{
    struct Texture : public Resource {
        VkImage             image       = VK_NULL_HANDLE;
        VkImageView         view        = VK_NULL_HANDLE;
        VkSampler           sampler     = VK_NULL_HANDLE;

        VkImageCreateInfo   infos       = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkImageLayout       imageLayout;
        VkDescriptorImageInfo descriptor;

        void create (VulkanDevice* _device,
                        VkImageType imageType, VkFormat format, uint32_t width, uint32_t height,
                        VkImageUsageFlags usage, VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
                        uint32_t  mipLevels = 1,
                        uint32_t arrayLayers = 1,
                        VkImageCreateFlags flags = 0,
                        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE) {
            device = _device;
            infos.imageType = imageType;
            infos.format = format;
            infos.extent = {width, height, 1};
            infos.mipLevels = mipLevels == 0 ?
                    static_cast<uint32_t>(floor(log2(std::max(infos.extent.width, infos.extent.height))) + 1.0) : mipLevels;
            infos.arrayLayers = arrayLayers;
            infos.samples = samples;
            infos.tiling = tiling;
            infos.usage = usage;
            infos.flags = flags;
            infos.sharingMode = sharingMode;
            infos.initialLayout = initialLayout;

            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

            VK_CHECK_RESULT(vkCreateImage(device->dev, &infos, nullptr, &image));
            vkGetImageMemoryRequirements(device->dev, image, &memReqs);
            memAllocInfo.allocationSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, memProps);
            VK_CHECK_RESULT(vkAllocateMemory(device->dev, &memAllocInfo, nullptr, &deviceMemory));
            VK_CHECK_RESULT(vkBindImageMemory(device->dev, image, deviceMemory, 0));
        }
        Texture (){}
        Texture (vks::VulkanDevice*  _device){
            device = _device;
        }
        Texture (vks::VulkanDevice*  _device,
                 VkImageType imageType, VkFormat format, uint32_t width, uint32_t height,
                 VkImageUsageFlags usage, VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 uint32_t  mipLevels = 1, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
                 uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0,
                 VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                 VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE)
        {
            create(_device, imageType, format, width, height, usage, memProps, tiling, mipLevels, arrayLayers,
                   flags, samples, initialLayout, sharingMode);
        }
        Texture (vks::VulkanDevice*  _device,  VkQueue copyQueue, VkImageType imageType, VkFormat format,
                 std::vector<Texture> texArray, uint32_t width, uint32_t height,
                 VkImageUsageFlags _usage = VK_IMAGE_USAGE_SAMPLED_BIT) {

            uint32_t mipLevels = (uint32_t)floor(log2(std::max(width, height))) + 1;

            create(_device, imageType, format, width, height,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | _usage,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, mipLevels, texArray.size());

            VkCommandBuffer blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            //imageLayout = _imageLayout;
            for (uint l = 0; l < texArray.size(); l++) {
                Texture* inTex = &texArray[l];

                VkImageBlit firstMipBlit{};

                firstMipBlit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                firstMipBlit.srcOffsets[1] = {(int32_t)inTex->infos.extent.width,(int32_t)inTex->infos.extent.height,1};

                firstMipBlit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, l, 1};
                firstMipBlit.dstOffsets[1] = {(int32_t)infos.extent.width,(int32_t)infos.extent.height,1};

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

            createView(VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_IMAGE_ASPECT_COLOR_BIT, infos.mipLevels, infos.arrayLayers);
            createSampler(VK_FILTER_LINEAR,VK_SAMPLER_ADDRESS_MODE_REPEAT,VK_SAMPLER_MIPMAP_MODE_LINEAR,
                          VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);

            updateDescriptor();
        }

        /** @brief Update image descriptor from current sampler, view and image layout */
        void updateDescriptor()
        {
            descriptor.sampler = sampler;
            descriptor.imageView = view;
        }

        virtual VkWriteDescriptorSet getWriteDescriptorSet (VkDescriptorSet ds, uint32_t binding, VkDescriptorType descriptorType) {
            VkWriteDescriptorSet wds = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            wds.dstSet = ds;
            wds.descriptorType = descriptorType;
            wds.dstBinding = binding;
            wds.descriptorCount = 1;
            wds.pImageInfo = &descriptor;
            return wds;
        }

        /** @brief Release all Vulkan resources held by this texture */
        void destroy()
        {
            if (view)
                vkDestroyImageView  (device->dev, view, VK_NULL_HANDLE);
            if (sampler)
                vkDestroySampler    (device->dev, sampler, VK_NULL_HANDLE);
            if (image){
                vkDestroyImage      (device->dev, image, VK_NULL_HANDLE);
                vkFreeMemory        (device->dev, deviceMemory, VK_NULL_HANDLE);
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
            Buffer stagingBuffer;
            stagingBuffer.create (device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  bufferSize, buffer);

            descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            setImageLayout (copyCmd, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, infos.arrayLayers};
            bufferCopyRegion.imageExtent = infos.extent;

            vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

            setImageLayout(copyCmd, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

            device->flushCommandBuffer(copyCmd, copyQueue, true);

            stagingBuffer.destroy();
        }
        void createView (VkImageViewType viewType, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         uint32_t levelCount = 1, uint32_t layerCount = 1, VkComponentMapping components =
                         {VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A }) {

            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image      = image;
            viewInfo.viewType   = viewType;
            viewInfo.format     = infos.format;
            viewInfo.components = components;
            viewInfo.subresourceRange.aspectMask = aspectMask;
            viewInfo.subresourceRange.levelCount = levelCount;
            viewInfo.subresourceRange.layerCount = layerCount;
            VK_CHECK_RESULT(vkCreateImageView(device->dev, &viewInfo, nullptr, &view));
        }

        void createSampler (VkFilter filter = VK_FILTER_NEAREST, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                            VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                            VkCompareOp compareOp = VK_COMPARE_OP_NEVER) {

            VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            samplerInfo.magFilter = filter;
            samplerInfo.minFilter = filter;
            samplerInfo.mipmapMode = mipmapMode;
            samplerInfo.addressModeU = addressMode;
            samplerInfo.addressModeV = addressMode;
            samplerInfo.addressModeW = addressMode;
            samplerInfo.compareOp = compareOp;
            samplerInfo.borderColor = borderColor;
            samplerInfo.maxAnisotropy = device->enabledFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f;
            samplerInfo.anisotropyEnable = device->enabledFeatures.samplerAnisotropy;
            samplerInfo.minLod = 0;
            samplerInfo.maxLod = infos.mipLevels;
            VK_CHECK_RESULT(vkCreateSampler(device->dev, &samplerInfo, nullptr, &sampler));
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
        void loadStbLinearNoSampling (
            std::string filename,
            vks::VulkanDevice *device,
            VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            bool flipY = true)
        {
            device = device;

            stbi_set_flip_vertically_on_load(flipY);

            int w=0,h=0,channels=0;
            unsigned char *img = stbi_load(filename.c_str(),&w,&h,&channels,4);

            if (img == NULL){
                std::cerr << "unable to load image " << std::string(stbi_failure_reason());
                exit(-1);
            }

            create(device, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                   imageUsageFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_IMAGE_TILING_LINEAR,
                   1,1,0,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_LAYOUT_PREINITIALIZED);
            uint32_t imgSize = infos.extent.width * infos.extent.height * 4;

            VkImageSubresource subRes = {};
            subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            VkSubresourceLayout subResLayout;

            void *data;
            vkGetImageSubresourceLayout(device->dev, image, &subRes, &subResLayout);
            VK_CHECK_RESULT(vkMapMemory(device->dev, deviceMemory, 0, imgSize, 0, &data));
            memcpy(data, img, imgSize);	// Copy image data into memory
            vkUnmapMemory(device->dev, deviceMemory);

            stbi_image_free(img);
            stbi_set_flip_vertically_on_load(false);
        }
    };
}
