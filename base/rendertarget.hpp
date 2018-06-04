#pragma once

#include "vke.h"
#include "texture.hpp"

namespace vks {

    struct RenderTarget {
        ptrSwapchain    			swapChain   = nullptr;
        std::vector<vks::Texture>   attachments;

        RenderTarget(ptrSwapchain _swapChain, VkFormat depthFormat, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
        virtual ~RenderTarget();
    };
}
