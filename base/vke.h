#pragma once


#include <chrono>
#include <sys/stat.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <exception>
#include <assert.h>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <array>
#include <vector>
#include <numeric>
#include <algorithm>

#include "vulkan/vulkan.h"

#include "GLFW/glfw3.h"

#include "macros.h"
#include "camera.hpp"
#include "keycodes.hpp"



namespace vks {
    typedef class  VkEngine*		ptrVkEngine;
    typedef class  VulkanSwapChain* ptrSwapchain;
    typedef struct VulkanDevice*	ptrVkDev;
    typedef struct Texture*			ptrTexture;
}


