/*
* Vulkan Example base class, stripped down version
*
* Copyright (C) 2016-2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/


#include "VkEngine.h"
#include <fstream>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "tiny_gltf.h"



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

std::vector<const char*> VulkanExampleBase::args;

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

VkResult VulkanExampleBase::createInstance(bool enableValidation)
{
    this->settings.validation = enableValidation;

    // Validation can also be forced via a define
#if defined(_VALIDATION)
    this->settings.validation = true;
#endif

    VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = name.c_str();
    appInfo.pEngineName = name.c_str();
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

    // Enable surface extensions depending on os
#if defined(VK_USE_PLATFORM_XCB_KHR)
    instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo instanceCreateInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceCreateInfo.pApplicationInfo = &appInfo;

    if (instanceExtensions.size() > 0)
    {
        if (settings.validation)
            instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
    }
    if (settings.validation) {
#if !defined(__ANDROID__)
        instanceCreateInfo.enabledLayerCount = 1;
        const char *validationLayerNames[] = {
            "VK_LAYER_LUNARG_standard_validation"
        };
        instanceCreateInfo.ppEnabledLayerNames = validationLayerNames;
#else
        instanceCreateInfo.enabledLayerCount = 6;
        const char *validationLayerNames[] = {
            "VK_LAYER_GOOGLE_threading",
            "VK_LAYER_LUNARG_parameter_validation",
            "VK_LAYER_LUNARG_object_tracker",
            "VK_LAYER_LUNARG_core_validation",
            "VK_LAYER_LUNARG_swapchain",
            "VK_LAYER_GOOGLE_unique_objects"
        };
        instanceCreateInfo.ppEnabledLayerNames = validationLayerNames;
#endif
    }
    return vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
}

std::string VulkanExampleBase::getWindowTitle()
{
    std::string device(deviceProperties.deviceName);
    std::string windowTitle;
    windowTitle = title + " - " + device + " - " + std::to_string(lastFPS) + " fps";
    return windowTitle;
}


void VulkanExampleBase::createRenderPass () {
    std::array<VkAttachmentDescription, 2> attachments = {};

    // Multisampled attachment that we render to
    attachments[0].format = swapChain.colorFormat;
    attachments[0].samples = settings.sampleCount;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Multisampled depth attachment we render to
    attachments[1].format = depthFormat;
    attachments[1].samples = settings.sampleCount;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCI = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassCI.attachmentCount = attachments.size();
    renderPassCI.pAttachments = attachments.data();
    renderPassCI.subpassCount = 1;
    renderPassCI.pSubpasses = &subpass;
    renderPassCI.dependencyCount = 2;
    renderPassCI.pDependencies = dependencies.data();
    VK_CHECK_RESULT(vkCreateRenderPass(vulkanDevice->dev, &renderPassCI, nullptr, &renderPass));
}

void VulkanExampleBase::prepare()
{
    initSwapchain();
    setupSwapChain();

    presentCompleteSemaphore = vulkanDevice->createSemaphore();

    createRenderTarget();

    //setupFrameBuffer();

    swapChain.multisampleTarget = &multisampleTarget;
    swapChain.depthStencil = &depthStencil;

    prepareUniformBuffers();
    prepared = true;
}

void VulkanExampleBase::renderFrame()
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

void VulkanExampleBase::renderLoop()
{
    destWidth = width;
    destHeight = height;
#if defined(VK_USE_PLATFORM_XCB_KHR)
    xcb_flush(connection);
    while (!quit)
    {
        auto tStart = std::chrono::high_resolution_clock::now();
        if (viewUpdated)
        {
            viewUpdated = false;
            viewChanged();
        }
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(connection)))
        {
            handleEvent(event);
            free(event);
        }
        render();
        frameCounter++;
        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        frameTimer = tDiff / 1000.0f;
        camera.update(frameTimer);
        if (camera.moving())
        {
            viewUpdated = true;
        }
        fpsTimer += (float)tDiff;
        if (fpsTimer > 1000.0f)
        {
            std::string windowTitle = getWindowTitle();
            xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
                window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                windowTitle.size(), windowTitle.c_str());
            lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
            fpsTimer = 0.0f;
            frameCounter = 0;
        }
    }
#endif
    // Flush device to make sure all resources can be freed
    vkDeviceWaitIdle(vulkanDevice->dev);
}

void VulkanExampleBase::prepareFrame()
{
    VkResult err = swapChain.acquireNextImage(presentCompleteSemaphore);
    if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_SUBOPTIMAL_KHR)) {
        windowResize();
    } else {
        VK_CHECK_RESULT(err);
    }
}

VulkanExampleBase::VulkanExampleBase()
{
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

#if defined(VK_USE_PLATFORM_XCB_KHR)
    initxcbConnection();
#endif
}

VulkanExampleBase::~VulkanExampleBase()
{
    sharedUBOs.matrices.destroy();
    sharedUBOs.params.destroy();

    // Clean up Vulkan resources
    swapChain.cleanup();

    vkDestroyRenderPass     (vulkanDevice->dev, renderPass, nullptr);
    vkDestroyFramebuffer    (vulkanDevice->dev, frameBuffer, nullptr);

    vulkanDevice->destroySemaphore(presentCompleteSemaphore);

    depthStencil.destroy();

    if (settings.multiSampling) {
        multisampleTarget.color.destroy();
        multisampleTarget.depth.destroy();
    }
    delete vulkanDevice;
    if (settings.validation)
        vkDestroyDebugReportCallback(instance, debugReportCallback, nullptr);

    vkDestroyInstance(instance, nullptr);
#if defined(_DIRECT2DISPLAY)
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    wl_shell_surface_destroy(shell_surface);
    wl_surface_destroy(surface);
    if (keyboard)
        wl_keyboard_destroy(keyboard);
    if (pointer)
        wl_pointer_destroy(pointer);
    wl_seat_destroy(seat);
    wl_shell_destroy(shell);
    wl_compositor_destroy(compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    // todo : android cleanup (if required)
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);
#endif
}

void VulkanExampleBase::initVulkan()
{
    VkResult err;

    err = createInstance(settings.validation);
    if (err) {
        std::cerr << "Could not create Vulkan instance!" << std::endl;
        exit(err);
    }

    if (settings.validation) {
        vkCreateDebugReportCallback = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
        vkDestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
        VkDebugReportCallbackCreateInfoEXT debugCreateInfo{};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
        debugCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)debugMessageCallback;
        debugCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        VK_CHECK_RESULT(vkCreateDebugReportCallback(instance, &debugCreateInfo, nullptr, &debugReportCallback));
    }

    uint32_t gpuCount = 0;
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
    assert(gpuCount > 0);
    std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
    err = vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
    if (err) {
        std::cerr << "Could not enumerate physical devices!" << std::endl;
        exit(err);
    }
    uint32_t selectedDevice = 0;
#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
    for (size_t i = 0; i < args.size(); i++) {
        if ((args[i] == std::string("-g")) || (args[i] == std::string("--gpu"))) {
            char* endptr;
            uint32_t index = strtol(args[i + 1], &endptr, 10);
            if (endptr != args[i + 1])  {
                if (index > gpuCount - 1) {
                    std::cerr << "Selected device index " << index << " is out of range, reverting to device 0 (use -listgpus to show available Vulkan devices)" << std::endl;
                } else {
                    std::cout << "Selected Vulkan device " << index << std::endl;
                    selectedDevice = index;
                }
            };
            break;
        }
    }
#endif

    VkPhysicalDevice phy = physicalDevices[selectedDevice];

    vkGetPhysicalDeviceProperties       (phy, &deviceProperties);
    vkGetPhysicalDeviceFeatures         (phy, &deviceFeatures);
    vkGetPhysicalDeviceMemoryProperties (phy, &deviceMemoryProperties);

    vulkanDevice = new vks::VulkanDevice(phy);
    VkPhysicalDeviceFeatures enabledFeatures = {};
    if (deviceFeatures.samplerAnisotropy)
        enabledFeatures.samplerAnisotropy = VK_TRUE;

    std::vector<const char*> enabledExtensions{};
    VkResult res = vulkanDevice->createLogicalDevice(enabledFeatures, enabledExtensions);
    if (res != VK_SUCCESS) {
        std::cerr << "Could not create Vulkan device!" << std::endl;
        exit(res);
    }

    depthFormat = vulkanDevice->getSuitableDepthFormat();

    swapChain.connect(instance, vulkanDevice);
}

#if defined(VK_USE_PLATFORM_XCB_KHR)

static inline xcb_intern_atom_reply_t* intern_atom_helper(xcb_connection_t *conn, bool only_if_exists, const char *str)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, only_if_exists, strlen(str), str);
    return xcb_intern_atom_reply(conn, cookie, NULL);
}

// Set up a window using XCB and request event types
xcb_window_t VulkanExampleBase::setupWindow()
{
    uint32_t value_mask, value_list[32];

    window = xcb_generate_id(connection);

    value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    value_list[0] = screen->black_pixel;
    value_list[1] =
        XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_EXPOSURE |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE;

    if (settings.fullscreen)
    {
        width = destWidth = screen->width_in_pixels;
        height = destHeight = screen->height_in_pixels;
    }

    xcb_create_window(connection,
        XCB_COPY_FROM_PARENT,
        window, screen->root,
        0, 0, width, height, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        value_mask, value_list);

    /* Magic code that will send notification when window is destroyed */
    xcb_intern_atom_reply_t* reply = intern_atom_helper(connection, true, "WM_PROTOCOLS");
    atom_wm_delete_window = intern_atom_helper(connection, false, "WM_DELETE_WINDOW");

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
        window, (*reply).atom, 4, 32, 1,
        &(*atom_wm_delete_window).atom);

    std::string windowTitle = getWindowTitle();
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
        window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
        title.size(), windowTitle.c_str());

    free(reply);

    if (settings.fullscreen)
    {
        xcb_intern_atom_reply_t *atom_wm_state = intern_atom_helper(connection, false, "_NET_WM_STATE");
        xcb_intern_atom_reply_t *atom_wm_fullscreen = intern_atom_helper(connection, false, "_NET_WM_STATE_FULLSCREEN");
        xcb_change_property(connection,
                XCB_PROP_MODE_REPLACE,
                window, atom_wm_state->atom,
                XCB_ATOM_ATOM, 32, 1,
                &(atom_wm_fullscreen->atom));
        free(atom_wm_fullscreen);
        free(atom_wm_state);
    }

    xcb_map_window(connection, window);

    return(window);
}

// Initialize XCB connection
void VulkanExampleBase::initxcbConnection()
{
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    int scr;

    connection = xcb_connect(NULL, &scr);
    if (connection == NULL) {
        printf("Could not find a compatible Vulkan ICD!\n");
        fflush(stdout);
        exit(1);
    }

    setup = xcb_get_setup(connection);
    iter = xcb_setup_roots_iterator(setup);
    while (scr-- > 0)
        xcb_screen_next(&iter);
    screen = iter.data;
}

void VulkanExampleBase::handleEvent(const xcb_generic_event_t *event)
{
    switch (event->response_type & 0x7f)
    {
    case XCB_CLIENT_MESSAGE:
        if ((*(xcb_client_message_event_t*)event).data.data32[0] ==
            (*atom_wm_delete_window).atom) {
            quit = true;
        }
        break;
    case XCB_MOTION_NOTIFY:
    {
        xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
        handleMouseMove((int32_t)motion->event_x, (int32_t)motion->event_y);
        break;
    }
    break;
    case XCB_BUTTON_PRESS:
    {
        xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
        switch (press->detail) {
        case XCB_BUTTON_INDEX_1:
            mouseButtons.left = true;
            break;
        case XCB_BUTTON_INDEX_2:
            mouseButtons.middle = true;
            break;
        case XCB_BUTTON_INDEX_3:
            mouseButtons.right = true;
            break;
        case XCB_BUTTON_INDEX_4://wheel scroll up
            camera.translate(glm::vec3(0.0f, 0.0f, camera.zoomSpeed));
            viewUpdated = true;
            break;
        case XCB_BUTTON_INDEX_5://wheel scroll down
            camera.translate(glm::vec3(0.0f, 0.0f, -camera.zoomSpeed));
            viewUpdated = true;
            break;
        }
        handleMouseButtonDown(press->detail);
    }
    break;
    case XCB_BUTTON_RELEASE:
    {
        xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
        if (press->detail == XCB_BUTTON_INDEX_1)
            mouseButtons.left = false;
        if (press->detail == XCB_BUTTON_INDEX_2)
            mouseButtons.middle = false;
        if (press->detail == XCB_BUTTON_INDEX_3)
            mouseButtons.right = false;
        handleMouseButtonUp(press->detail);
    }
    break;
    case XCB_KEY_PRESS:
    {
        const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
        switch (keyEvent->detail)
        {
            case KEY_W:
                camera.keys.up = true;
                break;
            case KEY_S:
                camera.keys.down = true;
                break;
            case KEY_A:
                camera.keys.left = true;
                break;
            case KEY_D:
                camera.keys.right = true;
                break;
            case KEY_P:
                paused = !paused;
                break;
        }
        keyDown(keyEvent->detail);
    }
    break;
    case XCB_KEY_RELEASE:
    {
        const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
        switch (keyEvent->detail)
        {
            case KEY_W:
                camera.keys.up = false;
                break;
            case KEY_S:
                camera.keys.down = false;
                break;
            case KEY_A:
                camera.keys.left = false;
                break;
            case KEY_D:
                camera.keys.right = false;
                break;
            case KEY_ESCAPE:
                quit = true;
                break;
        }
        keyUp(keyEvent->detail);
        keyPressed(keyEvent->detail);
    }
    break;
    case XCB_DESTROY_NOTIFY:
        quit = true;
        break;
    case XCB_CONFIGURE_NOTIFY:
    {
        const xcb_configure_notify_event_t *cfgEvent = (const xcb_configure_notify_event_t *)event;
        if ((prepared) && ((cfgEvent->width != width) || (cfgEvent->height != height)))
        {
                destWidth = cfgEvent->width;
                destHeight = cfgEvent->height;
                if ((destWidth > 0) && (destHeight > 0))
                {
                    windowResize();
                }
        }
    }
    break;
    default:
        break;
    }
}
#endif

void VulkanExampleBase::viewChanged() {
    updateUniformBuffers();
}

void VulkanExampleBase::prepareUniformBuffers()
{
    // Objact vertex shader uniform buffer
    sharedUBOs.matrices.create(vulkanDevice,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        sizeof(mvpMatrices));

    // Shared parameter uniform buffer
    sharedUBOs.params.create(vulkanDevice,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        sizeof(lightingParams));

    // Map persistent
    sharedUBOs.matrices.map();
    sharedUBOs.params.map();

    updateUniformBuffers();
    updateParams();
}

void VulkanExampleBase::updateUniformBuffers()
{
    // 3D object
    mvpMatrices.projection = camera.matrices.perspective;
    mvpMatrices.view = camera.matrices.view;
    mvpMatrices.view3 = glm::mat4(glm::mat3(camera.matrices.view));
    mvpMatrices.camPos = camera.position * -1.0f;
    sharedUBOs.matrices.copyTo (&mvpMatrices, sizeof(mvpMatrices));
}

void VulkanExampleBase::updateParams()
{
    lightingParams.lightDir = glm::vec4(-10.0f, 150.f, -10.f, 1.0f);
    sharedUBOs.params.copyTo(&lightingParams, sizeof(lightingParams));
}


void VulkanExampleBase::keyDown(uint32_t) {}
void VulkanExampleBase::keyUp(uint32_t) {}
void VulkanExampleBase::keyPressed(uint32_t key) {
#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
    switch (key) {
    case KEY_F1:
        if (lightingParams.exposure > 0.1f) {
            lightingParams.exposure -= 0.1f;
            updateParams();
            std::cout << "Exposure: " << lightingParams.exposure << std::endl;
        }
        break;
    case KEY_F2:
        if (lightingParams.exposure < 10.0f) {
            lightingParams.exposure += 0.1f;
            updateParams();
            std::cout << "Exposure: " << lightingParams.exposure << std::endl;
        }
        break;
    case KEY_F3:
        if (lightingParams.gamma > 0.1f) {
            lightingParams.gamma -= 0.1f;
            updateParams();
            std::cout << "Gamma: " << lightingParams.gamma << std::endl;
        }
        break;
    case KEY_F4:
        if (lightingParams.gamma < 10.0f) {
            lightingParams.gamma += 0.1f;
            updateParams();
            std::cout << "Gamma: " << lightingParams.gamma << std::endl;
        }
        break;
    }
#endif
}


void VulkanExampleBase::createRenderTarget () {
    if (settings.multiSampling) {

        multisampleTarget.color.create (vulkanDevice,
                                        VK_IMAGE_TYPE_2D, swapChain.colorFormat, width, height,
                                        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, 1, 1, 0,
                                        settings.sampleCount);
        multisampleTarget.color.createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT,1,1);

        multisampleTarget.depth.create (vulkanDevice,
                                        VK_IMAGE_TYPE_2D, depthFormat, width, height,
                                        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, 1, 1, 0,
                                        settings.sampleCount);
        multisampleTarget.depth.createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
    }

    depthStencil.create (vulkanDevice,
                                    VK_IMAGE_TYPE_2D, depthFormat, width, height,
                                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    depthStencil.createView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
}

void VulkanExampleBase::setupFrameBuffer()
{
    VkImageView attachments[] = {
        multisampleTarget.color.view,
        multisampleTarget.depth.view
    };

    VkFramebufferCreateInfo frameBufferCI = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    frameBufferCI.renderPass        = renderPass;
    frameBufferCI.attachmentCount   = 2;
    frameBufferCI.pAttachments      = attachments;
    frameBufferCI.width             = width;
    frameBufferCI.height            = height;
    frameBufferCI.layers            = 1;
    VK_CHECK_RESULT(vkCreateFramebuffer(vulkanDevice->dev, &frameBufferCI, nullptr, &frameBuffer));
}


void VulkanExampleBase::windowResize()
{
    if (!prepared) {
        return;
    }
    prepared = false;

    vkDeviceWaitIdle(vulkanDevice->dev);
    width = destWidth;
    height = destHeight;
    setupSwapChain();

    depthStencil.destroy();
    if (settings.multiSampling) {
        multisampleTarget.color.destroy();
        multisampleTarget.depth.destroy();
    }
    vkDestroyFramebuffer(vulkanDevice->dev, frameBuffer, nullptr);

    setupFrameBuffer();

    vkDeviceWaitIdle(vulkanDevice->dev);

    camera.updateAspectRatio((float)width / (float)height);
    viewChanged();

    prepared = true;
}

void VulkanExampleBase::handleMouseMove(int32_t x, int32_t y)
{
    int32_t dx = (int32_t)mousePos.x - x;
    int32_t dy = (int32_t)mousePos.y - y;

    bool handled = false;

    if (handled) {
        mousePos = glm::vec2((float)x, (float)y);
        return;
    }

    /*if (mouseButtons.left) {
        camera.rotate(glm::vec3(dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
        viewUpdated = true;
    }*/
    if (mouseButtons.right) {
        //camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f * camera.movementSpeed));
        camera.rotate(glm::vec3(-dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
        viewUpdated = true;
    }
    if (mouseButtons.middle) {
        camera.translate(glm::vec3(-dx * 0.01f, -dy * 0.01f, 0.0f));
        viewUpdated = true;
    }
    mousePos = glm::vec2((float)x, (float)y);
}

void VulkanExampleBase::handleMouseButtonDown(int buttonIndex) {}

void VulkanExampleBase::handleMouseButtonUp(int buttonIndex) {}

void VulkanExampleBase::initSwapchain()
{
#if defined(_WIN32)
    swapChain.initSurface(windowInstance, window);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    swapChain.initSurface(androidApp->window);
#elif defined(_DIRECT2DISPLAY)
    swapChain.initSurface(width, height);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    swapChain.initSurface(display, surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    swapChain.initSurface(connection, window);
#endif
}

void VulkanExampleBase::setupSwapChain()
{
    swapChain.create(&width, &height, settings.vsync);
}
