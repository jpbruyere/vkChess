/*
* Class wrapping access to the swap chain
*
* A swap chain is a collection of framebuffers used for rendering and presentation to the windowing system
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "VulkanSwapChain.hpp"
#include "VkEngine.h"

vks::VulkanSwapChain::VulkanSwapChain(ptrVkEngine _vke, bool vsync) {
    vke = _vke;
    GET_INSTANCE_PROC_ADDR(vke->instance, GetPhysicalDeviceSurfaceSupportKHR);
    GET_INSTANCE_PROC_ADDR(vke->instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
    GET_INSTANCE_PROC_ADDR(vke->instance, GetPhysicalDeviceSurfaceFormatsKHR);
    GET_INSTANCE_PROC_ADDR(vke->instance, GetPhysicalDeviceSurfacePresentModesKHR);
    GET_DEVICE_PROC_ADDR(vke->device->dev, CreateSwapchainKHR);
    GET_DEVICE_PROC_ADDR(vke->device->dev, DestroySwapchainKHR);
    GET_DEVICE_PROC_ADDR(vke->device->dev, GetSwapchainImagesKHR);
    GET_DEVICE_PROC_ADDR(vke->device->dev, AcquireNextImageKHR);
    GET_DEVICE_PROC_ADDR(vke->device->dev, QueuePresentKHR);

    infos.surface           = vke->surface;
    infos.imageUsage        = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    infos.imageSharingMode  = VK_SHARING_MODE_EXCLUSIVE;
    infos.imageArrayLayers  = 1;
    infos.clipped           = VK_TRUE;

    uint32_t formatCount;
    VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceFormatsKHR(
                        vke->device->phy, vke->surface, &formatCount, NULL));
    assert(formatCount > 0);

    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceFormatsKHR(
                        vke->device->phy, vke->surface, &formatCount, surfaceFormats.data()));

    // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
    // there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
    if ((formatCount == 1) && (surfaceFormats[0].format == VK_FORMAT_UNDEFINED))
    {
        infos.imageFormat       = VK_FORMAT_B8G8R8A8_UNORM;
        infos.imageColorSpace   = surfaceFormats[0].colorSpace;
    }
    else
    {
        // iterate over the list of available surface format and
        // check for the presence of VK_FORMAT_B8G8R8A8_UNORM
        bool found_B8G8R8A8_UNORM = false;
        for (auto&& surfaceFormat : surfaceFormats)
        {
            if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
            {
                infos.imageFormat       = surfaceFormat.format;
                infos.imageColorSpace   = surfaceFormat.colorSpace;
                found_B8G8R8A8_UNORM = true;
                break;
            }
        }

        // in case VK_FORMAT_B8G8R8A8_UNORM is not available
        // select the first available color format
        if (!found_B8G8R8A8_UNORM)
        {
            infos.imageFormat       = surfaceFormats[0].format;
            infos.imageColorSpace   = surfaceFormats[0].colorSpace;
        }
    }

    // Set additional usage flag for blitting from the swapchain images if supported
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(vke->device->phy, infos.imageFormat, &formatProps);
    if ((formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT_KHR)
            || (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
        infos.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkSurfaceCapabilitiesKHR surfCaps;
    VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceCapabilitiesKHR(
                        vke->device->phy, vke->surface, &surfCaps));

    infos.minImageCount = surfCaps.minImageCount + 1;
    if ((surfCaps.maxImageCount > 0) && (infos.minImageCount > surfCaps.maxImageCount))
        infos.minImageCount = surfCaps.maxImageCount;
    if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        infos.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    else
        infos.preTransform = surfCaps.currentTransform;

    infos.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (auto& compositeAlphaFlag : compositeAlphaFlags) {
        if (surfCaps.supportedCompositeAlpha & compositeAlphaFlag) {
            infos.compositeAlpha = compositeAlphaFlag;
            break;
        };
    }

    uint32_t presentModeCount;
    VK_CHECK_RESULT(fpGetPhysicalDeviceSurfacePresentModesKHR(
                        vke->device->phy, vke->surface, &presentModeCount, NULL));
    assert(presentModeCount > 0);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_CHECK_RESULT(fpGetPhysicalDeviceSurfacePresentModesKHR(
                        vke->device->phy, vke->surface, &presentModeCount, presentModes.data()));

    // v-sync mode
    infos.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync)
    {
        for (size_t i = 0; i < presentModeCount; i++)
        {
            if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                infos.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
            if ((infos.presentMode != VK_PRESENT_MODE_MAILBOX_KHR) && (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR))
                infos.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    depthFormat = vke->device->getSuitableDepthFormat();

    presentCompleteSemaphore = vke->device->createSemaphore();

    presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.swapchainCount = 1;
}
vks::VulkanSwapChain::~VulkanSwapChain()
{
    vke->device->destroySemaphore(presentCompleteSemaphore);

    if (swapChain != VK_NULL_HANDLE)
    {
        for (uint32_t i = 0; i < imageCount; i++)
            vkDestroyImageView(vke->device->dev, buffers[i].view, nullptr);
        fpDestroySwapchainKHR(vke->device->dev, swapChain, nullptr);
    }
    swapChain = VK_NULL_HANDLE;
}

void vks::VulkanSwapChain::create(uint32_t& width, uint32_t& height)
{
    vkDeviceWaitIdle (vke->device->dev);

    infos.oldSwapchain      = swapChain;

    VkSurfaceCapabilitiesKHR surfCaps;
    VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceCapabilitiesKHR(
                        vke->device->phy, vke->surface, &surfCaps));

    if (surfCaps.currentExtent.width == 0xFFFFFFFF) {
        if (width < surfCaps.minImageExtent.width)
            width = surfCaps.minImageExtent.width;
        else if (width > surfCaps.maxImageExtent.width)
            width = surfCaps.maxImageExtent.width;
        if (height < surfCaps.minImageExtent.height)
            height = surfCaps.minImageExtent.height;
        else if (height > surfCaps.maxImageExtent.height)
            height = surfCaps.maxImageExtent.height;
    }else{
        // If the surface size is defined, the swap chain size must match
        width = surfCaps.currentExtent.width;
        height= surfCaps.currentExtent.height;
    }

    infos.imageExtent = {width, height};


    VK_CHECK_RESULT(fpCreateSwapchainKHR(vke->device->dev, &infos, nullptr, &swapChain));

    presentInfo.pSwapchains = &swapChain;

    // If an existing swap chain is re-created, destroy the old swap chain
    // This also cleans up all the presentable images
    if (infos.oldSwapchain != VK_NULL_HANDLE)
    {
        for (uint32_t i = 0; i < imageCount; i++)
            vkDestroyImageView(vke->device->dev, buffers[i].view, nullptr);
        fpDestroySwapchainKHR(vke->device->dev, infos.oldSwapchain, nullptr);
    }

    VK_CHECK_RESULT(fpGetSwapchainImagesKHR(vke->device->dev, swapChain, &imageCount, NULL));
    VkImage images[imageCount];
    VK_CHECK_RESULT(fpGetSwapchainImagesKHR(vke->device->dev, swapChain, &imageCount, images));

    // Get the swap chain buffers containing the image and imageview
    buffers.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo colorAttachmentView = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        colorAttachmentView.format = infos.imageFormat;
        colorAttachmentView.components = {
            VK_COMPONENT_SWIZZLE_R,
            VK_COMPONENT_SWIZZLE_G,
            VK_COMPONENT_SWIZZLE_B,
            VK_COMPONENT_SWIZZLE_A
        };
        colorAttachmentView.subresourceRange= {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        colorAttachmentView.viewType        = VK_IMAGE_VIEW_TYPE_2D;

        buffers[i].image = images[i];

        colorAttachmentView.image = buffers[i].image;

        VK_CHECK_RESULT(vkCreateImageView(vke->device->dev, &colorAttachmentView, nullptr, &buffers[i].view));
    }
}


VkResult vks::VulkanSwapChain::acquireNextImage()
{
    // By setting timeout to UINT64_MAX we will always wait until the next image has been acquired or an actual error is thrown
    // With that we don't have to handle VK_NOT_READY
    return fpAcquireNextImageKHR(vke->device->dev, swapChain, UINT64_MAX, presentCompleteSemaphore, (VkFence)nullptr, &currentBuffer);
}

VkResult vks::VulkanSwapChain::queuePresent(VkQueue queue, VkSemaphore waitSemaphore)
{
    presentInfo.pImageIndices = &currentBuffer;
    // Check if a wait semaphore has been specified to wait for before presenting the image
    if (waitSemaphore != VK_NULL_HANDLE)
    {
        presentInfo.pWaitSemaphores = &waitSemaphore;
        presentInfo.waitSemaphoreCount = 1;
    }
    return fpQueuePresentKHR(queue, &presentInfo);
}


