#pragma once

#include "VkEngine.h"
#include "vkrenderer.h"
#include "VulkanTexture.hpp"
#include "VulkanglTFModel.hpp"

class pbrRenderer2 : public vkRenderer
{
    void generateBRDFLUT();
    void generateCubemaps();
protected:
    void configurePipelineLayout();
    void loadRessources();
    void prepareDescriptors();
    void preparePipeline();
public:
    struct Pipelines {
        VkPipeline skybox;
        VkPipeline pbr;
        VkPipeline pbrAlphaBlend;
    } pipelines;

    VkDescriptorSet         dsScene;

    struct Textures {
        vks::TextureCubeMap environmentCube;
        vks::Texture2D      lutBrdf;
        vks::TextureCubeMap irradianceCube;
        vks::TextureCubeMap prefilteredCube;
    } textures;

    vkglTF::Model skybox;
    std::vector<vkglTF::Model> models;

    pbrRenderer2 ();
    ~pbrRenderer2();

    virtual void create(vks::VulkanDevice* _device, VulkanSwapChain *_swapChain,
                        VkFormat _depthFormat, VkSampleCountFlagBits _sampleCount,
                        VulkanExampleBase::UniformBuffers& _sharedUbos);

    void renderPrimitive(vkglTF::Primitive &primitive, VkCommandBuffer commandBuffer);

    void prepareModels();

    virtual void draw(VkCommandBuffer cmdBuff);
    virtual void destroy();


};
