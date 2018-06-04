#pragma once

#include "VkEngine.h"
#include "VulkanSwapChain.hpp"

#define DRAW_FENCE_TIMEOUT 99900000

namespace vks {

    class vkRenderer
    {
    protected:

        bool prepared = false;

        vks::VulkanDevice*          device;
        vks::RenderTarget*          renderTarget;
        VkRenderPass                renderPass;
        std::vector<VkFramebuffer>  frameBuffers;
        vks::VkEngine::UniformBuffers sharedUBOs;

        vks::ShadingContext*        shadingCtx;

        VkCommandPool               commandPool;
        std::vector<VkCommandBuffer>cmdBuffers;

        VkDescriptorSet         descriptorSet;

        std::vector<VkFence>    fences;
        VkSubmitInfo            submitInfo;

        VkPipeline              pipeline;
        VkPipelineLayout        pipelineLayout;

        std::vector<float>	vertices;
        vks::Buffer			vertexBuff;
        uint32_t			vBufferSize = 10000 * sizeof(float) * 6;

        virtual void prepare();
        virtual void prepareRendering();
        virtual void configurePipelineLayout();
        virtual void loadRessources();
        virtual void freeRessources();
        virtual void prepareDescriptors();
        virtual void preparePipeline();

    public:
        VkSemaphore         drawComplete;
        uint32_t			vertexCount = 0;
        uint32_t			sdffVertexCount = 0;

        vkRenderer ();
        virtual ~vkRenderer();

        virtual void create (ptrVkDev _device, vks::RenderTarget* _renderTarget,
                                               VkEngine::UniformBuffers& _sharedUbos);
        virtual void destroy();

        virtual void buildCommandBuffer ();
        virtual void draw(VkCommandBuffer cmdBuff);

        void submit (VkQueue queue, VkSemaphore *waitSemaphore, uint32_t waitSemaphoreCount);

        void drawLine(const glm::vec3& from,const glm::vec3& to,const glm::vec3& fromColor, const glm::vec3& toColor);
        void drawLine(const glm::vec3& from,const glm::vec3& to,const glm::vec3& color);

        virtual void flush();
        virtual void clear();
    };
}

