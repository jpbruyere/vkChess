#pragma once

#include "VkEngine.h"

namespace vks {
    struct Resource
    {
        VulkanDevice*           device;
        VkDeviceMemory          deviceMemory= VK_NULL_HANDLE;
        VkMemoryRequirements    memReqs    = {};

        virtual VkWriteDescriptorSet getWriteDescriptorSet(VkDescriptorSet ds, uint32_t binding, VkDescriptorType descriptorType) = 0;
    };
}
