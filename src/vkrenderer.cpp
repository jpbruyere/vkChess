#include "vkrenderer.h"
#include "pbrrenderer2.h"

const VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

vkRenderer::vkRenderer () {}

void vkRenderer::create(vks::VulkanDevice* _device, VulkanSwapChain *_swapChain,
                   VkFormat _depthFormat, VkSampleCountFlagBits _sampleCount,
                   VulkanExampleBase::UniformBuffers& _sharedUbos) {
    swapChain   = _swapChain;
    device      = _device;
    depthFormat = _depthFormat;
    sampleCount = _sampleCount;
    sharedUBOs = _sharedUbos;

    prepare();
}
vkRenderer::~vkRenderer()
{
    if (prepared)
        destroy();
}

void vkRenderer::destroy() {
    prepared = false;

    freeRessources();

    for (uint32_t i = 0; i < frameBuffers.size(); i++)
        vkDestroyFramebuffer(device->dev, frameBuffers[i], nullptr);

    vkFreeCommandBuffers(device->dev, commandPool, cmdBuffers.size(), cmdBuffers.data());

    for (uint32_t i = 0; i < fences.size(); i++)
        device->destroyFence(fences[i]);

    device->destroySemaphore(drawComplete);

    delete shadingCtx;

    vkDestroyRenderPass     (device->dev, renderPass, VK_NULL_HANDLE);
    vkDestroyPipeline       (device->dev, pipeline, VK_NULL_HANDLE);
    vkDestroyPipelineLayout (device->dev, pipelineLayout, VK_NULL_HANDLE);
    vkDestroyCommandPool    (device->dev, commandPool, VK_NULL_HANDLE);
}
void vkRenderer::prepare() {
    fences.resize(swapChain->imageCount);
    for (uint i=0; i<swapChain->imageCount; i++)
        fences[i] = device->createFence(true);

    drawComplete= device->createSemaphore();

    submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.pWaitDstStageMask    = &stageFlags;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pSignalSemaphores    = &drawComplete;

    prepareRenderPass();

    VkCommandPoolCreateInfo cmdPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmdPoolInfo.queueFamilyIndex = device->queueFamilyIndices.graphics;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK_RESULT(vkCreateCommandPool(device->dev, &cmdPoolInfo, nullptr, &commandPool));

    cmdBuffers.resize(frameBuffers.size());

    VkCommandBufferAllocateInfo cmdBufAllocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdBufAllocateInfo.commandPool          = commandPool;
    cmdBufAllocateInfo.level                = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocateInfo.commandBufferCount   = frameBuffers.size();
    VK_CHECK_RESULT(vkAllocateCommandBuffers(device->dev, &cmdBufAllocateInfo, cmdBuffers.data()));

    configurePipelineLayout();
    loadRessources();
    prepareDescriptors();
    preparePipeline ();

    prepared = true;
}

void vkRenderer::prepareRenderPass()
{
    VkAttachmentDescription attachments[] = {
        {0, //color
            swapChain->colorFormat, sampleCount,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        },{0,//depth
           depthFormat, sampleCount,
           VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
           VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        },{0,// resolve
           swapChain->colorFormat, VK_SAMPLE_COUNT_1_BIT,
           VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
           VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        }
    };

    VkAttachmentReference colorReference    = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference    = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolveReference  = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    if (sampleCount > VK_SAMPLE_COUNT_1_BIT)
        subpass.pResolveAttachments     = &resolveReference;


    VkSubpassDependency dependencies[] =
    {
//        { VK_SUBPASS_EXTERNAL, VK_SUBPASS_EXTERNAL,
//          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
//          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
//          VK_DEPENDENCY_BY_REGION_BIT}

        { VK_SUBPASS_EXTERNAL, 0,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          VK_DEPENDENCY_BY_REGION_BIT},
        { 0, VK_SUBPASS_EXTERNAL,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
          VK_DEPENDENCY_BY_REGION_BIT},
    };

    VkRenderPassCreateInfo renderPassCI = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassCI.attachmentCount    = sampleCount > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    renderPassCI.pAttachments       = attachments;
    renderPassCI.subpassCount       = 1;
    renderPassCI.pSubpasses         = &subpass;
    renderPassCI.dependencyCount    = 2;
    renderPassCI.pDependencies      = dependencies;
    VK_CHECK_RESULT(vkCreateRenderPass(device->dev, &renderPassCI, nullptr, &renderPass));

    prepareFrameBuffer();
}
void vkRenderer::prepareFrameBuffer () {
    VkImageView attachments[] = {
        swapChain->multisampleTarget->color.view,
        swapChain->multisampleTarget->depth.view,
        VK_NULL_HANDLE
    };

    VkFramebufferCreateInfo frameBufferCI = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    frameBufferCI.renderPass        = renderPass;
    frameBufferCI.attachmentCount   = (sampleCount > VK_SAMPLE_COUNT_1_BIT) ? 3 : 2;
    frameBufferCI.pAttachments      = attachments;
    frameBufferCI.width             = swapChain->extent.width;
    frameBufferCI.height            = swapChain->extent.height;
    frameBufferCI.layers            = 1;

    frameBuffers.resize(swapChain->imageCount);

    for (uint32_t i = 0; i < frameBuffers.size(); i++) {
        attachments[2] = swapChain->buffers[i].view;
        VK_CHECK_RESULT(vkCreateFramebuffer(device->dev, &frameBufferCI, nullptr, &frameBuffers[i]));
    }
}

void vkRenderer::configurePipelineLayout () {
    shadingCtx = new vks::ShadingContext (device);

    shadingCtx->addDescriptorSetLayout({{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr }});

    shadingCtx->prepare();

}
//place here specific renderer resources
void vkRenderer::loadRessources() {
    vertexBuff.create (device,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vBufferSize);

    vertexBuff.map();
}
//and free here
void vkRenderer::freeRessources() {
    vertexBuff.unmap();
    vertexBuff.destroy();
}
void vkRenderer::prepareDescriptors()
{
    descriptorSet = shadingCtx->allocateDescriptorSet(0);

    shadingCtx->updateDescriptorSet (descriptorSet,{{0,0,&sharedUBOs.matrices}});
}
void vkRenderer::preparePipeline()
{
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 5.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState = {};
    blendAttachmentState.blendEnable = VK_TRUE;
    blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

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

    if (sampleCount > VK_SAMPLE_COUNT_1_BIT)
        multisampleStateCI.rasterizationSamples = sampleCount;

    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateCI = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    VkPipelineLayoutCreateInfo pipelineLayoutCI = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutCI.setLayoutCount = shadingCtx->layouts.size();
    pipelineLayoutCI.pSetLayouts    = shadingCtx->layouts.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device->dev, &pipelineLayoutCI, nullptr, &pipelineLayout));

    // Vertex bindings an attributes
    VkVertexInputBindingDescription vertexInputBinding =
        {0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};

    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},					// Position
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3},	// color
    };

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputStateCI.vertexBindingDescriptionCount = 1;
    vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
    vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
    vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

    // Pipelines
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkGraphicsPipelineCreateInfo pipelineCI = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
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

    shaderStages = {
        loadShader(device->dev, "debugDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(device->dev, "debugDraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device->dev, device->pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

    for (auto shaderStage : shaderStages)
        vkDestroyShaderModule(device->dev, shaderStage.module, nullptr);
}
void vkRenderer::draw(VkCommandBuffer cmdBuff) {
    VkDeviceSize offsets[1] = { 0 };

    vkCmdBindPipeline (cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdBindVertexBuffers (cmdBuff, 0, 1, &vertexBuff.buffer, offsets);
    vkCmdDraw (cmdBuff,  vertexCount, 1, 0, 0);
}
void vkRenderer::buildCommandBuffer (){
    prepared = false;

    VkCommandBufferBeginInfo cmdBufInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    VkClearValue clearValues[] = {
        {{ 1.0f, 1.0f, 1.0f, 1.0f }},
        { 1.0f, 0 },
        {{ 1.0f, 1.0f, 1.0f, 1.0f }},
    };

    VkRenderPassBeginInfo renderPassBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent = swapChain->extent;
    renderPassBeginInfo.clearValueCount = (sampleCount > VK_SAMPLE_COUNT_1_BIT) ? 3 : 2;
    renderPassBeginInfo.pClearValues = clearValues;

    for (size_t i = 0; i < cmdBuffers.size(); ++i)
    {
        renderPassBeginInfo.framebuffer = frameBuffers[i];
        VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffers[i], &cmdBufInfo));

        vkCmdBeginRenderPass(cmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = (float)swapChain->extent.width;
        viewport.height = (float)swapChain->extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffers[i], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = { swapChain->extent.width, swapChain->extent.height};
        vkCmdSetScissor(cmdBuffers[i], 0, 1, &scissor);

        draw (cmdBuffers[i]);
        vkCmdEndRenderPass(cmdBuffers[i]);

        VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffers[i]));
    }
    prepared = true;
}

void vkRenderer::submit (VkQueue queue, VkSemaphore* waitSemaphore, uint32_t waitSemaphoreCount) {
    if (!prepared)
        return;
    submitInfo.pCommandBuffers		= &cmdBuffers[swapChain->currentBuffer];
    submitInfo.waitSemaphoreCount	= waitSemaphoreCount;
    submitInfo.pWaitSemaphores		= waitSemaphore;

    VK_CHECK_RESULT(vkWaitForFences(device->dev, 1, &fences[swapChain->currentBuffer], VK_TRUE, DRAW_FENCE_TIMEOUT));
    VK_CHECK_RESULT(vkResetFences(device->dev, 1, &fences[swapChain->currentBuffer]));

    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fences[swapChain->currentBuffer]));
}
void vkRenderer::clear(){
    vertices.clear();
    vertexCount = 0;
}

void vkRenderer::flush(){
    VK_CHECK_RESULT(vkWaitForFences(device->dev, 1, &fences[swapChain->currentBuffer], VK_TRUE, DRAW_FENCE_TIMEOUT));
    memcpy(vertexBuff.mapped, vertices.data(), vertices.size() * sizeof(float));
    vertexCount = vertices.size() / 6;
    buildCommandBuffer();
}

void vkRenderer::drawLine(const glm::vec3& from, const glm::vec3& to,
                          const glm::vec3& fromColor, const glm::vec3& toColor)
{
    vertices.push_back(from.x);
    vertices.push_back(from.y);
    vertices.push_back(from.z);

    vertices.push_back(fromColor.x);
    vertices.push_back(fromColor.y);
    vertices.push_back(fromColor.z);

    vertices.push_back(to.x);
    vertices.push_back(to.y);
    vertices.push_back(to.z);

    vertices.push_back(toColor.x);
    vertices.push_back(toColor.y);
    vertices.push_back(toColor.z);
}

void vkRenderer::drawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color)
{
    drawLine(from,to,color,color);
}
