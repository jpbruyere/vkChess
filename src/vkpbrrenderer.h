#pragma once

#include "VkEngine.h"
#include "VulkanTexture.hpp"
#include "VulkanglTFModel.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class vkPbrRenderer : public VulkanExampleBase
{
public:
    std::vector<vkglTF::Model> models2;

    VkFence                 fence;
    VkSemaphore             drawComplete;
    VkSubmitInfo            submitInfo;

    struct Textures {
        vks::TextureCubeMap environmentCube;
        vks::Texture2D      lutBrdf;
        vks::TextureCubeMap irradianceCube;
        vks::TextureCubeMap prefilteredCube;
    } textures;

    vkglTF::Model skybox;


    struct UniformBuffers {
        vks::Buffer matrices;
        vks::Buffer params;
    } sharedUBOs;

    struct MVPMatrices {
        glm::mat4 projection;
        glm::mat4 view3;
        glm::mat4 view;
        glm::vec3 camPos;
    } mvpMatrices;

    struct LightingParams {
        glm::vec4 lightDir = glm::vec4(0.0f, -0.5f, -0.5f, 1.0f);
        float exposure = 4.5f;
        float gamma = 1.0f;
        float prefilteredCubeMipLevels;
    } lightingParams;

    struct Pipelines {
        VkPipeline skybox;
        VkPipeline pbr;
        VkPipeline pbrAlphaBlend;
    } pipelines;

    VkPipelineLayout        pipelineLayout;
    VkDescriptorPool        descriptorPool;
    VkDescriptorSetLayout   dsLayoutModels;
    VkDescriptorSetLayout   dsLayoutScene;

    VkDescriptorSet         dsScene;


    glm::vec3 rotation = glm::vec3(0.0f, 135.0f, 0.0f);

    vkPbrRenderer();
    ~vkPbrRenderer();

    void prepareUniformBuffers();
    void updateUniformBuffers();
    void updateParams();

    void setupDescriptors();
    void preparePipelines();

    void generateBRDFLUT();
    void generateCubemaps();

    void renderPrimitive(vkglTF::Primitive &primitive, VkCommandBuffer commandBuffer);

    virtual void submit (VkQueue queue, VkSemaphore* waitSemaphore, uint32_t waitSemaphoreCount);
    virtual void prepare();
    virtual void loadAssets();
    virtual void buildCommandBuffers();
    virtual void viewChanged();
    virtual void keyPressed(uint32_t key);

};
