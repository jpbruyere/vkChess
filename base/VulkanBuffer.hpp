/*
* Vulkan buffer class
*
* Encapsulates a Vulkan buffer
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "vke.h"

#include "VulkanDevice.hpp"
#include "resource.hpp"

namespace vks
{
    struct Buffer : public Resource
    {
        VkBuffer            buffer = VK_NULL_HANDLE;

        VkBufferCreateInfo  infos = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};

        VkDescriptorBufferInfo descriptor;
        VkDeviceSize size = 0;
        VkDeviceSize alignment = 0;
        void* mapped = nullptr;

        /** @brief Usage flags to be filled by external source at buffer creation (to query at some later point) */
        VkBufferUsageFlags usageFlags;
        /** @brief Memory propertys flags to be filled by external source at buffer creation (to query at some later point) */
        VkMemoryPropertyFlags memoryPropertyFlags;

        void create(ptrVkDev _device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags _memoryPropertyFlags, VkDeviceSize size, void *data = nullptr)
        {
            device = _device;

            infos.size          = size;
            infos.usage         = usageFlags;
            infos.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

            memoryPropertyFlags = _memoryPropertyFlags;

            VK_CHECK_RESULT(vkCreateBuffer(device->dev, &infos, nullptr, &buffer));

            vkGetBufferMemoryRequirements(device->dev, buffer, &memReqs);

            VkMemoryAllocateInfo memAlloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, _memoryPropertyFlags);

            VK_CHECK_RESULT(vkAllocateMemory(device->dev, &memAlloc, nullptr, &deviceMemory));

            size = memAlloc.allocationSize;

            // If a pointer to the buffer data has been passed, map the buffer and copy over the data
            if (data != nullptr)
            {
                VK_CHECK_RESULT(map());
                memcpy(mapped, data, size);
                unmap();
            }

            // Initialize a default descriptor that covers the whole buffer size
            setupDescriptor();

            // Attach the memory to the buffer object
            VK_CHECK_RESULT(bind());
        }

        VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
        {
            return vkMapMemory(device->dev, deviceMemory, offset, size, 0, &mapped);
        }
        void unmap()
        {
            if (mapped)
            {
                vkUnmapMemory(device->dev, deviceMemory);
                mapped = nullptr;
            }
        }

        VkResult bind(VkDeviceSize offset = 0)
        {
            return vkBindBufferMemory(device->dev, buffer, deviceMemory, offset);
        }

        void setupDescriptor(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
        {
            descriptor.offset = offset;
            descriptor.buffer = buffer;
            descriptor.range = size;
        }

        virtual VkWriteDescriptorSet getWriteDescriptorSet(VkDescriptorSet ds, uint32_t binding, VkDescriptorType descriptorType) {
            VkWriteDescriptorSet wds = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            wds.dstSet = ds;
            wds.descriptorType = descriptorType;
            wds.dstBinding = binding;
            wds.descriptorCount = 1;
            wds.pBufferInfo = &descriptor;
            return wds;
        }

        /**
        * Copies the specified data to the mapped buffer
        *
        * @param data Pointer to the data to copy
        * @param size Size of the data to copy in machine units
        *
        */
        void copyTo(void* data, VkDeviceSize size)
        {
            assert(mapped);
            memcpy(mapped, data, size);
        }

        /**
        * Flush a memory range of the buffer to make it visible to the device
        *
        * @note Only required for non-coherent memory
        *
        * @param size (Optional) Size of the memory range to flush. Pass VK_WHOLE_SIZE to flush the complete buffer range.
        * @param offset (Optional) Byte offset from beginning
        *
        * @return VkResult of the flush call
        */
        VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
        {
            VkMappedMemoryRange mappedRange = {};
            mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mappedRange.memory = deviceMemory;
            mappedRange.offset = offset;
            mappedRange.size = size;
            return vkFlushMappedMemoryRanges(device->dev, 1, &mappedRange);
        }

        /**
        * Invalidate a memory range of the buffer to make it visible to the host
        *
        * @note Only required for non-coherent memory
        *
        * @param size (Optional) Size of the memory range to invalidate. Pass VK_WHOLE_SIZE to invalidate the complete buffer range.
        * @param offset (Optional) Byte offset from beginning
        *
        * @return VkResult of the invalidate call
        */
        VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
        {
            VkMappedMemoryRange mappedRange = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            mappedRange.memory = deviceMemory;
            mappedRange.offset = offset;
            mappedRange.size = size;
            return vkInvalidateMappedMemoryRanges(device->dev, 1, &mappedRange);
        }

        /**
        * Release all Vulkan resources held by this buffer
        */
        void destroy()
        {
            unmap();

            if (deviceMemory)
                vkFreeMemory(device->dev, deviceMemory, nullptr);
            if (buffer)
                vkDestroyBuffer(device->dev, buffer, nullptr);

        }

    };
}
