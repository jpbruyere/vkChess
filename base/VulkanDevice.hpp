/*
* Vulkan device class
*
* Encapsulates a physical Vulkan device and it's logical representation
*
* Copyright (C) 2016-2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "vke.h"

//#include "VulkanBuffer.hpp"

namespace vks
{
    struct vkPhyInfo{
        VkPhysicalDevice                    phy;
        VkPhysicalDeviceMemoryProperties    memProps;
        VkPhysicalDeviceProperties          properties;
        VkPhysicalDeviceFeatures            features;
        VkPhysicalDeviceFeatures            enabledFeatures;
        VkQueueFamilyProperties*            queues;
        uint32_t                            queueCount;
        std::vector<int>                    cQueues;//compute, dedicated first
        std::vector<int>                    gQueues;//graphic, idem
        std::vector<int>                    tQueues;//transfer, idem
        int                                 pQueue;//presentation

        std::vector<VkDeviceQueueCreateInfo> pQueueInfos;
        std::vector<std::vector<float>>      qPriorities;

        vkPhyInfo (){}
        vkPhyInfo (VkPhysicalDevice _phy, VkSurfaceKHR surface = VK_NULL_HANDLE) {
            phy = _phy;
//            cQueues.clear();
//            gQueues.clear();
//            tQueues.clear();
            pQueue = -1;
//            pQueueInfos.clear();
//            qPriorities.clear();

            vkGetPhysicalDeviceProperties       (phy, &properties);
            vkGetPhysicalDeviceMemoryProperties (phy, &memProps);
            vkGetPhysicalDeviceFeatures         (phy, &features);

            vkGetPhysicalDeviceQueueFamilyProperties (phy, &queueCount, NULL);
            queues = (VkQueueFamilyProperties*)malloc(queueCount * sizeof(VkQueueFamilyProperties));
            vkGetPhysicalDeviceQueueFamilyProperties (phy, &queueCount, queues);

            //identify dedicated queues
            //try to find dedicated queues first
            VkBool32 present = VK_FALSE;
            for (uint j=0; j<queueCount; j++){
                switch (queues[j].queueFlags) {
                case VK_QUEUE_GRAPHICS_BIT:
                    if (surface){
                        vkGetPhysicalDeviceSurfaceSupportKHR(phy, j, surface, &present);
                        if (present && pQueue<0)
                            pQueue = j;
                    }
                    gQueues.push_back (j);
                    break;
                case VK_QUEUE_COMPUTE_BIT:
                    cQueues.push_back (j);
                    break;
                case VK_QUEUE_TRANSFER_BIT:
                    tQueues.push_back (j);
                    break;
                }
            }
            //add queues that include flag
            for (uint j=0; j<queueCount; j++){
                if (queues[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    if (surface) {
                        vkGetPhysicalDeviceSurfaceSupportKHR(phy, j, surface, &present);
                        if (present && pQueue<0)
                            pQueue = j;
                    }
                    if (std::find(gQueues.begin(), gQueues.end(), j) == gQueues.end())
                        gQueues.push_back(j);
                }
                if (queues[j].queueFlags & VK_QUEUE_COMPUTE_BIT){
                    if (std::find(cQueues.begin(), cQueues.end(), j) == cQueues.end())
                        cQueues.push_back(j);
                }
                if (queues[j].queueFlags & VK_QUEUE_TRANSFER_BIT){
                    if (std::find(tQueues.begin(), tQueues.end(), j) == tQueues.end())
                        tQueues.push_back(j);
                }
            }
        }

        void selectQueue (int qIndex, float priority = 0.f) {
            auto it = find_if(pQueueInfos.begin(), pQueueInfos.end(), [&qIndex](const VkDeviceQueueCreateInfo& qci)
                {return qci.queueFamilyIndex == qIndex;});
            if (it == pQueueInfos.end()) {
                VkDeviceQueueCreateInfo dqci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                dqci.queueCount = 1;
                dqci.queueFamilyIndex = qIndex;
                pQueueInfos.push_back (dqci);
                qPriorities.push_back ({priority});
                return;
            }

            int qciIdx = std::distance(pQueueInfos.begin(), it);
            pQueueInfos[qciIdx].queueCount ++;
            qPriorities[qciIdx].push_back(priority);
        }

    };
    struct VulkanDevice
    {
        VkPhysicalDevice            phy;
        VkDevice                    dev;
        VkPhysicalDeviceProperties  properties;
        VkPhysicalDeviceFeatures    features;
        VkPhysicalDeviceFeatures    enabledFeatures;
        VkPhysicalDeviceMemoryProperties    memoryProperties;
        std::vector<VkQueueFamilyProperties>queueFamilyProperties;

        VkCommandPool   commandPool     = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache   = VK_NULL_HANDLE;
        bool            savePLCache     = true;

        VkQueue queue;

        struct {
            uint32_t graphics;
            uint32_t compute;
        } queueFamilyIndices;

        VulkanDevice(vkPhyInfo phyInfos, const std::vector<const char *> &devLayers);
        ~VulkanDevice();

        uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr);
        VkFormat getSuitableDepthFormat ();
        uint32_t getQueueFamilyIndex(VkQueueFlagBits queueFlags);

        VkCommandPool createCommandPool(uint32_t queueFamilyIndex,
                                        VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, bool begin = false);

        void flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free = true);
        VkSemaphore createSemaphore ();
        VkFence createFence (bool signaled = false);
        void destroyFence (VkFence fence);
        void destroySemaphore (VkSemaphore semaphore);
    };
}
