#pragma once

#include "vke.h"
#include "texture.hpp"

namespace vks {
    enum AttachmentType {ColorAttach, DepthAttach, ResolveAttach};

    struct RenderTarget {
        uint32_t                    width;
        uint32_t                    height;
        ptrSwapchain    			swapChain   = nullptr;
        std::vector<vks::Texture>   attachments;
        uint                        presentableAttachment = 0;
        VkSampleCountFlagBits       samples;

        VkRenderPass                renderPass;
        std::vector<VkFramebuffer>  frameBuffers;

        RenderTarget(uint32_t _width, uint32_t _height, VkSampleCountFlagBits _samples = VK_SAMPLE_COUNT_1_BIT);
        RenderTarget(ptrSwapchain _swapChain, VkSampleCountFlagBits _samples = VK_SAMPLE_COUNT_1_BIT);
        virtual ~RenderTarget();

        void createAttachments();
        void createDefaultRenderPass();
        void createFrameBuffers();

        void updateSize();
    };
}
