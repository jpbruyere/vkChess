#pragma once

#include "VkEngine.h"

namespace vks {
    struct BindingSlot {
        uint layoutIdx;
        uint descriptorIdx;
        Resource* pResource;
    };

    class ShadingContext
    {
        vks::VulkanDevice*					device;
        VkDescriptorPool					descriptorPool;

        std::vector<std::vector<VkDescriptorSetLayoutBinding>> descriptorSetLayoutBindings;

        uint32_t                maxSets;

    public:
        std::vector<VkDescriptorSetLayout>	layouts;

        ShadingContext(vks::VulkanDevice* _device, uint32_t maxDescriptorSet = 1) {
            device = _device;
            maxSets = maxDescriptorSet;
        }
        virtual ~ShadingContext() {
            for(uint i=0; i<layouts.size(); i++)
                vkDestroyDescriptorSetLayout(device->dev, layouts[i], VK_NULL_HANDLE);
            vkDestroyDescriptorPool(device->dev, descriptorPool, VK_NULL_HANDLE);
        }

        uint32_t addDescriptorSetLayout (const std::vector<VkDescriptorSetLayoutBinding>& descriptorSetLayoutBinding) {
            uint32_t idx = descriptorSetLayoutBindings.size();
            descriptorSetLayoutBindings.push_back (descriptorSetLayoutBinding);
            return idx;
        }

        void prepare () {
            uint32_t descTypeCount[VK_DESCRIPTOR_TYPE_END_RANGE+1] = {};
            //count descriptor types to init desc pool
            for(uint i=0; i<descriptorSetLayoutBindings.size(); i++)
                for(uint j=0; j<descriptorSetLayoutBindings[i].size(); j++)
                    descTypeCount[descriptorSetLayoutBindings[i][j].descriptorType] += descriptorSetLayoutBindings[i][j].descriptorCount;

            std::vector<VkDescriptorPoolSize> poolSizes;
            for(int i=VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i<VK_DESCRIPTOR_TYPE_END_RANGE; i++) {
                if (descTypeCount[i]>0)
                    poolSizes.push_back ({(VkDescriptorType)i, descTypeCount[i]});
            }
            VkDescriptorPoolCreateInfo poolInfo= {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.poolSizeCount  = (uint32_t)poolSizes.size();
            poolInfo.pPoolSizes     = poolSizes.data();
            poolInfo.maxSets        = maxSets;
            VK_CHECK_RESULT(vkCreateDescriptorPool(device->dev, &poolInfo, nullptr, &descriptorPool));

            layouts.resize(descriptorSetLayoutBindings.size());
            for(uint i=0; i<descriptorSetLayoutBindings.size(); i++) {
                VkDescriptorSetLayoutCreateInfo layoutInfos = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                layoutInfos.bindingCount= (uint32_t)descriptorSetLayoutBindings[i].size();
                layoutInfos.pBindings	= descriptorSetLayoutBindings[i].data();
                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->dev, &layoutInfos, nullptr, &layouts[i]));
            }
        }

        VkDescriptorSet allocateDescriptorSet (uint32_t layoutIndex) {
            VkDescriptorSet ds = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo dsInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            dsInfo.descriptorPool       = descriptorPool;
            dsInfo.pSetLayouts          = &layouts[layoutIndex];
            dsInfo.descriptorSetCount   = 1;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device->dev, &dsInfo, &ds));
            return ds;
        }
        void updateDescriptorSet (VkDescriptorSet ds, const std::vector<BindingSlot>& slots) {
            std::vector<VkWriteDescriptorSet> writeDescriptorSets;

            for (uint i=0; i<slots.size(); i++) {
                VkDescriptorSetLayoutBinding dslb = descriptorSetLayoutBindings[slots[i].layoutIdx][slots[i].descriptorIdx];
                writeDescriptorSets.push_back (
                            slots[i].pResource->getWriteDescriptorSet (ds, dslb.binding, dslb.descriptorType));
            }
            vkUpdateDescriptorSets(device->dev,(uint32_t) writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
        }
    };
}
