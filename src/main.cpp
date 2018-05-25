/*
* Vulkan Example - Physical based rendering a glTF 2.0 model (metal/roughness workflow) with image based lighting
*
* Note: Requires the separate asset pack (see data/README.md)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
* Copyright (C) 2018 by jp_bruyere@hotmail.com (instanced rendering with texture array and material ubo)
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

// PBR reference: http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// glTF format: https://github.com/KhronosGroup/glTF
// tinyglTF loader: https://github.com/syoyo/tinygltf

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <chrono>

#include "vkpbrrenderer.h"

/*
    PBR example main class
*/
class VulkanExample : public vkPbrRenderer
{
public:


    VulkanExample() : vkPbrRenderer()
    {
        title = "Vulkan glTf 2.0 PBR";
        camera.type = Camera::CameraType::firstperson;
        camera.movementSpeed = 8.0f;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
        camera.rotationSpeed = 0.25f;
        camera.setRotation({ -32.0f, 0.0f, 0.0f });
        camera.setPosition({ .05f, 6.31f, -10.85f });
    }

    ~VulkanExample()
    {

    }


    virtual void loadAssets() {
        vkPbrRenderer::loadAssets();

        models.object.loadFromFile("/home/jp/gltf/chess/blend.gltf", vulkanDevice, queue, true);
        models.object.addInstance("Plane", glm::translate(glm::mat4(1.0),       glm::vec3( 0,0,0)));
        models.object.addInstance("frame", glm::translate(glm::mat4(1.0),       glm::vec3( 0,0,0)));

        models.object.addInstance("white_rook", glm::translate(glm::mat4(1.0),  glm::vec3(-7,0, 7)));
        models.object.addInstance("white_rook", glm::translate(glm::mat4(1.0),  glm::vec3( 7,0, 7)));
        models.object.addInstance("white_knight", glm::translate(glm::mat4(1.0),glm::vec3(-5,0, 7)));
        models.object.addInstance("white_knight", glm::translate(glm::mat4(1.0),glm::vec3( 5,0, 7)));
        models.object.addInstance("white_bishop", glm::translate(glm::mat4(1.0),glm::vec3(-3,0, 7)));
        models.object.addInstance("white_bishop", glm::translate(glm::mat4(1.0),glm::vec3( 3,0, 7)));
        models.object.addInstance("white_queen", glm::translate(glm::mat4(1.0), glm::vec3( 1,0, 7)));
        models.object.addInstance("white_king", glm::translate(glm::mat4(1.0),  glm::vec3(-1,0, 7)));

        for (int i=0; i<8; i++){
            models.object.addInstance("white_pawn", glm::translate(glm::mat4(1.0),  glm::vec3(i*2-7,0, 5)));
            models.object.addInstance("black_pawn", glm::translate(glm::mat4(1.0),  glm::vec3(i*2-7,0, -5)));
        }

        models.object.addInstance("black_rook", glm::translate(glm::mat4(1.0),  glm::vec3(-7,0,-7)));
        models.object.addInstance("black_rook", glm::translate(glm::mat4(1.0),  glm::vec3( 7,0,-7)));
        models.object.addInstance("black_knight", glm::translate(glm::mat4(1.0),glm::vec3(-5,0,-7)));
        models.object.addInstance("black_knight", glm::translate(glm::mat4(1.0),glm::vec3( 5,0,-7)));
        models.object.addInstance("black_bishop", glm::translate(glm::mat4(1.0),glm::vec3(-3,0,-7)));
        models.object.addInstance("black_bishop", glm::translate(glm::mat4(1.0),glm::vec3( 3,0,-7)));
        models.object.addInstance("black_queen", glm::translate(glm::mat4(1.0), glm::vec3( 1,0,-7)));
        models.object.addInstance("black_king", glm::translate(glm::mat4(1.0),  glm::vec3(-1,0,-7)));    }
};

VulkanExample *vulkanExample;

// OS specific macros for the example main entry points
#if defined(_WIN32)
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (vulkanExample != NULL)
    {
        vulkanExample->handleMessages(hWnd, uMsg, wParam, lParam);
    }
    return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    for (int32_t i = 0; i < __argc; i++) { VulkanExample::args.push_back(__argv[i]); };
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->setupWindow(hInstance, WndProc);
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    return 0;
}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
// Android entry point
// A note on app_dummy(): This is required as the compiler may otherwise remove the main entry point of the application
void android_main(android_app* state)
{
    app_dummy();
    vulkanExample = new VulkanExample();
    state->userData = vulkanExample;
    state->onAppCmd = VulkanExample::handleAppCommand;
    state->onInputEvent = VulkanExample::handleAppInput;
    androidApp = state;
    vks::android::getDeviceConfig();
    vulkanExample->renderLoop();
    delete(vulkanExample);
}
#elif defined(_DIRECT2DISPLAY)
// Linux entry point with direct to display wsi
static void handleEvent()
{
}
int main(const int argc, const char *argv[])
{
    for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    return 0;
}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
int main(const int argc, const char *argv[])
{
    for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->setupWindow();
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    return 0;
}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
static void handleEvent(const xcb_generic_event_t *event)
{
    if (vulkanExample != NULL)
    {
        vulkanExample->handleEvent(event);
    }
}
int main(const int argc, const char *argv[])
{
    for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->setupWindow();
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    return 0;
}
#endif
