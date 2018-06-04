#include "rendertarget.hpp"

#include "VkEngine.h"
#include "VulkanSwapChain.hpp"


vks::RenderTarget::RenderTarget(ptrSwapchain _swapChain, VkSampleCountFlagBits _samples) {
    swapChain = _swapChain;
    swapChain->boundRenderTargets.push_back (this);
    samples = _samples;

    createAttachments();
    createDefaultRenderPass();
    createFrameBuffers();
}

vks::RenderTarget::~RenderTarget () {
    swapChain->boundRenderTargets.erase(std::remove(swapChain->boundRenderTargets.begin(),
                                                    swapChain->boundRenderTargets.end(), this), swapChain->boundRenderTargets.end());

    for (uint32_t i = 0; i < frameBuffers.size(); i++)
        vkDestroyFramebuffer(swapChain->vke->device->dev, frameBuffers[i], nullptr);

    vkDestroyRenderPass     (swapChain->vke->device->dev, renderPass, VK_NULL_HANDLE);

    for(uint i=0; i<attachments.size(); i++)
        attachments[i].destroy();
}
uint32_t vks::RenderTarget::getWidth() {
    return attachments[0].infos.extent.width;
}
uint32_t vks::RenderTarget::getHeight() {
    return attachments[0].infos.extent.height;
}

void vks::RenderTarget::updateSize () {
    for (uint32_t i = 0; i < frameBuffers.size(); i++)
        vkDestroyFramebuffer(swapChain->vke->device->dev, frameBuffers[i], nullptr);
    for(uint i=0; i<attachments.size(); i++)
        attachments[i].destroy();

    createAttachments();
}

void vks::RenderTarget::createAttachments() {
    if (samples > VK_SAMPLE_COUNT_1_BIT) {
        attachments.resize(3);
        presentableAttachment = 2;

        attachments[0] = vks::Texture(swapChain->vke->device, VK_IMAGE_TYPE_2D, swapChain->infos.imageFormat,
                                      swapChain->infos.imageExtent.width, swapChain->infos.imageExtent.height,
                                      VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, 1, 1, 0,
                                      samples);
        attachments[0].createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT,1,1);

        attachments[1] = vks::Texture(swapChain->vke->device, VK_IMAGE_TYPE_2D, swapChain->depthFormat,
                                      swapChain->infos.imageExtent.width, swapChain->infos.imageExtent.height,
                                      VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, 1, 1, 0,
                                      samples);
        attachments[1].createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
    }else {
        attachments.resize(2);
        attachments[1] = vks::Texture(swapChain->vke->device, VK_IMAGE_TYPE_2D, swapChain->depthFormat,
                                      swapChain->infos.imageExtent.width, swapChain->infos.imageExtent.height,
                                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        attachments[1].createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
    }
}

void vks::RenderTarget::createFrameBuffers()
{
    std::vector<VkImageView> views;
    views.resize(attachments.size());
    for(uint i=0; i<attachments.size(); i++) {
        views[i] = attachments[i].view;
    }

    frameBuffers.resize(swapChain->imageCount);
    for(uint j=0; j<swapChain->imageCount; j++){

        views[presentableAttachment] = swapChain->buffers[j].view;


        VkFramebufferCreateInfo frameBufferCI = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        frameBufferCI.renderPass        = renderPass;
        frameBufferCI.attachmentCount   = (uint32_t)views.size();
        frameBufferCI.pAttachments      = views.data();
        frameBufferCI.width             = swapChain->infos.imageExtent.width;
        frameBufferCI.height            = swapChain->infos.imageExtent.height;
        frameBufferCI.layers            = 1;
        VK_CHECK_RESULT(vkCreateFramebuffer(swapChain->vke->device->dev, &frameBufferCI, nullptr, &frameBuffers[j]));
    }
}

void vks::RenderTarget::createDefaultRenderPass () {
    VkAttachmentDescription rpAttachments[] = {
        {0, //color
            attachments[0].infos.format, samples,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        },{0,//depth
           attachments[1].infos.format, samples,
           VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
           VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        },{0,// resolve
           swapChain->infos.imageFormat, VK_SAMPLE_COUNT_1_BIT,
           VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
           VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        }
    };

    VkAttachmentReference colorReference    = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference    = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolveReference  = {presentableAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    if (samples > VK_SAMPLE_COUNT_1_BIT)
        subpass.pResolveAttachments     = &resolveReference;


    VkSubpassDependency dependencies[] =
    {
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
    renderPassCI.attachmentCount    = attachments[0].infos.samples > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    renderPassCI.pAttachments       = rpAttachments;
    renderPassCI.subpassCount       = 1;
    renderPassCI.pSubpasses         = &subpass;
    renderPassCI.dependencyCount    = 2;
    renderPassCI.pDependencies      = dependencies;
    VK_CHECK_RESULT(vkCreateRenderPass(swapChain->vke->device->dev, &renderPassCI, nullptr, &renderPass));
}
