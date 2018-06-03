#pragma once

#include "VkEngine.h"

#define DRAW_FENCE_TIMEOUT 99900000

class vkRenderer
{
protected:

    bool prepared = false;

    VkSampleCountFlagBits       sampleCount;

    vks::VulkanDevice*          device;
    VulkanSwapChain*            swapChain;
    VkRenderPass                renderPass;
    std::vector<VkFramebuffer>  frameBuffers;
    VulkanExampleBase::UniformBuffers sharedUBOs;

    vks::ShadingContext*        shadingCtx;

    VkCommandPool               commandPool;
    std::vector<VkCommandBuffer>cmdBuffers;

    VkDescriptorSet         descriptorSet;

    std::vector<VkFence>    fences;
    VkSubmitInfo            submitInfo;

    VkPipeline              pipeline;
    VkPipelineLayout        pipelineLayout;

    VkFormat                depthFormat;

    std::vector<float>	vertices;
    vks::Buffer			vertexBuff;
    uint32_t			vBufferSize = 10000 * sizeof(float) * 6;

    virtual void prepare();
    virtual void prepareRenderPass();
    virtual void prepareFrameBuffer();
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

    virtual void create (vks::VulkanDevice* _device, VulkanSwapChain *_swapChain,
                                           VkFormat _depthFormat, VkSampleCountFlagBits _sampleCount,
                                           VulkanExampleBase::UniformBuffers& _sharedUbos);
    virtual void destroy();

    virtual void buildCommandBuffer ();
    virtual void draw(VkCommandBuffer cmdBuff);

    void submit (VkQueue queue, VkSemaphore *waitSemaphore, uint32_t waitSemaphoreCount);

    void drawLine(const glm::vec3& from,const glm::vec3& to,const glm::vec3& fromColor, const glm::vec3& toColor);
    void drawLine(const glm::vec3& from,const glm::vec3& to,const glm::vec3& color);

    virtual void flush();
    virtual void clear();
};


