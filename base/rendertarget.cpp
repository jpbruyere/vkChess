#include "rendertarget.hpp"

#include "VkEngine.h"
#include "VulkanSwapChain.hpp"


vks::RenderTarget::RenderTarget(ptrSwapchain _swapChain, VkFormat depthFormat, VkSampleCountFlagBits samples) {
	swapChain = _swapChain;
	if (samples > VK_SAMPLE_COUNT_1_BIT) {
		attachments.resize(3);

		attachments[0] = vks::Texture(swapChain->vke->device, VK_IMAGE_TYPE_2D, swapChain->infos.imageFormat,
									  swapChain->infos.imageExtent.width, swapChain->infos.imageExtent.height,
									  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
									  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, 1, 1, 0,
									  samples);
		attachments[0].createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT,1,1);

		attachments[1] = vks::Texture(swapChain->vke->device, VK_IMAGE_TYPE_2D, depthFormat,
									  swapChain->infos.imageExtent.width, swapChain->infos.imageExtent.height,
									  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
									  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, 1, 1, 0,
									  samples);
		attachments[1].createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
	}else {
		attachments.resize(2);
		attachments[1] = vks::Texture(swapChain->vke->device, VK_IMAGE_TYPE_2D, depthFormat,
									  swapChain->infos.imageExtent.width, swapChain->infos.imageExtent.height,
									  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		attachments[1].createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
	}
}

vks::RenderTarget::~RenderTarget () {
	for(uint i=0; i<attachments.size(); i++) {
		attachments[i].destroy();
	}
}
