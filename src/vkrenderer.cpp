#include "vkrenderer.h"
//#include "pbrrenderer2.h"

const VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

vks::vkRenderer::vkRenderer () {}

void vks::vkRenderer::create(ptrVkDev _device, vks::RenderTarget *_renderTarget,
                   VkEngine::UniformBuffers& _sharedUbos) {
    renderTarget= _renderTarget;
    device      = _device;
    sharedUBOs = _sharedUbos;

    prepare();
}
vks::vkRenderer::~vkRenderer()
{
    if (prepared)
        destroy();
}

void vks::vkRenderer::destroy() {
    prepared = false;

    freeRessources();

    vkFreeCommandBuffers(device->dev, commandPool, cmdBuffers.size(), cmdBuffers.data());

    for (uint32_t i = 0; i < fences.size(); i++)
        device->destroyFence(fences[i]);

    device->destroySemaphore(drawComplete);

    delete shadingCtx;

    vkDestroyPipeline       (device->dev, pipeline, VK_NULL_HANDLE);
    vkDestroyPipelineLayout (device->dev, pipelineLayout, VK_NULL_HANDLE);
    vkDestroyCommandPool    (device->dev, commandPool, VK_NULL_HANDLE);
    pipeline        = VK_NULL_HANDLE;
    pipelineLayout  = VK_NULL_HANDLE;
    commandPool     = VK_NULL_HANDLE;
}
void vks::vkRenderer::prepare() {
    fences.resize (renderTarget->swapChain->imageCount);
    for (uint i=0; i<renderTarget->swapChain->imageCount; i++)
        fences[i] = device->createFence(true);

    drawComplete= device->createSemaphore();

    submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.pWaitDstStageMask    = &stageFlags;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pSignalSemaphores    = &drawComplete;

    prepareRendering();

    VkCommandPoolCreateInfo cmdPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmdPoolInfo.queueFamilyIndex = device->queueFamilyIndices.graphics;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK_RESULT(vkCreateCommandPool(device->dev, &cmdPoolInfo, nullptr, &commandPool));

    cmdBuffers.resize(renderTarget->frameBuffers.size());

    VkCommandBufferAllocateInfo cmdBufAllocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdBufAllocateInfo.commandPool          = commandPool;
    cmdBufAllocateInfo.level                = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocateInfo.commandBufferCount   = renderTarget->frameBuffers.size();
    VK_CHECK_RESULT(vkAllocateCommandBuffers(device->dev, &cmdBufAllocateInfo, cmdBuffers.data()));

    configurePipelineLayout();
    loadRessources();
    prepareDescriptors();
    preparePipeline ();

    prepared = true;
}

void vks::vkRenderer::prepareRendering()
{

}

void vks::vkRenderer::configurePipelineLayout () {
    shadingCtx = new vks::ShadingContext (device);

    shadingCtx->addDescriptorSetLayout({{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr }});

    shadingCtx->prepare();

}
//place here specific renderer resources
void vks::vkRenderer::loadRessources() {
    vertexBuff.create (device,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vBufferSize);

    vertexBuff.map();
}
//and free here
void vks::vkRenderer::freeRessources() {
    vertexBuff.unmap();
    vertexBuff.destroy();
}
void vks::vkRenderer::prepareDescriptors()
{
    descriptorSet = shadingCtx->allocateDescriptorSet(0);

    shadingCtx->updateDescriptorSet (descriptorSet,{{0,0,&sharedUBOs.matrices}});
}
void vks::vkRenderer::preparePipeline()
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

    if (renderTarget->samples > VK_SAMPLE_COUNT_1_BIT)
        multisampleStateCI.rasterizationSamples = renderTarget->samples;

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
    pipelineCI.renderPass = renderTarget->renderPass;
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
void vks::vkRenderer::draw(VkCommandBuffer cmdBuff) {
    VkDeviceSize offsets[1] = { 0 };

    vkCmdBindPipeline (cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdBindVertexBuffers (cmdBuff, 0, 1, &vertexBuff.buffer, offsets);
    vkCmdDraw (cmdBuff,  vertexCount, 1, 0, 0);
}
void vks::vkRenderer::rebuildCommandBuffer () {
    prepared = false;
    vkDeviceWaitIdle(device->dev);
    vkFreeCommandBuffers(device->dev, commandPool, cmdBuffers.size(), cmdBuffers.data());
    cmdBuffers.resize(renderTarget->frameBuffers.size());

    VkCommandBufferAllocateInfo cmdBufAllocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdBufAllocateInfo.commandPool          = commandPool;
    cmdBufAllocateInfo.level                = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocateInfo.commandBufferCount   = renderTarget->frameBuffers.size();
    VK_CHECK_RESULT(vkAllocateCommandBuffers(device->dev, &cmdBufAllocateInfo, cmdBuffers.data()));

    buildCommandBuffer();
    vkDeviceWaitIdle(device->dev);
}
void vks::vkRenderer::buildCommandBuffer (){
    prepared = false;

    VkCommandBufferBeginInfo cmdBufInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    VkClearValue clearValues[] = {
        {{ 1.0f, 1.0f, 1.0f, 1.0f }},
        { 1.0f, 0 },
        {{ 1.0f, 1.0f, 1.0f, 1.0f }},
    };

    VkRenderPassBeginInfo renderPassBeginInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = renderTarget->renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent = {renderTarget->width, renderTarget->height};
    renderPassBeginInfo.clearValueCount = (renderTarget->samples > VK_SAMPLE_COUNT_1_BIT) ? 3 : 2;
    renderPassBeginInfo.pClearValues = clearValues;

    for (size_t i = 0; i < cmdBuffers.size(); ++i)
    {
        renderPassBeginInfo.framebuffer = renderTarget->frameBuffers[i];
        VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffers[i], &cmdBufInfo));

        vkCmdBeginRenderPass(cmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = (float)renderTarget->width;
        viewport.height = (float)renderTarget->height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffers[i], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = {renderTarget->width, renderTarget->height};
        vkCmdSetScissor(cmdBuffers[i], 0, 1, &scissor);

        draw (cmdBuffers[i]);
        vkCmdEndRenderPass(cmdBuffers[i]);

        VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffers[i]));
    }
    prepared = true;
}

void vks::vkRenderer::submit (VkQueue queue, VkSemaphore* waitSemaphore, uint32_t waitSemaphoreCount) {
    if (!prepared)
        return;
    submitInfo.pCommandBuffers		= &cmdBuffers[renderTarget->swapChain->currentBuffer];
    submitInfo.waitSemaphoreCount	= waitSemaphoreCount;
    submitInfo.pWaitSemaphores		= waitSemaphore;

    VK_CHECK_RESULT(vkWaitForFences(device->dev, 1, &fences[renderTarget->swapChain->currentBuffer], VK_TRUE, DRAW_FENCE_TIMEOUT));
    VK_CHECK_RESULT(vkResetFences(device->dev, 1, &fences[renderTarget->swapChain->currentBuffer]));

    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fences[renderTarget->swapChain->currentBuffer]));
}
void vks::vkRenderer::clear(){
    vertices.clear();
    vertexCount = 0;
}

void vks::vkRenderer::flush(){
    VK_CHECK_RESULT(vkWaitForFences(device->dev, 1, &fences[renderTarget->swapChain->currentBuffer], VK_TRUE, DRAW_FENCE_TIMEOUT));
    memcpy(vertexBuff.mapped, vertices.data(), vertices.size() * sizeof(float));
    vertexCount = vertices.size() / 6;
    buildCommandBuffer();
}

void vks::vkRenderer::drawLine(const glm::vec3& from, const glm::vec3& to,
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

void vks::vkRenderer::drawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color)
{
    drawLine(from,to,color,color);
}
