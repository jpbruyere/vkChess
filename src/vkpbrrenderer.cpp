#include "vkpbrrenderer.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "tiny_gltf.h"

const VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
/*
    Utility functions
*/
vkPbrRenderer::vkPbrRenderer()
{

}
vkPbrRenderer::~vkPbrRenderer() {
    vkDestroyPipeline(device, pipelines.skybox, nullptr);
    vkDestroyPipeline(device, pipelines.pbr, nullptr);
    vkDestroyPipeline(device, pipelines.pbrAlphaBlend, nullptr);

    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, dsLayoutScene, nullptr);
    vkDestroyDescriptorSetLayout(device, dsLayoutModels, nullptr);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    vulkanDevice->destroySemaphore (drawComplete);
    vulkanDevice->destroyFence (fence);

    for (int i=0; i<models2.size(); i++)
        models2[i].destroy();
    skybox.destroy();

    sharedUBOs.matrices.destroy();
    sharedUBOs.params.destroy();

    textures.environmentCube.destroy();
    textures.irradianceCube.destroy();
    textures.prefilteredCube.destroy();
    textures.lutBrdf.destroy();
}

void vkPbrRenderer::buildCommandBuffers()
{
    VkCommandBufferBeginInfo cmdBufferBeginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    VkClearValue clearValues[3];
    if (settings.multiSampling) {
        clearValues[0].color = { { 1.0f, 1.0f, 1.0f, 1.0f } };
        clearValues[1].color = { { 1.0f, 1.0f, 1.0f, 1.0f } };
        clearValues[2].depthStencil = { 1.0f, 0 };
    }
    else {
        clearValues[0].color = { { 0.1f, 0.1f, 0.1f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };
    }

    VkRenderPassBeginInfo renderPassBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;
    renderPassBeginInfo.framebuffer = frameBuffer;

    VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffer, &cmdBufferBeginInfo));
    vkCmdBeginRenderPass(drawCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(drawCmdBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = { width, height };
    vkCmdSetScissor(drawCmdBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(drawCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsScene, 0, NULL);

    vkCmdBindPipeline(drawCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);

    skybox.buildCommandBuffer(drawCmdBuffer, pipelineLayout);


    // Opaque primitives first
    vkCmdBindPipeline(drawCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbr);
    //for (m=0; m<models2.size(); m++)
    //    models2[m].buildCommandBuffer(drawCmdBuffers[i]);
    //models2[1].buildCommandBuffer(drawCmdBuffers[i], pipelineLayout);

    //vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrAlphaBlend);
    models2[0].buildCommandBuffer(drawCmdBuffer, pipelineLayout);

    // Transparent last
    // TODO: Correct depth sorting
    /*vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrAlphaBlend);
    for (auto primitive : model.primitives) {
        if (model.materials[primitive.material].alphaMode == vkglTF::ALPHAMODE_BLEND) {
            renderPrimitive(model, primitive, drawCmdBuffers[i]);
        }
    }*/

    vkCmdEndRenderPass(drawCmdBuffer);
    VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffer));
}

void vkPbrRenderer::loadAssets()
{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    const std::string assetpath = "";
#else
    const std::string assetpath = "data/";
    struct stat info;
    if (stat(assetpath.c_str(), &info) != 0) {
        std::string msg = "Could not locate asset path in \"" + assetpath + "\".\nMake sure binary is run from correct relative directory!";
        std::cerr << msg << std::endl;
#if defined(_WIN32)
        MessageBox(NULL, msg.c_str(), "Fatal error", MB_OK | MB_ICONERROR);
#endif
        exit(-1);
    }
#endif

//    std::vector<std::string> cubeMaps = {
////        "data/textures/st-peters/posx.jpg",
////        "data/textures/st-peters/negx.jpg",
////        "data/textures/st-peters/negy.jpg",
////        "data/textures/st-peters/posy.jpg",
////        "data/textures/st-peters/posz.jpg",
////        "data/textures/st-peters/negz.jpg",
//        "data/textures/grace/px.png",
//        "data/textures/grace/nx.png",
//        "data/textures/grace/ny.png",
//        "data/textures/grace/py.png",
//        "data/textures/grace/pz.png",
//        "data/textures/grace/nz.png",
//    };
//    textures.environmentCube.buildFromImages (cubeMaps, 2048, VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
    textures.environmentCube.loadFromFile(assetpath + "textures/papermill_hdr16f_cube.ktx", VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
    skybox.loadFromFile(assetpath + "models/Box/glTF-Embedded/Box.gltf", vulkanDevice, queue);
}

void vkPbrRenderer::setupDescriptors()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 6 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 }
    };
    VkDescriptorPoolCreateInfo descriptorPoolCI = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolCI.poolSizeCount = 2;
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = 4;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

    // Scene (matrices and environment maps)
    {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },//matrices
            { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },//params
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },//irradiance
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },//prefiltered cube
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },//lutBrdf
        };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptorSetLayoutCI.pBindings     = setLayoutBindings.data();
        descriptorSetLayoutCI.bindingCount  = static_cast<uint32_t>(setLayoutBindings.size());
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &dsLayoutScene));

        VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        descriptorSetAllocInfo.descriptorPool       = descriptorPool;
        descriptorSetAllocInfo.pSetLayouts          = &dsLayoutScene;
        descriptorSetAllocInfo.descriptorSetCount   = 1;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &dsScene));

        std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{
            createWriteDS (dsScene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &sharedUBOs.matrices.descriptor),
            createWriteDS (dsScene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &sharedUBOs.params.descriptor),
            createWriteDS (dsScene, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.irradianceCube.descriptor),
            createWriteDS (dsScene, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &textures.prefilteredCube.descriptor),
            createWriteDS (dsScene, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, &textures.lutBrdf.descriptor),
        };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }

    // models (samplers)
    {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },//texture sampler
            { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },//material ubo
        };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
        descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &dsLayoutModels));
    }
}

void vkPbrRenderer::preparePipelines()
{
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCI = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencilStateCI.depthTestEnable = VK_FALSE;
    depthStencilStateCI.depthWriteEnable = VK_FALSE;
    depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilStateCI.front = depthStencilStateCI.back;
    depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo viewportStateCI = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampleStateCI = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};

    if (settings.multiSampling)
        multisampleStateCI.rasterizationSamples = settings.sampleCount;

    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateCI = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    // Pipeline layout
    std::array<VkDescriptorSetLayout, 2> setLayouts = {
        dsLayoutScene, dsLayoutModels
    };
    VkPipelineLayoutCreateInfo pipelineLayoutCI = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutCI.setLayoutCount = 2;
    pipelineLayoutCI.pSetLayouts = setLayouts.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

    // Vertex bindings an attributes
    std::vector<VkVertexInputBindingDescription> vertexInputBinding = {
        {0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(vkglTF::InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6 },
        //instance
        { 3, 1, VK_FORMAT_R32_UINT, 0 },                //material index
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,  4},     //model matrix
        { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 20},
        { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 36},
        { 7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 52},
        { 8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 68}, //color for selection
    };
    VkPipelineVertexInputStateCreateInfo vertexInputStateCI = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputStateCI.vertexBindingDescriptionCount = 2;
    vertexInputStateCI.pVertexBindingDescriptions = vertexInputBinding.data();
    vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
    vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

    // Pipelines
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.layout = pipelineLayout;
    pipelineCI.renderPass = renderPass;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = &vertexInputStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    // Skybox pipeline (background cube)
    rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
    vertexInputStateCI.vertexBindingDescriptionCount = 1;
    vertexInputStateCI.vertexAttributeDescriptionCount = 3;

    shaderStages = {
        loadShader(device, "skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(device, "skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));

    for (auto shaderStage : shaderStages)
        vkDestroyShaderModule(device, shaderStage.module, nullptr);

    // PBR pipeline
    rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
    vertexInputStateCI.vertexBindingDescriptionCount = 2;
    vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
    depthStencilStateCI.depthWriteEnable = VK_TRUE;
    depthStencilStateCI.depthTestEnable = VK_TRUE;

    shaderStages = {
        loadShader(device, "pbr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(device, "pbr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbr));

    depthStencilStateCI.depthWriteEnable = VK_FALSE;
    depthStencilStateCI.depthTestEnable = VK_FALSE;

    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    blendAttachmentState.blendEnable = VK_TRUE;
    blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbrAlphaBlend));

    for (auto shaderStage : shaderStages)
        vkDestroyShaderModule(device, shaderStage.module, nullptr);
}

/*
    Generate a BRDF integration map storing roughness/NdotV as a look-up-table
*/
void vkPbrRenderer::generateBRDFLUT()
{
    auto tStart = std::chrono::high_resolution_clock::now();

    const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
    const int32_t dim = 512;

    // Image
    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = format;
    imageCI.extent.width = dim;
    imageCI.extent.height = dim;
    imageCI.extent.depth = 1;
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &textures.lutBrdf.image));
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, textures.lutBrdf.image, &memReqs);
    VkMemoryAllocateInfo memAllocInfo{};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &textures.lutBrdf.deviceMemory));
    VK_CHECK_RESULT(vkBindImageMemory(device, textures.lutBrdf.image, textures.lutBrdf.deviceMemory, 0));

    // View
    VkImageViewCreateInfo viewCI{};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = format;
    viewCI.subresourceRange = {};
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.layerCount = 1;
    viewCI.image = textures.lutBrdf.image;
    VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &textures.lutBrdf.view));

    // Sampler
    VkSamplerCreateInfo samplerCI{};
    samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 1.0f;
    samplerCI.maxAnisotropy = 1.0f;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &textures.lutBrdf.sampler));

    // FB, Att, RP, Pipe, etc.
    VkAttachmentDescription attDesc{};
    // Color attachment
    attDesc.format = format;
    attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpassDescription{};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;

    // Use subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Create the actual renderpass
    VkRenderPassCreateInfo renderPassCI{};
    renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCI.attachmentCount = 1;
    renderPassCI.pAttachments = &attDesc;
    renderPassCI.subpassCount = 1;
    renderPassCI.pSubpasses = &subpassDescription;
    renderPassCI.dependencyCount = 2;
    renderPassCI.pDependencies = dependencies.data();

    VkRenderPass renderpass;
    VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

    VkFramebufferCreateInfo framebufferCI{};
    framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferCI.renderPass = renderpass;
    framebufferCI.attachmentCount = 1;
    framebufferCI.pAttachments = &textures.lutBrdf.view;
    framebufferCI.width = dim;
    framebufferCI.height = dim;
    framebufferCI.layers = 1;

    VkFramebuffer framebuffer;
    VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &framebuffer));

    // Desriptors
    VkDescriptorSetLayout descriptorsetlayout;
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

    // Pipeline layout
    VkPipelineLayout pipelinelayout;
    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

    // Pipeline
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
    inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
    rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
    colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
    depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilStateCI.depthTestEnable = VK_FALSE;
    depthStencilStateCI.depthWriteEnable = VK_FALSE;
    depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilStateCI.front = depthStencilStateCI.back;
    depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo viewportStateCI{};
    viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
    multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
    emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.layout = pipelinelayout;
    pipelineCI.renderPass = renderpass;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = &emptyInputStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = shaderStages.data();

    // Look-up-table (from BRDF) pipeline
    shaderStages = {
        loadShader(device, "genbrdflut.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(device, "genbrdflut.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
    for (auto shaderStage : shaderStages) {
        vkDestroyShaderModule(device, shaderStage.module, nullptr);
    }

    // Render
    VkClearValue clearValues[1];
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderpass;
    renderPassBeginInfo.renderArea.extent.width = dim;
    renderPassBeginInfo.renderArea.extent.height = dim;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clearValues;
    renderPassBeginInfo.framebuffer = framebuffer;

    VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = (float)dim;
    viewport.height = (float)dim;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent.width = width;
    scissor.extent.height = height;

    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmdBuf, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmdBuf);
    vulkanDevice->flushCommandBuffer(cmdBuf, queue);

    vkQueueWaitIdle(queue);

    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
    vkDestroyRenderPass(device, renderpass, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);

    textures.lutBrdf.descriptor.imageView = textures.lutBrdf.view;
    textures.lutBrdf.descriptor.sampler = textures.lutBrdf.sampler;
    textures.lutBrdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    textures.lutBrdf.device = vulkanDevice;

    auto tEnd = std::chrono::high_resolution_clock::now();
    auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    std::cout << "Generating BRDF LUT took " << tDiff << " ms" << std::endl;
}

/*
    Offline generation for the cube maps used for PBR lighting
    - Irradiance cube map
    - Pre-filterd environment cubemap
*/
void vkPbrRenderer::generateCubemaps()
{
    enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };

    for (uint32_t target = 0; target < PREFILTEREDENV + 1; target++) {

        vks::TextureCubeMap cubemap;

        auto tStart = std::chrono::high_resolution_clock::now();

        VkFormat format;
        int32_t dim;

        switch (target) {
        case IRRADIANCE:
            format = VK_FORMAT_R32G32B32A32_SFLOAT;
            dim = 64;
            break;
        case PREFILTEREDENV:
            format = VK_FORMAT_R16G16B16A16_SFLOAT;
            dim = 512;
            break;
        };

        const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

        // Create target cubemap
        {
            // Image
            VkImageCreateInfo imageCI{};
            imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCI.imageType = VK_IMAGE_TYPE_2D;
            imageCI.format = format;
            imageCI.extent.width = dim;
            imageCI.extent.height = dim;
            imageCI.extent.depth = 1;
            imageCI.mipLevels = numMips;
            imageCI.arrayLayers = 6;
            imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &cubemap.image));
            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(device, cubemap.image, &memReqs);
            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAllocInfo.allocationSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &cubemap.deviceMemory));
            VK_CHECK_RESULT(vkBindImageMemory(device, cubemap.image, cubemap.deviceMemory, 0));

            // View
            VkImageViewCreateInfo viewCI{};
            viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            viewCI.format = format;
            viewCI.subresourceRange = {};
            viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCI.subresourceRange.levelCount = numMips;
            viewCI.subresourceRange.layerCount = 6;
            viewCI.image = cubemap.image;
            VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &cubemap.view));

            // Sampler
            VkSamplerCreateInfo samplerCI{};
            samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCI.magFilter = VK_FILTER_LINEAR;
            samplerCI.minFilter = VK_FILTER_LINEAR;
            samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCI.minLod = 0.0f;
            samplerCI.maxLod = static_cast<float>(numMips);
            samplerCI.maxAnisotropy = 1.0f;
            samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &cubemap.sampler));
        }

        // FB, Att, RP, Pipe, etc.
        VkAttachmentDescription attDesc{};
        // Color attachment
        attDesc.format = format;
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorReference;

        // Use subpass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // Renderpass
        VkRenderPassCreateInfo renderPassCI{};
        renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCI.attachmentCount = 1;
        renderPassCI.pAttachments = &attDesc;
        renderPassCI.subpassCount = 1;
        renderPassCI.pSubpasses = &subpassDescription;
        renderPassCI.dependencyCount = 2;
        renderPassCI.pDependencies = dependencies.data();
        VkRenderPass renderpass;
        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

        struct Offscreen {
            VkImage         image;
            VkImageView     view;
            VkDeviceMemory  memory;
            VkFramebuffer   framebuffer;
        } offscreen;

        // Create offscreen framebuffer
        {
            // Image
            VkImageCreateInfo imageCI{};
            imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCI.imageType = VK_IMAGE_TYPE_2D;
            imageCI.format = format;
            imageCI.extent.width = dim;
            imageCI.extent.height = dim;
            imageCI.extent.depth = 1;
            imageCI.mipLevels = 1;
            imageCI.arrayLayers = 1;
            imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &offscreen.image));
            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(device, offscreen.image, &memReqs);
            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAllocInfo.allocationSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &offscreen.memory));
            VK_CHECK_RESULT(vkBindImageMemory(device, offscreen.image, offscreen.memory, 0));

            // View
            VkImageViewCreateInfo viewCI{};
            viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCI.format = format;
            viewCI.flags = 0;
            viewCI.subresourceRange = {};
            viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCI.subresourceRange.baseMipLevel = 0;
            viewCI.subresourceRange.levelCount = 1;
            viewCI.subresourceRange.baseArrayLayer = 0;
            viewCI.subresourceRange.layerCount = 1;
            viewCI.image = offscreen.image;
            VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &offscreen.view));

            // Framebuffer
            VkFramebufferCreateInfo framebufferCI{};
            framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCI.renderPass = renderpass;
            framebufferCI.attachmentCount = 1;
            framebufferCI.pAttachments = &offscreen.view;
            framebufferCI.width = dim;
            framebufferCI.height = dim;
            framebufferCI.layers = 1;
            VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &offscreen.framebuffer));

            VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.image = offscreen.image;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = 0;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier(layoutCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
            vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);
        }

        // Descriptors
        VkDescriptorSetLayout descriptorsetlayout;
        VkDescriptorSetLayoutBinding setLayoutBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
        descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCI.pBindings = &setLayoutBinding;
        descriptorSetLayoutCI.bindingCount = 1;
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

        // Descriptor Pool
        VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo descriptorPoolCI{};
        descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCI.poolSizeCount = 1;
        descriptorPoolCI.pPoolSizes = &poolSize;
        descriptorPoolCI.maxSets = 2;
        VkDescriptorPool descriptorpool;
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorpool));

        // Descriptor sets
        VkDescriptorSet descriptorset;
        VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
        descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocInfo.descriptorPool = descriptorpool;
        descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
        descriptorSetAllocInfo.descriptorSetCount = 1;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorset));
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorset;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pImageInfo = &textures.environmentCube.descriptor;
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

        struct PushBlockIrradiance {
            glm::mat4 mvp;
            float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
            float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
        } pushBlockIrradiance;

        struct PushBlockPrefilterEnv {
            glm::mat4 mvp;
            float roughness;
            uint32_t numSamples = 32u;
        } pushBlockPrefilterEnv;

        // Pipeline layout
        VkPipelineLayout pipelinelayout;
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        switch (target) {
            case IRRADIANCE:
                pushConstantRange.size = sizeof(PushBlockIrradiance);
                break;
            case PREFILTEREDENV:
                pushConstantRange.size = sizeof(PushBlockPrefilterEnv);
                break;
        };

        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

        // Pipeline
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
        inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
        rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
        rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationStateCI.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachmentState.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
        colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendStateCI.attachmentCount = 1;
        colorBlendStateCI.pAttachments = &blendAttachmentState;

        VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
        depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilStateCI.depthTestEnable = VK_FALSE;
        depthStencilStateCI.depthWriteEnable = VK_FALSE;
        depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilStateCI.front = depthStencilStateCI.back;
        depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineViewportStateCreateInfo viewportStateCI{};
        viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCI.viewportCount = 1;
        viewportStateCI.scissorCount = 1;

        VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
        multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicStateCI{};
        dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
        dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

        // Vertex input state
        VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription vertexInputAttribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

        VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
        vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputStateCI.vertexBindingDescriptionCount = 1;
        vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputStateCI.vertexAttributeDescriptionCount = 1;
        vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCI{};
        pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCI.layout = pipelinelayout;
        pipelineCI.renderPass = renderpass;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pVertexInputState = &vertexInputStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;
        pipelineCI.stageCount = 2;
        pipelineCI.pStages = shaderStages.data();
        pipelineCI.renderPass = renderpass;

        shaderStages[0] = loadShader(device, "filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        switch (target) {
            case IRRADIANCE:
                shaderStages[1] = loadShader(device, "irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
                break;
            case PREFILTEREDENV:
                shaderStages[1] = loadShader(device, "prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
                break;
        };
        VkPipeline pipeline;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
        for (auto shaderStage : shaderStages) {
            vkDestroyShaderModule(device, shaderStage.module, nullptr);
        }

        // Render cubemap
        VkClearValue clearValues[1];
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

        VkRenderPassBeginInfo renderPassBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassBeginInfo.renderPass = renderpass;
        renderPassBeginInfo.framebuffer = offscreen.framebuffer;
        renderPassBeginInfo.renderArea.extent.width = dim;
        renderPassBeginInfo.renderArea.extent.height = dim;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = clearValues;

        std::vector<glm::mat4> matrices = {
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        VkViewport  viewport = {0, 0, (float)dim, (float)dim, 0.0f, 1.0f};
        VkRect2D    scissor  = {{}, {width, height}};

        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, numMips,0 , 6};

        // Change image layout for all cubemap faces to transfer destination
        {
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.image = cubemap.image;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = 0;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.subresourceRange = subresourceRange;
            vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
        }

        for (uint32_t m = 0; m < numMips; m++) {
            for (uint32_t f = 0; f < 6; f++) {
                viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
                viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
                vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

                // Render scene from cube face's point of view
                vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Pass parameters for current pass using a push constant block
                switch (target) {
                    case IRRADIANCE:
                        pushBlockIrradiance.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
                        vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
                        break;
                    case PREFILTEREDENV:
                        pushBlockPrefilterEnv.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
                        pushBlockPrefilterEnv.roughness = (float)m / (float)(numMips - 1);
                        vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
                        break;
                };

                vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

                skybox.buildCommandBuffer(cmdBuf, pipelinelayout);

                vkCmdEndRenderPass(cmdBuf);

                {
                    VkImageMemoryBarrier imageMemoryBarrier{};
                    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imageMemoryBarrier.image = offscreen.image;
                    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                }

                // Copy region for transfer from framebuffer to cube face
                VkImageCopy copyRegion{};

                copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.srcSubresource.baseArrayLayer = 0;
                copyRegion.srcSubresource.mipLevel = 0;
                copyRegion.srcSubresource.layerCount = 1;
                copyRegion.srcOffset = { 0, 0, 0 };

                copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.dstSubresource.baseArrayLayer = f;
                copyRegion.dstSubresource.mipLevel = m;
                copyRegion.dstSubresource.layerCount = 1;
                copyRegion.dstOffset = { 0, 0, 0 };

                copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
                copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
                copyRegion.extent.depth = 1;

                vkCmdCopyImage(
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    cubemap.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &copyRegion);

                {
                    VkImageMemoryBarrier imageMemoryBarrier{};
                    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imageMemoryBarrier.image = offscreen.image;
                    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                }

            }
        }

        {
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.image = cubemap.image;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.subresourceRange = subresourceRange;
            vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
        }

        vulkanDevice->flushCommandBuffer(cmdBuf, queue);

        vkDestroyRenderPass(device, renderpass, nullptr);
        vkDestroyFramebuffer(device, offscreen.framebuffer, nullptr);
        vkFreeMemory(device, offscreen.memory, nullptr);
        vkDestroyImageView(device, offscreen.view, nullptr);
        vkDestroyImage(device, offscreen.image, nullptr);
        vkDestroyDescriptorPool(device, descriptorpool, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelinelayout, nullptr);

        cubemap.descriptor.imageView = cubemap.view;
        cubemap.descriptor.sampler = cubemap.sampler;
        cubemap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cubemap.device = vulkanDevice;

        switch (target) {
            case IRRADIANCE:
                textures.irradianceCube = cubemap;
                break;
            case PREFILTEREDENV:
                textures.prefilteredCube = cubemap;
                lightingParams.prefilteredCubeMipLevels = numMips;
                break;
        };

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        std::cout << "Generating cube map with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
    }
}

/*
    Prepare and initialize uniform buffer containing shader uniforms
*/
void vkPbrRenderer::prepareUniformBuffers()
{
    // Objact vertex shader uniform buffer
    VK_CHECK_RESULT(vulkanDevice->createBuffer(
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &sharedUBOs.matrices,
        sizeof(mvpMatrices)));

    // Skybox vertex shader uniform buffer
//    VK_CHECK_RESULT(vulkanDevice->createBuffer(
//        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
//        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//        &sharedUBOs.skybox,
//        sizeof(mvpMatrices)));

    // Shared parameter uniform buffer
    VK_CHECK_RESULT(vulkanDevice->createBuffer(
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &sharedUBOs.params,
        sizeof(lightingParams)));

    // Map persistent
    //sharedUBOs.skybox.map();
    sharedUBOs.matrices.map();
    sharedUBOs.params.map();

    updateUniformBuffers();
    updateParams();
}

void vkPbrRenderer::updateUniformBuffers()
{
    // 3D object
    mvpMatrices.projection = camera.matrices.perspective;
    mvpMatrices.view = camera.matrices.view;
    mvpMatrices.view3 = glm::mat4(glm::mat3(camera.matrices.view));
    mvpMatrices.camPos = camera.position * -1.0f;
    sharedUBOs.matrices.copyTo (&mvpMatrices, sizeof(mvpMatrices));
}

void vkPbrRenderer::updateParams()
{
    lightingParams.lightDir = glm::vec4(-10.0f, 150.f, -10.f, 1.0f);
    sharedUBOs.params.copyTo(&lightingParams, sizeof(lightingParams));
}

void vkPbrRenderer::prepare()
{
    VulkanExampleBase::prepare();


    fence = vulkanDevice->createFence(true);

    drawComplete= vulkanDevice->createSemaphore();

    submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.pWaitDstStageMask = &stageFlags;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.commandBufferCount = 1;
    submitInfo.pSignalSemaphores = &drawComplete;

    loadAssets();

    generateBRDFLUT();
    generateCubemaps();
    prepareUniformBuffers();

    setupDescriptors();

    for (int i=0; i<models2.size(); i++) {
        models2[i].buildInstanceBuffer();
        models2[i].allocateDescriptorSet (descriptorPool,dsLayoutModels);
    }

    preparePipelines();
    buildCommandBuffers();

    prepared = true;
}

void vkPbrRenderer::submit (VkQueue queue, VkSemaphore* waitSemaphore, uint32_t waitSemaphoreCount) {
    if (!prepared)
        return;
    submitInfo.pCommandBuffers		= &drawCmdBuffer;
    submitInfo.waitSemaphoreCount	= waitSemaphoreCount;
    submitInfo.pWaitSemaphores		= waitSemaphore;

    VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    VK_CHECK_RESULT(vkResetFences(device, 1, &fence));

    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
}

void vkPbrRenderer::viewChanged()
{
    updateUniformBuffers();
}

#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
void vkPbrRenderer::keyPressed(uint32_t key)
{
    switch (key) {
        case KEY_F1:
            if (lightingParams.exposure > 0.1f) {
                lightingParams.exposure -= 0.1f;
                updateParams();
                std::cout << "Exposure: " << lightingParams.exposure << std::endl;
            }
            break;
        case KEY_F2:
            if (lightingParams.exposure < 10.0f) {
                lightingParams.exposure += 0.1f;
                updateParams();
                std::cout << "Exposure: " << lightingParams.exposure << std::endl;
            }
            break;
        case KEY_F3:
            if (lightingParams.gamma > 0.1f) {
                lightingParams.gamma -= 0.1f;
                updateParams();
                std::cout << "Gamma: " << lightingParams.gamma << std::endl;
            }
            break;
        case KEY_F4:
            if (lightingParams.gamma < 10.0f) {
                lightingParams.gamma += 0.1f;
                updateParams();
                std::cout << "Gamma: " << lightingParams.gamma << std::endl;
            }
            break;
    }
}
#endif
