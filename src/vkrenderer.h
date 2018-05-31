#pragma once

#include "VulkanExampleBase.h"
#include "VulkanSwapChain.hpp"

#define DRAW_FENCE_TIMEOUT 9900000

class vkRenderer
{
protected:

    bool prepared = false;

    VkSampleCountFlagBits       sampleCount;

    vks::VulkanDevice*          device;
    VulkanSwapChain*            swapChain;
    VkRenderPass                renderPass;
    std::vector<VkFramebuffer>  frameBuffers;
    vks::Buffer*                uboMatrices;

    VkCommandPool               commandPool;
    std::vector<VkCommandBuffer>cmdBuffers;

    VkDescriptorSet         descriptorSet;

    std::vector<VkFence>    fences;
    VkSubmitInfo            submitInfo;

    VkPipeline              pipeline;
    VkPipelineLayout        pipelineLayout;
    VkPipelineCache         pipelineCache;

    VkFormat                depthFormat;

    std::vector<float>	vertices;
    vks::Buffer			vertexBuff;
    uint32_t			vBufferSize = 10000 * sizeof(float) * 6;


    virtual void destroy();
    virtual void prepareRenderPass();
    virtual void prepareFrameBuffer();
    virtual void prepareDescriptors();
    virtual void preparePipeline();

public:
    VkDescriptorPool        descriptorPool;
    VkDescriptorSetLayout   descriptorSetLayout;

    VkSemaphore         drawComplete;
    uint32_t			vertexCount = 0;
    uint32_t			sdffVertexCount = 0;

    vkRenderer (vks::VulkanDevice* _device, VulkanSwapChain* _swapChain, VkFormat _depthFormat,
                    VkSampleCountFlagBits _sampleCount, vks::Buffer *_uboMatrices);
    virtual ~vkRenderer();

    virtual void buildCommandBuffer ();
    virtual void prepare();

    void submit (VkQueue queue, VkSemaphore *waitSemaphore, uint32_t waitSemaphoreCount);

    void drawLine(const glm::vec3& from,const glm::vec3& to,const glm::vec3& fromColor, const glm::vec3& toColor);
    void drawLine(const glm::vec3& from,const glm::vec3& to,const glm::vec3& color);

    virtual void flush();
    virtual void clear();
};


