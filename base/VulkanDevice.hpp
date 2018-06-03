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

#include <exception>
#include <assert.h>
#include <algorithm>
#include <vector>
//#include "vulkan/vulkan.h"
#include "macros.h"
#include "VkEngine.h"

//#include "VulkanBuffer.hpp"

namespace vks
{
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

        operator VkDevice() { return dev; }

        /**
        * Default constructor
        *
        * @param physicalDevice Physical device that is to be used
        */
        VulkanDevice(VkPhysicalDevice physicalDevice)
        {
            assert(physicalDevice);
            this->phy = physicalDevice;

            // Store Properties features, limits and properties of the physical device for later use
            // Device properties also contain limits and sparse properties
            vkGetPhysicalDeviceProperties(physicalDevice, &properties);
            // Features should be checked by the examples before using them
            vkGetPhysicalDeviceFeatures(physicalDevice, &features);
            // Memory properties are used regularly for creating all kinds of buffers
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
            // Queue family properties, used for setting up requested queues upon device creation
            uint32_t queueFamilyCount;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            assert(queueFamilyCount > 0);
            queueFamilyProperties.resize(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
        }

        /**
        * Default destructor
        *
        * @note Frees the logical device
        */
        ~VulkanDevice()
        {
            if (pipelineCache){
                if (savePLCache){
                    std::ofstream os("pipeline.cache", std::ofstream::binary);
                    size_t plCacheSize = 0;
                    char* plCache = nullptr;
                    VK_CHECK_RESULT(vkGetPipelineCacheData(dev, pipelineCache, &plCacheSize, nullptr));
                    plCache = new char [plCacheSize];
                    VK_CHECK_RESULT(vkGetPipelineCacheData(dev, pipelineCache, &plCacheSize, plCache));
                    os.write (plCache, plCacheSize);
                    os.close();
                    delete[] plCache;
                }
                vkDestroyPipelineCache(dev, pipelineCache, NULL);
            }
            if (commandPool)
                vkDestroyCommandPool(dev, commandPool, nullptr);
            if (dev)
                vkDestroyDevice(dev, nullptr);
        }

        uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr)
        {
            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
                if ((typeBits & 1) == 1) {
                    if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                        if (memTypeFound) {
                            *memTypeFound = true;
                        }
                        return i;
                    }
                }
                typeBits >>= 1;
            }

            if (memTypeFound) {
                *memTypeFound = false;
                return 0;
            } else {
                throw std::runtime_error("Could not find a matching memory type");
            }
        }


        uint32_t getQueueFamilyIndex(VkQueueFlagBits queueFlags)
        {
            // Dedicated queue for compute
            // Try to find a queue family index that supports compute but not graphics
            if (queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
                    if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)) {
                        return i;
                        break;
                    }
                }
            }

            // For other queue types or if no separate compute queue is present, return the first one to support the requested flags
            for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
                if (queueFamilyProperties[i].queueFlags & queueFlags) {
                    return i;
                    break;
                }
            }

            throw std::runtime_error("Could not find a matching queue family index");
        }

        VkResult createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)
        {
            std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

            const float defaultQueuePriority(0.0f);

            // Graphics queue
            if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT) {
                queueFamilyIndices.graphics = getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = queueFamilyIndices.graphics;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos.push_back(queueInfo);
            } else {
                queueFamilyIndices.graphics = VK_NULL_HANDLE;
            }

            // Dedicated compute queue
            if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT) {
                queueFamilyIndices.compute = getQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
                if (queueFamilyIndices.compute != queueFamilyIndices.graphics) {
                    // If compute family index differs, we need an additional queue create info for the compute queue
                    VkDeviceQueueCreateInfo queueInfo{};
                    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                    queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
                    queueInfo.queueCount = 1;
                    queueInfo.pQueuePriorities = &defaultQueuePriority;
                    queueCreateInfos.push_back(queueInfo);
                }
            } else {
                // Else we use the same queue
                queueFamilyIndices.compute = queueFamilyIndices.graphics;
            }

            // Create the logical device representation
            std::vector<const char*> deviceExtensions(enabledExtensions);
            deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

            VkDeviceCreateInfo deviceCreateInfo = {};
            deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());;
            deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
            deviceCreateInfo.pEnabledFeatures = &enabledFeatures;

            if (deviceExtensions.size() > 0) {
                deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
                deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
            }

            VkResult result = vkCreateDevice(phy, &deviceCreateInfo, nullptr, &dev);

            if (result == VK_SUCCESS)
                commandPool = createCommandPool(queueFamilyIndices.graphics);

            std::ifstream is ("pipeline.cache", std::ifstream::binary);
            char* buffer = nullptr;
            int length = 0;
            VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
            if (is) {
                is.seekg (0, is.end);
                length = is.tellg();
                is.seekg (0, is.beg);
                buffer = new char [length];
                is.read (buffer,length);
                is.close();
                pipelineCacheCreateInfo.initialDataSize = length;
                pipelineCacheCreateInfo.pInitialData = buffer;
            }

            VK_CHECK_RESULT(vkCreatePipelineCache (dev, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
            if (buffer)
                delete[] buffer;

            this->enabledFeatures = enabledFeatures;

            vkGetDeviceQueue(dev, queueFamilyIndices.graphics, 0, &queue);

            return result;
        }

        VkCommandPool createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
        {
            VkCommandPoolCreateInfo cmdPoolInfo = {};
            cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
            cmdPoolInfo.flags = createFlags;
            VkCommandPool cmdPool;
            VK_CHECK_RESULT(vkCreateCommandPool(dev, &cmdPoolInfo, nullptr, &cmdPool));
            return cmdPool;
        }

        VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, bool begin = false)
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
            cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufAllocateInfo.commandPool = commandPool;
            cmdBufAllocateInfo.level = level;
            cmdBufAllocateInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuffer;
            VK_CHECK_RESULT(vkAllocateCommandBuffers(dev, &cmdBufAllocateInfo, &cmdBuffer));

            // If requested, also start recording for the new command buffer
            if (begin) {
                VkCommandBufferBeginInfo cmdBufInfo{};
                cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
            }

            return cmdBuffer;
        }

        /**
        * Finish command buffer recording and submit it to a queue
        *
        * @param commandBuffer Command buffer to flush
        * @param queue Queue to submit the command buffer to
        * @param free (Optional) Free the command buffer once it has been submitted (Defaults to true)
        *
        * @note The queue that the command buffer is submitted to must be from the same family index as the pool it was allocated from
        * @note Uses a fence to ensure command buffer has finished executing
        */
        void flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free = true)
        {
            VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            // Create fence to ensure that the command buffer has finished executing
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence;
            VK_CHECK_RESULT(vkCreateFence(dev, &fenceInfo, nullptr, &fence));

            // Submit to the queue
            VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
            // Wait for the fence to signal that command buffer has finished executing
            VK_CHECK_RESULT(vkWaitForFences(dev, 1, &fence, VK_TRUE, 100000000000));

            vkDestroyFence(dev, fence, nullptr);

            if (free) {
                vkFreeCommandBuffers(dev, commandPool, 1, &commandBuffer);
            }
        }
        VkSemaphore createSemaphore ()
        {
            VkSemaphore sema = VK_NULL_HANDLE;
            VkSemaphoreCreateInfo semaphoreCreateInfo {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK_RESULT(vkCreateSemaphore (dev, &semaphoreCreateInfo, nullptr, &sema));
            return sema;
        }
        VkFence createFence (bool signaled = false) {
            VkFence fence = VK_NULL_HANDLE;
            VkFenceCreateInfo fenceCreateInfo {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, VK_NULL_HANDLE,
                        signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0};
            VK_CHECK_RESULT(vkCreateFence (dev, &fenceCreateInfo, nullptr, &fence));
            return fence;
        }
        inline void destroyFence (VkFence fence) {
            vkDestroyFence (dev, fence, VK_NULL_HANDLE);
        }
        inline void destroySemaphore (VkSemaphore semaphore) {
            vkDestroySemaphore (dev, semaphore, VK_NULL_HANDLE);
        }
    };
}
