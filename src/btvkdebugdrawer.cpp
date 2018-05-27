#include "btvkdebugdrawer.h"

#define FENCE_TIME_OUT 100000000

const VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

btVKDebugDrawer::btVKDebugDrawer(vks::VulkanDevice* _device, VulkanSwapChain *_swapChain,
                                 VkFormat depthFormat, VkSampleCountFlagBits _sampleCount,
                                 std::vector<VkFramebuffer>&_frameBuffers, vks::Buffer* _uboMatrices,
                                 std::string fontFnt, vks::Texture& fontTexture)
:vkRenderer(_device, _swapChain, depthFormat, _sampleCount, _frameBuffers, _uboMatrices)
{
    m_debugMode = 0;

    fontChars = parsebmFont(fontFnt);
    texSDFFont = fontTexture;

    prepareDescriptors();
    preparePipeline ();
}

btVKDebugDrawer::~btVKDebugDrawer()
{
    if (prepared)
        destroy();
}
void btVKDebugDrawer::destroy() {
    vkRenderer::destroy();

    vkDestroyPipeline       (device->logicalDevice, pipelineSDFF, VK_NULL_HANDLE);
}

void btVKDebugDrawer::prepareDescriptors()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    };
    VkDescriptorPoolCreateInfo descriptorPoolCI = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolCI.poolSizeCount = 2;
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = 1;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorPool));

    // Descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    };
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
    descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

    VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descriptorSetAllocInfo.descriptorPool = descriptorPool;
    descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
    descriptorSetAllocInfo.descriptorSetCount = 1;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &descriptorSet));

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        {createWriteDS (descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uboMatrices->descriptor)},
        {createWriteDS (descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texSDFFont.descriptor)}
    };

    vkUpdateDescriptorSets(device->logicalDevice, 2, writeDescriptorSets.data(), 0, NULL);
}

void btVKDebugDrawer::preparePipeline () {
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 1.0f;

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
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device->logicalDevice, &pipelineLayoutCI, nullptr, &pipelineLayout));

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
        loadShader(device->logicalDevice, "debugDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(device->logicalDevice, "debugDraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device->logicalDevice, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));

    for (auto shaderStage : shaderStages)
        vkDestroyShaderModule(device->logicalDevice, shaderStage.module, nullptr);

    //SDFF pipeline

    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vertexInputBinding = {0, 5 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};

    vertexInputAttributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},					// Position
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3},	// color
    };

    shaderStages[0] = loadShader(device->logicalDevice, "sdf.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(device->logicalDevice, "sdf.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device->logicalDevice, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipelineSDFF));

    for (auto shaderStage : shaderStages)
        vkDestroyShaderModule(device->logicalDevice, shaderStage.module, nullptr);
}

void btVKDebugDrawer::buildCommandBuffer (){
    if (vertexCount == 0) {
        return;
    }

    prepared = false;

    VkCommandBufferBeginInfo cmdBufInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    VkClearValue clearValues[3];
    if (sampleCount > VK_SAMPLE_COUNT_1_BIT) {
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
    renderPassBeginInfo.renderArea.extent = swapChain->swapchainExtent;
    renderPassBeginInfo.clearValueCount = (sampleCount > VK_SAMPLE_COUNT_1_BIT) ? 3 : 2;
    renderPassBeginInfo.pClearValues = clearValues;

    for (size_t i = 0; i < cmdBuffers.size(); ++i)
    {
        renderPassBeginInfo.framebuffer = frameBuffers[i];
        VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffers[i], &cmdBufInfo));

        vkCmdBeginRenderPass(cmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = (float)swapChain->swapchainExtent.width;
        viewport.height = (float)swapChain->swapchainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffers[i], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = { swapChain->swapchainExtent.width, swapChain->swapchainExtent.height};
        vkCmdSetScissor(cmdBuffers[i], 0, 1, &scissor);

        VkDeviceSize offsets[1] = { 0 };

        vkCmdBindPipeline (cmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
        vkCmdBindVertexBuffers (cmdBuffers[i], 0, 1, &vertexBuff.buffer, offsets);
        vkCmdDraw (cmdBuffers[i],  vertexCount, 1, 0, 0);

        vkCmdBindPipeline (cmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineSDFF);
        offsets[0] = vertices.size() * sizeof(float);
        vkCmdBindVertexBuffers (cmdBuffers[i], 0, 1, &vertexBuff.buffer, offsets);
        vkCmdDraw (cmdBuffers[i],  sdffVertexCount, 1, 0, 0);

        vkCmdEndRenderPass(cmdBuffers[i]);

        VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffers[i]));
    }
    prepared = true;
}

void btVKDebugDrawer::clear(){
    vkRenderer::clear();
    sdffVertices.clear();
    sdffVertexCount = 0;
}

void btVKDebugDrawer::flush(){
    vkRenderer::flush();
    vertexCount = vertices.size() / 6;
    memcpy(vertexBuff.mapped + vertices.size() * sizeof(float), sdffVertices.data(), sdffVertices.size() * sizeof(float));
    sdffVertexCount = sdffVertices.size() / 5;
}


void btVKDebugDrawer::clearLines() {
    clear();
}
void btVKDebugDrawer::flushLines() {
    flush();
}
void btVKDebugDrawer::drawLine(const btVector3& from,const btVector3& to,const btVector3& fromColor, const btVector3& toColor)
{
    vertices.push_back(from.getX());
    vertices.push_back(from.getY());
    vertices.push_back(from.getZ());

    vertices.push_back(fromColor.getX());
    vertices.push_back(fromColor.getY());
    vertices.push_back(fromColor.getZ());

    vertices.push_back(to.getX());
    vertices.push_back(to.getY());
    vertices.push_back(to.getZ());

    vertices.push_back(toColor.getX());
    vertices.push_back(toColor.getY());
    vertices.push_back(toColor.getZ());
}

void btVKDebugDrawer::drawLine(const btVector3& from,const btVector3& to,const btVector3& color)
{
    drawLine(from,to,color,color);
}

void btVKDebugDrawer::drawSphere (const btVector3& p, btScalar radius, const btVector3& color)
{
//	glColor4f (color.getX(), color.getY(), color.getZ(), btScalar(1.0f));
//	glPushMatrix ();
//	glTranslatef (p.getX(), p.getY(), p.getZ());

//	int lats = 5;
//	int longs = 5;

//	int i, j;
//	for(i = 0; i <= lats; i++) {
//		btScalar lat0 = SIMD_PI * (-btScalar(0.5) + (btScalar) (i - 1) / lats);
//		btScalar z0  = radius*sin(lat0);
//		btScalar zr0 =  radius*cos(lat0);

//		btScalar lat1 = SIMD_PI * (-btScalar(0.5) + (btScalar) i / lats);
//		btScalar z1 = radius*sin(lat1);
//		btScalar zr1 = radius*cos(lat1);

//		glBegin(GL_QUAD_STRIP);
//		for(j = 0; j <= longs; j++) {
//			btScalar lng = 2 * SIMD_PI * (btScalar) (j - 1) / longs;
//			btScalar x = cos(lng);
//			btScalar y = sin(lng);

//			glNormal3f(x * zr0, y * zr0, z0);
//			glVertex3f(x * zr0, y * zr0, z0);
//			glNormal3f(x * zr1, y * zr1, z1);
//			glVertex3f(x * zr1, y * zr1, z1);
//		}
//		glEnd();
//	}

//	glPopMatrix();
}



void btVKDebugDrawer::drawTriangle(const btVector3& a,const btVector3& b,const btVector3& c,const btVector3& color,btScalar alpha)
{
    if (m_debugMode > 1)
    {
        drawLine(a,b,color);
        drawLine(b,c,color);
        drawLine(c,a,color);
    }
}

void btVKDebugDrawer::setDebugMode(int debugMode)
{
    m_debugMode = debugMode;

}

void btVKDebugDrawer::draw3dText(const btVector3& location, const char* textString)
{
    generateText (textString, location, 1.0f);

    //glRasterPos3f(location.x(),  location.y(),  location.z());
    //BMF_DrawString(BMF_GetFont(BMF_kHelvetica10),textString);

}

void btVKDebugDrawer::reportErrorWarning(const char* warningString)
{
    printf("%s\n",warningString);
}

void btVKDebugDrawer::drawContactPoint(const btVector3& pointOnB,const btVector3& normalOnB,btScalar distance,int lifeTime,const btVector3& color)
{
    drawLine(pointOnB,pointOnB+normalOnB*0.1,color);
}

void btVKDebugDrawer::sdffAddVertex (float posX,float posY,float posZ, float uvT, float uvU) {
    sdffVertices.push_back(posX);
    sdffVertices.push_back(posY);
    sdffVertices.push_back(posZ);

    sdffVertices.push_back(uvT);
    sdffVertices.push_back(uvU);
}
// Creates a vertex buffer containing quads for the passed text
void btVKDebugDrawer::generateText(const std::string& text, btVector3 pos, float scale)
{
    float w = texSDFFont.infos.extent.width;
    float cw = 36.0f;

    for (uint32_t i = 0; i < text.size(); i++)
    {
        bmchar *charInfo = &fontChars[(int)text[i]];

        if (charInfo->width == 0)
            charInfo->width = cw;

        float charw = ((float)(charInfo->width) / cw);
        float dimx = scale * charw;
        float charh = ((float)(charInfo->height) / cw);
        float dimy = scale * charh;
        float y = pos.getY() - (charh + (float)(charInfo->yoffset) / cw) * scale;// - charh;// * scale;

        float us = charInfo->x / w;
        float ue = (charInfo->x + charInfo->width) / w;
        float ts = (charInfo->y + charInfo->height) / w;
        float te = charInfo->y / w;

        float xo = charInfo->xoffset / cw;

        /*vertices.push_back({ { posx + dimx + xo,  posy + dimy, 0.0f }, { ue, te } });
        vertices.push_back({ { posx + xo,         posy + dimy, 0.0f }, { us, te } });
        vertices.push_back({ { posx + xo,         posy,        0.0f }, { us, ts } });
        vertices.push_back({ { posx + dimx + xo,  posy,        0.0f }, { ue, ts } });
        */
        //{ 0,1,2, 2,3,0 };
        sdffAddVertex (pos.getX() + dimx + xo,  y + dimy, pos.getZ(), ue, te);
        sdffAddVertex (pos.getX() + xo,         y + dimy, pos.getZ(), us, te);
        sdffAddVertex (pos.getX() + xo,         y,        pos.getZ(), us, ts);
        sdffAddVertex (pos.getX() + xo,         y,        pos.getZ(), us, ts);
        sdffAddVertex (pos.getX() + dimx + xo,  y,        pos.getZ(), ue, ts);
        sdffAddVertex (pos.getX() + dimx + xo,  y + dimy, pos.getZ(), ue, te);

        float advance = ((float)(charInfo->xadvance) / cw) * scale;
        pos.setX(pos.getX() + advance);
    }
}


