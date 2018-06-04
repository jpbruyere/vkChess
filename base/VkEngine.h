/*
* Vulkan Example base class
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "vke.h"

#include "rendertarget.hpp"
#include "VulkanBuffer.hpp"
#include "shadingcontext.hpp"

//const std::string getAssetPath();


VkPipelineShaderStageCreateInfo loadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage);

// AngelCode .fnt format structs and classes
struct bmchar {
    uint32_t x, y;
    uint32_t width;
    uint32_t height;
    int32_t xoffset;
    int32_t yoffset;
    int32_t xadvance;
    uint32_t page;
};
std::array<bmchar, 255> parsebmFont(const std::string& fileName);

namespace vks {

    class VkEngine
    {
    private:
        float       fpsTimer = 0.0f;
        uint32_t    frameCounter = 0;

        bool        viewUpdated = false;
        bool        resizing = false;
        uint32_t    destWidth;
        uint32_t    destHeight;

        std::string getWindowTitle();

        PFN_vkCreateDebugReportCallbackEXT  vkCreateDebugReportCallback;
        PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallback;
        VkDebugReportCallbackEXT            debugReportCallback;

        void createInstance (const std::string& app_name, std::vector<const char*>& extentions);
    protected:
        vks::vkPhyInfo      phyInfos;
        GLFWwindow*         window;

        struct VulkanSwapChain* swapChain;
        RenderTarget*           renderTarget;
        std::string     title = "Vulkan Example";
        std::string     name = "vulkanExample";

        virtual void windowResize();
    public:
        static std::vector<const char*> args;

        VkInstance      instance;
        ptrVkDev        device;
        VkSurfaceKHR    surface;


        uint32_t    lastFPS     = 0;
        float       frameTimer  = 1.0f;
        bool        prepared    = false;
        uint32_t    width, height;
        Camera      camera;
        glm::vec2   mousePos;
        bool        paused      = false;

        struct Settings {
            bool validation = false;
            bool fullscreen = false;
            bool vsync = false;
            bool multiSampling = true;
            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_4_BIT;
        } settings;

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

        glm::vec3 rotation = glm::vec3(0.0f, 135.0f, 0.0f);

        struct GamePadState {
            glm::vec2 axisLeft = glm::vec2(0.0f);
            glm::vec2 axisRight = glm::vec2(0.0f);
        } gamePadState;

        bool mouseButtons[3] = {false,false,false};


        VkEngine(uint32_t width, uint32_t height,
                 VkPhysicalDeviceType preferedGPU = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        virtual ~VkEngine();

        virtual void start();

        virtual void render() = 0;
        virtual void viewChanged();
        virtual void keyDown(uint32_t key);
        virtual void keyUp(uint32_t key);
        virtual void keyPressed(uint32_t key);
        virtual void handleMouseMove(int32_t x, int32_t y);
        virtual void handleMouseButtonDown(int buttonIndex);
        virtual void handleMouseButtonUp(int buttonIndex);
        virtual void prepare();

        void prepareUniformBuffers();
        void updateUniformBuffers();
        void updateParams();

        void renderLoop();
        void prepareFrame();
        void renderFrame();
    };
}
