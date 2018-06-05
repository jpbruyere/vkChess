#pragma once

#include "vke.h"
#include "texture.hpp"

namespace vks {
    enum AttachmentType {ColorAttach, DepthAttach, ResolveAttach};

    struct AttachmentDef {
        VkImageUsageFlags       usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        VkAttachmentLoadOp      loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp     storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        VkImageLayout           inLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkImageLayout           outLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSampleCountFlagBits   samples     = VK_SAMPLE_COUNT_1_BIT;
    };

    struct RenderTarget {
        ptrVkDev                    device;

        uint32_t                    width;
        uint32_t                    height;
        VkSampleCountFlagBits       samples;
        std::vector<AttachmentDef>  attachmentDefs;
        std::vector<vks::Texture>   attachments;
        int                         presentableAttachment = -1;

        ptrSwapchain    			swapChain   = nullptr;

        VkRenderPass                renderPass;
        std::vector<VkFramebuffer>  frameBuffers;

        RenderTarget(ptrVkDev _device, VkSampleCountFlagBits _samples = VK_SAMPLE_COUNT_1_BIT);
        virtual ~RenderTarget();

        /** @brief create default offscreen rendertarget with 1 color and 1 depth attachments */
        void createDefaultOffscreenTarget (uint32_t _width, uint32_t _height, VkFormat _colorFormat, VkFormat _depthFormat);
        /** @brief create default presentable rendertarget bound to a swapchain */
        void createDefaultPresentableTarget (ptrSwapchain _swapChain);

        void cleanupAttachments();
        void createAttachments(VkFormat _colorFormat, VkFormat _depthFormat);
        void createDefaultRenderPass();
        void createFrameBuffers();

        void updateSize();
    };
}
