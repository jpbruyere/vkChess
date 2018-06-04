#pragma once

#include "vke.h"
#include "texture.hpp"

namespace vks {
    enum AttachmentType {ColorAttach, DepthAttach, ResolveAttach};

    struct RenderTarget {
        ptrSwapchain    			swapChain   = nullptr;
        std::vector<vks::Texture>   attachments;
        uint                        presentableAttachment = 0;
        VkSampleCountFlagBits       samples;
        RenderTarget(ptrSwapchain _swapChain, VkSampleCountFlagBits _samples = VK_SAMPLE_COUNT_1_BIT);
        virtual ~RenderTarget();

        uint32_t getWidth();
        uint32_t getHeight();
        VkRenderPass createDefaultRenderPass ();
        void createFrameBuffers(VkRenderPass renderPass, std::vector<VkFramebuffer> &frameBuffers);
    };
}
