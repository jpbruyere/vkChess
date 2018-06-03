/*
* Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>
#include <map>

#include <VkEngine.h>

#include "../src/vkrenderer.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gli/gli.hpp>

#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

//when mipLevels is set to 0 => mipmaps with be generated
#define AUTOGEN_MIPMAPS 0

namespace vkglTF
{
    /*
        glTF material class
    */
    enum AlphaMode : uint32_t { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };

    struct Material {
        glm::vec4 baseColorFactor = glm::vec4(1.0f);

        AlphaMode alphaMode = ALPHAMODE_OPAQUE;
        float alphaCutoff = 1.0f;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;

        uint32_t baseColorTexture = 0;
        uint32_t metallicRoughnessTexture = 0;
        uint32_t normalTexture = 0;
        uint32_t occlusionTexture = 0;

        uint32_t emissiveTexture = 0;
        uint32_t pad1;
        uint32_t pad2;
        uint32_t pad3;

        glm::vec4 emissiveFactor = glm::vec4(0.0f);
    };

    struct Dimension
    {
        glm::vec3 min = glm::vec3(FLT_MAX);
        glm::vec3 max = glm::vec3(-FLT_MAX);
        glm::vec3 size;
    };

    /*
        glTF primitive class
    */
    struct Primitive {
        std::string name;
        uint32_t    indexBase;
        uint32_t    indexCount;
        uint32_t    vertexBase;
        uint32_t    vertexCount;
        uint32_t    material;
        Dimension   dims;
    };

    struct InstanceData {
        uint32_t    materialIndex = 0;
        glm::mat4   modelMat = glm::mat4();
        glm::vec4   color = glm::vec4(0);
    };

    /*
        glTF model loading and rendering class
    */
    struct Model {
        bool        prepared    = false;
        uint32_t    textureSize = 1024; //texture array size w/h

        struct Vertex {
            glm::vec3 pos;
            glm::vec3 normal;
            glm::vec2 uv;
        };

        vks::VulkanDevice*  device;

        //vertices list are kept for meshLoading only (usefull for bullet lowpoly meshes without texture or material
        //their are cleared for normal loading
        std::vector<uint32_t>       indices;
        std::vector<Vertex>         vertices;
        std::vector<vks::Texture>   textures;
        std::vector<Material>       materials;
        std::map<std::string, int>  materialsNames;
        std::vector<uint32_t>       instances;//TODO:should store part idx and count
        std::vector<InstanceData>   instanceDatas;

        vks::Buffer     vbo;
        vks::Buffer     ibo;
        vks::Buffer     vboInstances;
        vks::Buffer     uboMaterials;

        vks::Texture*   texAtlas        = NULL;//array of all textuures used by model
        VkDescriptorSet descriptorSet   = VK_NULL_HANDLE;//atlas sampler and materials ubo, defined by pbr renderer

        std::vector<Primitive> primitives;

        void destroy()
        {
            prepared = false;
            vbo.destroy();
            ibo.destroy();
            vboInstances.destroy();
            uboMaterials.destroy();
            if (texAtlas) {
                texAtlas->destroy();
                delete (texAtlas);
            }

            for (vks::Texture texture : textures)
                texture.destroy();
        }

        void loadNode(const tinygltf::Node &node, const glm::mat4 &parentMatrix, const tinygltf::Model &model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale)
        {
            // Generate local node matrix
            glm::vec3 translation = glm::vec3(0.0f);
            if (node.translation.size() == 3) {
                translation = glm::make_vec3(node.translation.data());
            }
            glm::mat4 rotation = glm::mat4(1.0f);
            if (node.rotation.size() == 4) {
                glm::quat q = glm::make_quat(node.rotation.data());
                rotation = glm::mat4(q);
            }
            glm::vec3 scale = glm::vec3(1.0f);
            if (node.scale.size() == 3) {
                scale = glm::make_vec3(node.scale.data());
            }
            glm::mat4 localNodeMatrix = glm::mat4(1.0f);
            if (node.matrix.size() == 16) {
                localNodeMatrix = glm::make_mat4x4(node.matrix.data());
            } else {
                // T * R * S
                localNodeMatrix = glm::translate(glm::mat4(1.0f), translation) * rotation * glm::scale(glm::mat4(1.0f), scale);
            }
            localNodeMatrix = parentMatrix * localNodeMatrix;

            // Parent node with children
            if (node.children.size() > 0)
                for (uint i = 0; i < node.children.size(); i++)
                    loadNode(model.nodes[node.children[i]], localNodeMatrix, model, indexBuffer, vertexBuffer, globalscale);

            // Node contains mesh data
            if (node.mesh > -1) {
                const tinygltf::Mesh mesh = model.meshes[node.mesh];

                for (size_t j = 0; j < mesh.primitives.size(); j++) {
                    const tinygltf::Primitive &primitive = mesh.primitives[j];
                    if (primitive.indices < 0)
                        continue;

                    Primitive modPart;

                    modPart.indexBase = static_cast<uint32_t>(indexBuffer.size());
                    modPart.vertexBase = static_cast<uint32_t>(vertexBuffer.size());
                    modPart.material = primitive.material;
                    modPart.name = node.name;

                    // Vertices
                    {
                        const float *bufferPos = nullptr;
                        const float *bufferNormals = nullptr;
                        const float *bufferTexCoords = nullptr;

                        // Position attribute is required
                        assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

                        const tinygltf::Accessor &posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
                        const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
                        bufferPos = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));

                        if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                            const tinygltf::Accessor &normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
                            const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
                            bufferNormals = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
                        }

                        if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                            const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
                            const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
                            bufferTexCoords = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
                        }

                        modPart.vertexCount = posAccessor.count;
                        for (size_t v = 0; v < posAccessor.count; v++) {
                            Vertex vert{};
                            vert.pos = localNodeMatrix * glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
                            vert.pos *= globalscale;
                            vert.normal = glm::normalize(glm::mat3(localNodeMatrix) * glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
                            vert.uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
                            // Vulkan coordinate system
                            //vert.pos.y *= -1.0f;
                            //vert.normal.y *= -1.0f;
                            vertexBuffer.push_back(vert);

                            modPart.dims.max.x = fmax(vert.pos.x, modPart.dims.max.x);
                            modPart.dims.max.y = fmax(vert.pos.y, modPart.dims.max.y);
                            modPart.dims.max.z = fmax(vert.pos.z, modPart.dims.max.z);

                            modPart.dims.min.x = fmin(vert.pos.x, modPart.dims.min.x);
                            modPart.dims.min.y = fmin(vert.pos.y, modPart.dims.min.y);
                            modPart.dims.min.z = fmin(vert.pos.z, modPart.dims.min.z);
                        }

                        modPart.dims.size = modPart.dims.max - modPart.dims.min;
                    }
                    // Indices
                    {
                        const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
                        const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
                        const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

                        modPart.indexCount = static_cast<uint32_t>(accessor.count);

                        switch (accessor.componentType) {
                        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                            uint32_t *buf = new uint32_t[accessor.count];
                            memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indexBuffer.push_back(buf[index]);
                            }
                            break;
                        }
                        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                            uint16_t *buf = new uint16_t[accessor.count];
                            memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indexBuffer.push_back(buf[index]);
                            }
                            break;
                        }
                        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                            uint8_t *buf = new uint8_t[accessor.count];
                            memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indexBuffer.push_back(buf[index]);
                            }
                            break;
                        }
                        default:
                            std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
                            return;
                        }
                    }
                    primitives.push_back (modPart);
                }
            }
        }

        void loadImages(tinygltf::Model &gltfModel, vks::VulkanDevice *device, VkQueue copyQueue, bool loadOnly = false)
        {
            for (tinygltf::Image &gltfimage : gltfModel.images) {
                std::cout << "gltf image loading: " << gltfimage.name << std::endl << std::flush;

                unsigned char* buffer = nullptr;
                VkDeviceSize bufferSize = 0;
                bool deleteBuffer = false;
                if (gltfimage.component == 3) {
                    // Most devices don't support RGB only on Vulkan so convert if necessary
                    // TODO: Check actual format support and transform only if required
                    bufferSize = gltfimage.width * gltfimage.height * 4;
                    buffer = new unsigned char[bufferSize];
                    unsigned char* rgba = buffer;
                    unsigned char* rgb = gltfimage.image.data();
                    for (size_t i = 0; i< gltfimage.width * gltfimage.height; ++i) {
                        for (int32_t j = 0; j < 3; ++j) {
                            rgba[j] = rgb[j];
                        }
                        rgba += 4;
                        rgb += 3;
                    }
                    deleteBuffer = true;
                }
                else {
                    buffer = gltfimage.image.data();
                    bufferSize = gltfimage.image.size();
                }

                //Texture test(device, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, gltfimage.width, gltfimage.height,
                //             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);


                VkFormatProperties formatProperties;
                vkGetPhysicalDeviceFormatProperties(device->phy, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
                assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
                assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

                uint32_t mipLevels = loadOnly ? 1 :
                            static_cast<uint32_t>(floor(log2(std::max(gltfimage.width, gltfimage.height))) + 1.0);

                vks::Texture texture;

                texture.create(device,
                       VK_IMAGE_TYPE_2D,VK_FORMAT_R8G8B8A8_UNORM, gltfimage.width, gltfimage.height,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       VK_IMAGE_TILING_OPTIMAL, mipLevels);

                texture.copyTo(copyQueue, buffer, bufferSize);

                if (!loadOnly) {
                    texture.buildMipmaps (copyQueue);

                    texture.createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
                    texture.createSampler(VK_FILTER_LINEAR,VK_SAMPLER_ADDRESS_MODE_REPEAT,VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                  VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);

                    texture.updateDescriptor();
                }

                textures.push_back(texture);
            }
        }

        void loadMaterials(tinygltf::Model &gltfModel)
        {
            for (tinygltf::Material &mat : gltfModel.materials) {
                vkglTF::Material material = {};
                if (mat.values.find("baseColorFactor") != mat.values.end())
                    material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());

                if (mat.values.find("baseColorTexture") != mat.values.end())
                    material.baseColorTexture = gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source + 1;

                if (mat.values.find("metallicRoughnessTexture") != mat.values.end())
                    material.metallicRoughnessTexture = gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source + 1;

                if (mat.values.find("roughnessFactor") != mat.values.end())
                    material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());

                if (mat.values.find("metallicFactor") != mat.values.end())
                    material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());

                if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end())
                    material.normalTexture = gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source + 1;

                if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
                    material.emissiveTexture = gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source + 1;
                    material.emissiveFactor = glm::vec4(1.0f);
                }
                //emissive factor has boolean_value here (blender export or tinygltf error???
                /*if (mat.additionalValues.find("emissiveFactor") != mat.values.end()){
                    tinygltf::Parameter pm = mat.additionalValues["emissiveFactor"];
                    if (pm.bool_value);
                        material.emissiveFactor = glm::make_vec3(pm.ColorFactor().data());
                }*/

                if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end())
                    material.occlusionTexture = gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source + 1;

                if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
                    tinygltf::Parameter param = mat.additionalValues["alphaMode"];
                    if (param.string_value == "BLEND") {
                        material.alphaMode = ALPHAMODE_BLEND;
                    }
                    if (param.string_value == "MASK") {
                        material.alphaMode = ALPHAMODE_MASK;
                    }
                }
                if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
                    material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
                }
                materialsNames.insert(std::make_pair(mat.name, materials.size()));
                materials.push_back(material);
            }
        }

        void loadFromFile(std::string filename, vks::VulkanDevice* _device,
                          bool instancedRendering = false, float scale = 1.0f, bool meshOnly = false)
        {
            device = _device;//should be set in CTOR

            tinygltf::Model gltfModel;
            tinygltf::TinyGLTF gltfContext;
            std::string error;

#if defined(__ANDROID__)
            AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
            assert(asset);
            size_t size = AAsset_getLength(asset);
            assert(size > 0);
            char* fileData = new char[size];
            AAsset_read(asset, fileData, size);
            AAsset_close(asset);
            std::string baseDir;
            bool fileLoaded = gltfContext.LoadASCIIFromString(&gltfModel, &error, fileData, size, baseDir);
            free(fileData);
#else
            bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, filename.c_str());
#endif

            if (fileLoaded) {
                if (!meshOnly) {
                    loadImages(gltfModel, device, device->queue, instancedRendering);
                    if (instancedRendering){
                        texAtlas = new vks::Texture(device, device->queue, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, textures, textureSize, textureSize);
                        for (vks::Texture texture : textures){
                            texture.destroy();
                        }
                        textures.clear();
                    }
                    loadMaterials(gltfModel);
                    if (instancedRendering){
                        buildMaterialBuffer();
                        updateMaterialBuffer();
                    }
                }
                const tinygltf::Scene &scene = gltfModel.scenes[gltfModel.defaultScene];
                for (size_t i = 0; i < scene.nodes.size(); i++) {
                    const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
                    loadNode(node, glm::mat4(1.0f), gltfModel, indices, vertices, scale);
                }
                if (meshOnly)
                    return;
            }
            else {
                // TODO: throw
                std::cerr << "Could not load gltf file: " << error << std::endl;
                exit(-1);
            }

            size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
            size_t indexBufferSize = indices.size() * sizeof(uint32_t);

            assert((vertexBufferSize > 0) && (indexBufferSize > 0));

            vks::Buffer vertexStaging, indexStaging;
            vertexStaging.create (device,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                vertexBufferSize, vertices.data());
            indexStaging.create (device,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                indexBufferSize, indices.data());

            vbo.create (device,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                vertexBufferSize);
            ibo.create (device,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                indexBufferSize);


            // Copy from staging buffers
            VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            VkBufferCopy copyRegion = {};

            copyRegion.size = vertexBufferSize;
            vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vbo.buffer, 1, &copyRegion);

            copyRegion.size = indexBufferSize;
            vkCmdCopyBuffer(copyCmd, indexStaging.buffer, ibo.buffer, 1, &copyRegion);

            device->flushCommandBuffer(copyCmd, device->queue, true);

            vertexStaging.destroy();
            indexStaging.destroy();

            vertices.clear();
            indices.clear();

            prepared = true;
        }
        void addOneInstanceOfEach () {
            for (uint i=0; i<primitives.size(); i++)
                addInstance(i,glm::mat4(1));
        }
        uint32_t addInstance(uint32_t partIdx,const glm::mat4& modelMat, int materialIdx = -1){
            uint32_t idx = instances.size();
            instances.push_back (partIdx);
            InstanceData id;
            if (materialIdx < 0)//gltf material
                id.materialIndex = primitives[partIdx].material;
            else//force another material
                id.materialIndex = materialIdx;
            id.modelMat = modelMat;
            instanceDatas.push_back(id);
            return idx;
        }
        uint32_t addInstance(const std::string& name, const glm::mat4& modelMat, int materialIdx = -1){
            for (uint i=0; i<primitives.size(); i++) {
                if (name != primitives[i].name)
                    continue;
                return addInstance(i,modelMat, materialIdx);
            }
            return 0;
        }
        Primitive* getPrimitiveFromInstanceIdx (uint32_t idx) {
            return &primitives[instances[idx]];
        }
        int getPrimitiveIndex (const std::string& _name) {
            auto it = find_if(primitives.begin(), primitives.end(), [&_name](const Primitive& obj) {return obj.name == _name;});
            return (it != primitives.end()) ? std::distance(primitives.begin(), it) : -1 ;
        }
        int getMaterialIndex (const std::string& _name) {
            std::map<std::string, int>::iterator it = materialsNames.find(_name);
            return it == materialsNames.end() ? -1 : it->second;
        }
        void allocateDescriptorSet (vks::ShadingContext* shadingCtx) {
            descriptorSet = shadingCtx->allocateDescriptorSet(1);

            shadingCtx->updateDescriptorSet (descriptorSet,
                {
                    {1,0,texAtlas},
                    {1,1,&uboMaterials}
                });
        }
        void buildCommandBuffer(VkCommandBuffer cmdBuff, VkPipelineLayout pipelineLayout){
            if (!prepared)
                return;
            VkDeviceSize offsets[1] = { 0 };

            if (descriptorSet)
                vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &descriptorSet, 0, NULL);

            vkCmdBindVertexBuffers(cmdBuff, 0, 1, &vbo.buffer, offsets);
            vkCmdBindIndexBuffer(cmdBuff, ibo.buffer, 0, VK_INDEX_TYPE_UINT32);

            if (instances.empty()) {
                for (auto primitive : primitives)
                    vkCmdDrawIndexed(cmdBuff, primitive.indexCount, 1, primitive.vertexBase, 0, 0);
                return;
            }

            //instance buffer
            vkCmdBindVertexBuffers(cmdBuff, 1, 1, &vboInstances.buffer, offsets);

            uint32_t partIdx = instances[0];
            uint32_t instCount = 0;
            uint32_t instOffset = 0;

            for (uint i = 0; i < instances.size(); i++){
                if (partIdx != instances[i]) {
                    vkCmdDrawIndexed(cmdBuff,	primitives[partIdx].indexCount, instCount,
                                                primitives[partIdx].indexBase,
                                                primitives[partIdx].vertexBase, instOffset);

                    partIdx = instances[i];
                    instCount = 0;
                    instOffset = i;
                }
                instCount++;
            }
            if (instCount==0)
                return;

            vkCmdDrawIndexed(cmdBuff,	primitives[partIdx].indexCount, instCount,
                                        primitives[partIdx].indexBase,
                                        primitives[partIdx].vertexBase, instOffset);
        }

        void buildMaterialBuffer () {
            uboMaterials.create (device,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                sizeof(Material)*16);

            VK_CHECK_RESULT(uboMaterials.map());
            updateMaterialBuffer();
        }
        void buildInstanceBuffer (){
            if (vboInstances.size > 0){
                vkDeviceWaitIdle(device->dev);
                vboInstances.destroy();
            }

            vboInstances.create (device,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                instanceDatas.size() * sizeof(InstanceData));

            VK_CHECK_RESULT(vboInstances.map());
            minDirty = 0;
            maxDirty = instanceDatas.size()-1;
            updateInstancesBuffer();
        }

        int minDirty = INT_MAX, maxDirty = 0;

        void setInstanceIsDirty (uint32_t idx) {
            if (idx < minDirty)
                minDirty = idx;
            if (idx > maxDirty)
                maxDirty = idx;
        }

        void updateInstancesBuffer(){
            long count = maxDirty - minDirty + 1;
            if (count > 0) {
                size_t offset = minDirty * sizeof(InstanceData);
                memcpy((char*)vboInstances.mapped + offset,
                       (char*)instanceDatas.data() + offset, count * sizeof(InstanceData));

            }
            minDirty = INT_MAX;
            maxDirty = 0;
        }
        void updateMaterialBuffer(){
            memcpy(uboMaterials.mapped, materials.data(), sizeof(Material)*materials.size());
        }
        void disableEmissive (uint32_t matIdx) {
            VkDeviceSize offset = matIdx * sizeof(Material) + 48;
            uint32_t* emissive = (uint32_t*)(uboMaterials.mapped + offset);
            *emissive = 0;
            uboMaterials.flush(4, offset);
        }
        void enableEmissive (uint32_t matIdx) {
            VkDeviceSize offset = matIdx * sizeof(Material) + 48;
            uint32_t* emissive = (uint32_t*)(uboMaterials.mapped + offset);
            *emissive = materials[matIdx].emissiveTexture;
            uboMaterials.flush(4, offset);
        }
    };
}
