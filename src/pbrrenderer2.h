#pragma once

#include "VkEngine.h"
#include "vkrenderer.h"
#include "VulkanTexture.hpp"
#include "VulkanglTFModel.hpp"


class pbrRenderer : public vks::vkRenderer
{
    void generateBRDFLUT();
    void generateCubemaps();
protected:
    virtual void configurePipelineLayout();
    virtual void loadRessources();
    virtual void freeRessources();
    virtual void prepareDescriptors();
    virtual void preparePipeline();
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

    vkglTF::Model               skybox;
    std::vector<vkglTF::Model>  models;

    pbrRenderer ();
    virtual ~pbrRenderer();

    virtual void destroy();

    void renderPrimitive(vkglTF::Primitive &primitive, VkCommandBuffer commandBuffer);

    void prepareModels();

    virtual void draw(VkCommandBuffer cmdBuff);


};
