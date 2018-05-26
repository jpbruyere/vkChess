#pragma once

#include "VulkanExampleBase.h"
#include "VulkanTexture.hpp"
#include "VulkanglTFModel.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class vkPbrRenderer : public VulkanExampleBase
{
public:
	struct Textures {
		vks::TextureCubeMap environmentCube;
		vks::Texture2D lutBrdf;
		vks::TextureCubeMap irradianceCube;
		vks::TextureCubeMap prefilteredCube;
	} textures;

	struct Models {
		vkglTF::Model object;
		vkglTF::Model skybox;
	} models;


	struct UniformBuffers {
		vks::Buffer matrices;
		vks::Buffer skybox;
		vks::Buffer params;
	} uniformBuffers;

	struct UBOMatrices {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec3 camPos;
		float flipUV = 1.0f;
	} uboMatrices;

	struct UBOParams {
		glm::vec4 lightDir = glm::vec4(0.0f, -0.5f, -0.5f, 1.0f);
		float exposure = 4.5f;
		float gamma = 2.2f;
		float prefilteredCubeMipLevels;
	} uboParams;

	VkPipelineLayout pipelineLayout;

	struct Pipelines {
		VkPipeline skybox;
		VkPipeline pbr;
		VkPipeline pbrAlphaBlend;
	} pipelines;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout scene;
		VkDescriptorSetLayout material;
	} descriptorSetLayouts;

	struct DescriptorSets {
		VkDescriptorSet scene;
		VkDescriptorSet materials;
		VkDescriptorSet skybox;
	} descriptorSets;

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

	virtual void prepare();
	virtual void loadAssets();
	virtual void buildCommandBuffers();
	virtual void render();
	virtual void viewChanged();
	virtual void keyPressed(uint32_t key);

};
