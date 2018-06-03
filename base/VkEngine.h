/*
* Vulkan Example base class
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows")
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <sys/system_properties.h>
#include "VulkanAndroid.h"
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
#include <wayland-client.h>
#elif defined(_DIRECT2DISPLAY)
//
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <xcb/xcb.h>
#endif

#include <iostream>
#include <chrono>
#include <sys/stat.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <sstream>
#include <fstream>
#include <array>
#include <vector>
#include <numeric>

#include "vulkan/vulkan.h"

#include "macros.h"
#include "camera.hpp"
#include "keycodes.hpp"

#include "VulkanDevice.hpp"

#include "resource.hpp"
#include "VulkanBuffer.hpp"
#include "shadingcontext.hpp"

#include "VulkanSwapChain.hpp"




const std::string getAssetPath();
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

class VulkanExampleBase
{
private:
    float fpsTimer = 0.0f;
    uint32_t frameCounter = 0;
    std::string getWindowTitle();
    bool viewUpdated = false;
    uint32_t destWidth;
    uint32_t destHeight;
    bool resizing = false;
    void windowResize();
    PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallback;
    PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallback;
    VkDebugReportCallbackEXT debugReportCallback;
    RenderTarget multisampleTarget;
protected:
    VkInstance          instance;
    vks::VulkanDevice*  vulkanDevice;
    VkPhysicalDeviceProperties          deviceProperties;
    VkPhysicalDeviceFeatures            deviceFeatures;
    VkPhysicalDeviceMemoryProperties    deviceMemoryProperties;

    VkFormat depthFormat;
    VkRenderPass renderPass;
    VkFramebuffer frameBuffer;
    VulkanSwapChain swapChain;
    VkSemaphore presentCompleteSemaphore;
    std::string title = "Vulkan Example";
    std::string name = "vulkanExample";
public:
    uint32_t lastFPS = 0;
    static std::vector<const char*> args;
    bool prepared = false;
    uint32_t width = 1280;
    uint32_t height = 720;
    float frameTimer = 1.0f;
    Camera camera;
    glm::vec2 mousePos;
    bool paused = false;

    struct Settings {
        bool validation = false;
        bool fullscreen = false;
        bool vsync = false;
        bool multiSampling = true;
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_4_BIT;
    } settings;

    vks::Texture depthStencil;
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

    struct MouseButtons {
        bool left = false;
        bool right = false;
        bool middle = false;
    } mouseButtons;

#if defined(VK_USE_PLATFORM_XCB_KHR)
    bool quit = false;
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_intern_atom_reply_t *atom_wm_delete_window;
#endif

#if defined(VK_USE_PLATFORM_XCB_KHR)
    xcb_window_t setupWindow();
    void initxcbConnection();
    void handleEvent(const xcb_generic_event_t *event);
#endif

    VulkanExampleBase();
    virtual ~VulkanExampleBase();

    void initVulkan();

    virtual VkResult createInstance(bool enableValidation);
    virtual void render() = 0;
    virtual void viewChanged();
    virtual void keyDown(uint32_t);
    virtual void keyUp(uint32_t);
    virtual void keyPressed(uint32_t key);
    virtual void handleMouseMove(int32_t x, int32_t y);
    virtual void handleMouseButtonDown(int buttonIndex);
    virtual void handleMouseButtonUp(int buttonIndex);
    virtual void setupFrameBuffer();
    virtual void prepare();

    void initSwapchain();
    void setupSwapChain();

    void createRenderPass ();
    void createRenderTarget ();

    void prepareUniformBuffers();
    void updateUniformBuffers();
    void updateParams();

    void renderLoop();
    void prepareFrame();
    void renderFrame();
};
