/*
* Vulkan Example base class, stripped down version
*
* Copyright (C) 2016-2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "tiny_gltf.h"

#include "VkEngine.h"

#define ENGINE_NAME     "vke"
#define ENGINE_VERSION  1

#include "VulkanDevice.hpp"

#include "resource.hpp"
#include "VulkanBuffer.hpp"
#include "texture.hpp"

#include "shadingcontext.hpp"
#include "rendertarget.hpp"

#include "VulkanSwapChain.hpp"

VkPipelineShaderStageCreateInfo loadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage)
{
    VkPipelineShaderStageCreateInfo shaderStage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStage.stage = stage;
    shaderStage.pName = "main";
    std::ifstream is("shaders/" + filename, std::ios::binary | std::ios::in | std::ios::ate);

    if (is.is_open()) {
        size_t size = is.tellg();
        is.seekg(0, std::ios::beg);
        char* shaderCode = new char[size];
        is.read(shaderCode, size);
        is.close();
        assert(size > 0);
        VkShaderModuleCreateInfo moduleCreateInfo{};
        moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleCreateInfo.codeSize = size;
        moduleCreateInfo.pCode = (uint32_t*)shaderCode;
        vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderStage.module);
        delete[] shaderCode;
    }
    else {
        std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
        shaderStage.module = VK_NULL_HANDLE;
    }
    assert(shaderStage.module != VK_NULL_HANDLE);
    return shaderStage;
}

int32_t nextValuePair(std::stringstream *stream)
{
    std::string pair;
    *stream >> pair;
    uint32_t spos = pair.find("=");
    std::string value = pair.substr(spos + 1);
    int32_t val = std::stoi(value);
    return val;
}
// Basic parser fpr AngelCode bitmap font format files
// See http://www.angelcode.com/products/bmfont/doc/file_format.html for details
std::array<bmchar, 255> parsebmFont(const std::string& fileName)
{
    std::array<bmchar, 255> fontChars;

#if defined(__ANDROID__)
    // Font description file is stored inside the apk
    // So we need to load it using the asset manager
    AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, fileName.c_str(), AASSET_MODE_STREAMING);
    assert(asset);
    size_t size = AAsset_getLength(asset);

    assert(size > 0);

    void *fileData = malloc(size);
    AAsset_read(asset, fileData, size);
    AAsset_close(asset);

    std::stringbuf sbuf((const char*)fileData);
    std::istream istream(&sbuf);
#else
    std::filebuf fileBuffer;
    fileBuffer.open(fileName, std::ios::in);
    std::istream istream(&fileBuffer);
#endif

    assert(istream.good());

    while (!istream.eof())
    {
        std::string line;
        std::stringstream lineStream;
        std::getline(istream, line);
        lineStream << line;

        std::string info;
        lineStream >> info;

        if (info == "char")
        {
            // char id
            uint32_t charid = nextValuePair(&lineStream);
            // Char properties
            fontChars[charid].x = nextValuePair(&lineStream);
            fontChars[charid].y = nextValuePair(&lineStream);
            fontChars[charid].width = nextValuePair(&lineStream);
            fontChars[charid].height = nextValuePair(&lineStream);
            fontChars[charid].xoffset = nextValuePair(&lineStream);
            fontChars[charid].yoffset = nextValuePair(&lineStream);
            fontChars[charid].xadvance = nextValuePair(&lineStream);
            fontChars[charid].page = nextValuePair(&lineStream);
        }
    }
    return fontChars;
}

std::vector<const char*> vks::VkEngine::args;

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char * pLayerPrefix, const char * pMsg, void * pUserData)
{
    std::string prefix("");
    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        prefix += "ERROR:";
    };
    if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        prefix += "WARNING:";
    };
    if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        prefix += "DEBUG:";
    }
    std::stringstream debugMessage;
    debugMessage << prefix << " [" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg;
#if defined(__ANDROID__)
    LOGD("%s", debugMessage.str().c_str());
#else
    std::cout << debugMessage.str() << "\n";
#endif
    fflush(stdout);
    return VK_FALSE;
}

std::string vks::VkEngine::getWindowTitle()
{
    std::string device(this->device->properties.deviceName);
    std::string windowTitle;
    windowTitle = title + " - " + device + " - " + std::to_string(lastFPS) + " fps";
    return windowTitle;
}

static void onkey_callback (GLFWwindow* window, int key, int scanCode, int action ,int mods){
    vks::VkEngine* e = (vks::VkEngine*)glfwGetWindowUserPointer(window);
    if (action == GLFW_PRESS)
        e->keyDown(key);
    else
        e->keyUp(key);
}
static void char_callback (GLFWwindow* window, uint32_t c){
    vks::VkEngine* e = (vks::VkEngine*)glfwGetWindowUserPointer(window);
}
static void mouse_move_callback(GLFWwindow* window, double x, double y){
    vks::VkEngine* e = (vks::VkEngine*)glfwGetWindowUserPointer(window);
    e->handleMouseMove((int32_t)x, (int32_t)y);
}
static void mouse_button_callback(GLFWwindow* window, int but, int state, int modif){
    vks::VkEngine* e = (vks::VkEngine*)glfwGetWindowUserPointer(window);
    if (state == GLFW_PRESS){
        e->mouseButtons[but] = true;
        e->handleMouseButtonDown(but);
    }else{
        e->mouseButtons[but] = false;
        e->handleMouseButtonUp(but);
    }
}

vks::VkEngine::VkEngine (uint32_t _width, uint32_t _height,
                    VkPhysicalDeviceType preferedGPU)
{
    width = _width;
    height = _height;

    char* numConvPtr;
    // Parse command line arguments
    for (size_t i = 0; i < args.size(); i++)
    {
        if (args[i] == std::string("-validation")) {
            settings.validation = true;
        }
        if (args[i] == std::string("-vsync")) {
            settings.vsync = true;
        }
        if ((args[i] == std::string("-f")) || (args[i] == std::string("--fullscreen"))) {
            settings.fullscreen = true;
        }
        if ((args[i] == std::string("-w")) || (args[i] == std::string("--width"))) {
            uint32_t w = strtol(args[i + 1], &numConvPtr, 10);
            if (numConvPtr != args[i + 1]) { width = w; };
        }
        if ((args[i] == std::string("-h")) || (args[i] == std::string("--height"))) {
            uint32_t h = strtol(args[i + 1], &numConvPtr, 10);
            if (numConvPtr != args[i + 1]) { height = h; };
        }
    }

    glfwInit();
    assert (glfwVulkanSupported()==GLFW_TRUE);

    uint32_t enabledExtsCount = 0, phyCount = 0;
    const char ** enabledExts = glfwGetRequiredInstanceExtensions (&enabledExtsCount);
    std::vector<const char*> enabledExtentions;
    enabledExtentions.assign(enabledExts, enabledExts + enabledExtsCount);

    createInstance ("vkChess", enabledExtentions);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING,   GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED,  GLFW_TRUE);

    window = glfwCreateWindow (width, height, "Window Title", NULL, NULL);

    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback          (window, onkey_callback);
    glfwSetCharCallback         (window, char_callback);
    glfwSetMouseButtonCallback  (window, mouse_button_callback);
    glfwSetCursorPosCallback    (window, mouse_move_callback);
    //glfwSetScrollCallback       (e->window, onScroll);


    VK_CHECK_RESULT(glfwCreateWindowSurface(instance, window, NULL, &surface));

    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &phyCount, nullptr));
    assert(phyCount > 0);
    std::vector<VkPhysicalDevice> physicalDevices(phyCount);
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &phyCount, physicalDevices.data()));

    for (uint i=0; i<phyCount; i++){
        phyInfos = vks::vkPhyInfo(physicalDevices[i], surface);
        if (phyInfos.properties.deviceType == preferedGPU)
            break;
    }

}


vks::VkEngine::~VkEngine()
{
    sharedUBOs.matrices.destroy();
    sharedUBOs.params.destroy();

    delete renderTarget;
    delete swapChain;

    if (surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, surface, nullptr);

    surface = VK_NULL_HANDLE;

    delete device;

    //if (settings.validation)
    //    vkDestroyDebugReportCallback(instance, debugReportCallback, nullptr);

    vkDestroyInstance (instance, VK_NULL_HANDLE);
}

void vks::VkEngine::start () {
    device = new vks::VulkanDevice (phyInfos);

    swapChain = new VulkanSwapChain (this, false);
    swapChain->create (width, height);

    renderTarget = new RenderTarget(swapChain, settings.sampleCount);

    prepareUniformBuffers();

    prepare();

    prepared = true;

    while (!glfwWindowShouldClose (window)) {
        glfwPollEvents();
        auto tStart = std::chrono::high_resolution_clock::now();
        if (viewUpdated)
        {
            viewUpdated = false;
            viewChanged();
        }

        render();

        frameCounter++;
        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        frameTimer = tDiff / 1000.0f;
        camera.update(frameTimer);
        if (camera.moving())
            viewUpdated = true;
        fpsTimer += (float)tDiff;
        if (fpsTimer > 1000.0f)
        {
            glfwSetWindowTitle(window, getWindowTitle().c_str());
            lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
            fpsTimer = 0.0f;
            frameCounter = 0;
        }
    }
}



void vks::VkEngine::prepare(){}

void vks::VkEngine::renderFrame()
{
    auto tStart = std::chrono::high_resolution_clock::now();
    if (viewUpdated)
    {
        viewUpdated = false;
        viewChanged();
    }

    render();
    frameCounter++;
    auto tEnd = std::chrono::high_resolution_clock::now();
    auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    frameTimer = (float)tDiff / 1000.0f;
    camera.update(frameTimer);
    if (camera.moving())
    {
        viewUpdated = true;
    }
    fpsTimer += (float)tDiff;
    if (fpsTimer > 1000.0f)
    {
        lastFPS = static_cast<uint32_t>((float)frameCounter * (1000.0f / fpsTimer));
        fpsTimer = 0.0f;
        frameCounter = 0;
    }
}

void vks::VkEngine::renderLoop()
{
//    destWidth = width;
//    destHeight = height;
//#if defined(VK_USE_PLATFORM_XCB_KHR)
//    xcb_flush(connection);
//    while (!quit)
//    {
//        auto tStart = std::chrono::high_resolution_clock::now();
//        if (viewUpdated)
//        {
//            viewUpdated = false;
//            viewChanged();
//        }
//        xcb_generic_event_t *event;
//        while ((event = xcb_poll_for_event(connection)))
//        {
//            handleEvent(event);
//            free(event);
//        }
//        render();
//        frameCounter++;
//        auto tEnd = std::chrono::high_resolution_clock::now();
//        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
//        frameTimer = tDiff / 1000.0f;
//        camera.update(frameTimer);
//        if (camera.moving())
//        {
//            viewUpdated = true;
//        }
//        fpsTimer += (float)tDiff;
//        if (fpsTimer > 1000.0f)
//        {
//            std::string windowTitle = getWindowTitle();
//            xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
//                window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
//                windowTitle.size(), windowTitle.c_str());
//            lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
//            fpsTimer = 0.0f;
//            frameCounter = 0;
//        }
//    }
//#endif
//    // Flush device to make sure all resources can be freed
//    vkDeviceWaitIdle(vulkanDevice->dev);
}

void vks::VkEngine::prepareFrame()
{
    VkResult err = swapChain->acquireNextImage();
    if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_SUBOPTIMAL_KHR)) {
        windowResize();
    } else {
        VK_CHECK_RESULT(err);
    }
}

void vks::VkEngine::createInstance (const std::string& app_name, std::vector<const char*>& extentions) {
    VkApplicationInfo   infos = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
                        infos.pApplicationName  = app_name.c_str();
                        infos.applicationVersion= 1;
                        infos.pEngineName       = ENGINE_NAME;
                        infos.engineVersion     = ENGINE_VERSION;
                        infos.apiVersion        = VK_API_VERSION_1_0;


    std::vector<const char*> enabledLayers;
#if DEBUG
    enabledLayers.push_back ("VK_LAYER_LUNARG_standard_validation");
#endif

    VkInstanceCreateInfo inst_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
                         inst_info.pApplicationInfo        = &infos;
                         inst_info.enabledExtensionCount   = extentions.size();
                         inst_info.ppEnabledExtensionNames = extentions.data();
                         inst_info.enabledLayerCount       = enabledLayers.size();
                         inst_info.ppEnabledLayerNames     = enabledLayers.data();

    VK_CHECK_RESULT(vkCreateInstance (&inst_info, NULL, &instance));

}


//void vks::VkEngine::handleEvent(const xcb_generic_event_t *event)
//{
//    switch (event->response_type & 0x7f)
//    {
//    case XCB_CLIENT_MESSAGE:
//        if ((*(xcb_client_message_event_t*)event).data.data32[0] ==
//            (*atom_wm_delete_window).atom) {
//            quit = true;
//        }
//        break;
//    case XCB_MOTION_NOTIFY:
//    {
//        xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
//        handleMouseMove((int32_t)motion->event_x, (int32_t)motion->event_y);
//        break;
//    }
//    break;
//    case XCB_BUTTON_PRESS:
//    {
//        xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
//        switch (press->detail) {
//        case XCB_BUTTON_INDEX_1:
//            mouseButtons.left = true;
//            break;
//        case XCB_BUTTON_INDEX_2:
//            mouseButtons.middle = true;
//            break;
//        case XCB_BUTTON_INDEX_3:
//            mouseButtons.right = true;
//            break;
//        case XCB_BUTTON_INDEX_4://wheel scroll up
//            camera.translate(glm::vec3(0.0f, 0.0f, camera.zoomSpeed));
//            viewUpdated = true;
//            break;
//        case XCB_BUTTON_INDEX_5://wheel scroll down
//            camera.translate(glm::vec3(0.0f, 0.0f, -camera.zoomSpeed));
//            viewUpdated = true;
//            break;
//        }
//        handleMouseButtonDown(press->detail);
//    }
//    break;
//    case XCB_BUTTON_RELEASE:
//    {
//        xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
//        if (press->detail == XCB_BUTTON_INDEX_1)
//            mouseButtons.left = false;
//        if (press->detail == XCB_BUTTON_INDEX_2)
//            mouseButtons.middle = false;
//        if (press->detail == XCB_BUTTON_INDEX_3)
//            mouseButtons.right = false;
//        handleMouseButtonUp(press->detail);
//    }
//    break;
//    case XCB_KEY_PRESS:
//    {
//        const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
//        switch (keyEvent->detail)
//        {
//            case KEY_W:
//                camera.keys.up = true;
//                break;
//            case KEY_S:
//                camera.keys.down = true;
//                break;
//            case KEY_A:
//                camera.keys.left = true;
//                break;
//            case KEY_D:
//                camera.keys.right = true;
//                break;
//            case KEY_P:
//                paused = !paused;
//                break;
//        }
//        keyDown(keyEvent->detail);
//    }
//    break;
//    case XCB_KEY_RELEASE:
//    {
//        const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
//        switch (keyEvent->detail)
//        {
//            case KEY_W:
//                camera.keys.up = false;
//                break;
//            case KEY_S:
//                camera.keys.down = false;
//                break;
//            case KEY_A:
//                camera.keys.left = false;
//                break;
//            case KEY_D:
//                camera.keys.right = false;
//                break;
//            case KEY_ESCAPE:
//                quit = true;
//                break;
//        }
//        keyUp(keyEvent->detail);
//        keyPressed(keyEvent->detail);
//    }
//    break;
//    case XCB_DESTROY_NOTIFY:
//        quit = true;
//        break;
//    case XCB_CONFIGURE_NOTIFY:
//    {
//        const xcb_configure_notify_event_t *cfgEvent = (const xcb_configure_notify_event_t *)event;
//        if ((prepared) && ((cfgEvent->width != width) || (cfgEvent->height != height)))
//        {
//                destWidth = cfgEvent->width;
//                destHeight = cfgEvent->height;
//                if ((destWidth > 0) && (destHeight > 0))
//                {
//                    windowResize();
//                }
//        }
//    }
//    break;
//    default:
//        break;
//    }
//}


void vks::VkEngine::viewChanged() {
    updateUniformBuffers();
}

void vks::VkEngine::prepareUniformBuffers()
{
    // Objact vertex shader uniform buffer
    sharedUBOs.matrices.create(device,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        sizeof(mvpMatrices));

    // Shared parameter uniform buffer
    sharedUBOs.params.create(device,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        sizeof(lightingParams));

    // Map persistent
    sharedUBOs.matrices.map();
    sharedUBOs.params.map();

    updateUniformBuffers();
    updateParams();
}
void vks::VkEngine::updateUniformBuffers()
{
    // 3D object
    mvpMatrices.projection = camera.matrices.perspective;
    mvpMatrices.view = camera.matrices.view;
    mvpMatrices.view3 = glm::mat4(glm::mat3(camera.matrices.view));
    mvpMatrices.camPos = camera.position * -1.0f;
    sharedUBOs.matrices.copyTo (&mvpMatrices, sizeof(mvpMatrices));
}
void vks::VkEngine::updateParams()
{
    lightingParams.lightDir = glm::vec4(-10.0f, 150.f, -10.f, 1.0f);
    sharedUBOs.params.copyTo(&lightingParams, sizeof(lightingParams));
}

void vks::VkEngine::windowResize()
{
    if (!prepared)
        return;
    prepared = false;

    vkDeviceWaitIdle(device->dev);

    swapChain->create (width, height);

    vkDeviceWaitIdle(device->dev);

    camera.updateAspectRatio((float)width / (float)height);
    viewChanged();

    prepared = true;
}

void vks::VkEngine::keyDown(uint32_t key) {
    switch (key) {
    case GLFW_KEY_ESCAPE :
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    }

}
void vks::VkEngine::keyUp(uint32_t key) {
    keyPressed (key);
}
void vks::VkEngine::keyPressed(uint32_t key) {
    switch (key) {
    case GLFW_KEY_F1:
        if (lightingParams.exposure > 0.1f) {
            lightingParams.exposure -= 0.1f;
            updateParams();
            std::cout << "Exposure: " << lightingParams.exposure << std::endl;
        }
        break;
    case GLFW_KEY_F2:
        if (lightingParams.exposure < 10.0f) {
            lightingParams.exposure += 0.1f;
            updateParams();
            std::cout << "Exposure: " << lightingParams.exposure << std::endl;
        }
        break;
    case GLFW_KEY_F3:
        if (lightingParams.gamma > 0.1f) {
            lightingParams.gamma -= 0.1f;
            updateParams();
            std::cout << "Gamma: " << lightingParams.gamma << std::endl;
        }
        break;
    case GLFW_KEY_F4:
        if (lightingParams.gamma < 10.0f) {
            lightingParams.gamma += 0.1f;
            updateParams();
            std::cout << "Gamma: " << lightingParams.gamma << std::endl;
        }
        break;
    }
}
void vks::VkEngine::handleMouseMove(int32_t x, int32_t y)
{
    int32_t dx = (int32_t)mousePos.x - x;
    int32_t dy = (int32_t)mousePos.y - y;

    bool handled = false;

    if (handled) {
        mousePos = glm::vec2((float)x, (float)y);
        return;
    }

    if (mouseButtons[GLFW_MOUSE_BUTTON_RIGHT]) {
        camera.rotate(glm::vec3(-dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
        viewUpdated = true;
    }
    if (mouseButtons[GLFW_MOUSE_BUTTON_MIDDLE]) {
        camera.translate(glm::vec3(-dx * 0.01f, -dy * 0.01f, 0.0f));
        viewUpdated = true;
    }
    mousePos = glm::vec2((float)x, (float)y);
}
void vks::VkEngine::handleMouseButtonDown(int buttonIndex) {}
void vks::VkEngine::handleMouseButtonUp(int buttonIndex) {}

