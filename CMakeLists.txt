CMAKE_MINIMUM_REQUIRED (VERSION 3.0)
PROJECT(vkChess VERSION 0.1.0 DESCRIPTION "Vulkan Stockfish client")

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
SET(CMAKE_CXX_FLAGS "-W -Wall -Wno-missing-field-initializers")
SET(CMAKE_EXE_LINKER_FLAGS "-lm")

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE Debug)
ENDIF()
MESSAGE(STATUS "${CMAKE_BUILD_TYPE} build.")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	ADD_DEFINITIONS (-DDEBUG)

ENDIF()

FIND_PATH(GLM_SUBMODULE_DIR external/glm)
if (NOT GLM_SUBMODULE_DIR)
	EXECUTE_PROCESS(COMMAND git submodule update --depth -init --recursive
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

set(VULKAN_SDK "$ENV{VULKAN_SDK}" CACHE STRING "LunarG Vulkan SDK path")
if (VULKAN_SDK)
		set(ENV{VULKAN_SDK} ${VULKAN_SDK})
		SET(ENV{VK_LAYER_PATH} "${VULKAN_SDK}/etc/explicit_layer.d")
endif ()

MESSAGE(STATUS "VULKAN_SDK = $ENV{VULKAN_SDK}")
MESSAGE(STATUS "VK_LAYER_PATH = $ENV{VK_LAYER_PATH}")

include_directories(external/glm)
include_directories(external/gli)
include_directories(external/glfw)
include_directories(base)

SET(VKVG_BUILD_TESTS off)
add_subdirectory	(external/vkvg)
include_directories	(external/vkvg/include)


OPTION(USE_D2D_WSI "Build the project using Direct to Display swapchain" OFF)
OPTION(USE_WAYLAND_WSI "Build the project using Wayland swapchain" OFF)

set(RESOURCE_INSTALL_DIR "" CACHE PATH "Path to install resources to (leave empty for running uninstalled)")

# Use FindVulkan module added with CMAKE 3.7
if (NOT CMAKE_VERSION VERSION_LESS 3.7.0)
	message(STATUS "Using module to find Vulkan")
	FIND_PACKAGE(Vulkan)
endif()

IF(WIN32)
	IF (NOT Vulkan_FOUND)
		FIND_LIBRARY(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS ${CMAKE_SOURCE_DIR}/libs/vulkan)
		IF (Vulkan_LIBRARY)
			set(Vulkan_FOUND ON)
			MESSAGE("Using bundled Vulkan library version")
		ENDIF()
	ENDIF()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
ELSE(WIN32)
	IF (NOT Vulkan_FOUND)
		FIND_LIBRARY(Vulkan_LIBRARY NAMES vulkan HINTS "${VULKAN_SDK}/lib" "${CMAKE_SOURCE_DIR}/libs/vulkan" REQUIRED)
		IF (Vulkan_LIBRARY)
			set(Vulkan_FOUND ON)
			MESSAGE("Using bundled Vulkan library version")
		ENDIF()
	ENDIF()
	find_package(Threads REQUIRED)
IF(USE_D2D_WSI)
	MESSAGE("Using direct to display extension...")
	add_definitions(-D_DIRECT2DISPLAY)
ELSEIF(USE_WAYLAND_WSI)
	find_package(Wayland REQUIRED)
	if (NOT WAYLAND_FOUND)
		message(FATAL_ERROR "Wayland development package not found")
	endif ()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WAYLAND_KHR")
	include_directories(${WAYLAND_INCLUDE_DIR})
ELSE(USE_D2D_WSI)
	find_package(XCB REQUIRED)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR")
ENDIF(USE_D2D_WSI)
	# Todo : android?
ENDIF(WIN32)

IF (NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan library!")
ENDIF()

# Set preprocessor defines
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX -D_USE_MATH_DEFINES")

# Clang specific stuff
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch-enum")
endif()

add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-std=c++11 -g)

find_package (Bullet REQUIRED)

if(RESOURCE_INSTALL_DIR)
	add_definitions(-DDATA_DIR=\"${RESOURCE_INSTALL_DIR}/\")
	install(DIRECTORY data/ DESTINATION ${RESOURCE_INSTALL_DIR}/)
else()
	add_definitions(-DDATA_DIR=\"${CMAKE_SOURCE_DIR}/data/\")
endif()

# Compiler specific stuff
IF(MSVC)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
ENDIF(MSVC)

IF(WIN32)
	# Nothing here (yet)
ELSE(WIN32)
	link_libraries(${XCB_LIBRARIES} ${Vulkan_LIBRARY} ${Vulkan_LIBRARY}
		${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${BULLET_LIBRARIES})
ENDIF(WIN32)

# Find glslc shader compiler.
# On Android, the NDK includes the binary, so no external dependency.
if(ANDROID)
	file(GLOB glslc-folders ${ANDROID_NDK}/shader-tools/*)
else()
	file(GLOB glslc-folders ${VULKAN_SDK}/bin)
endif()
FIND_PROGRAM(GLSLC glslc HINTS ${glslc-folders})

file(GLOB_RECURSE DATAS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "data/*")
FOREACH(DATA_FILE ${DATAS})
	GET_FILENAME_COMPONENT(copy-dest-dir ${CMAKE_CURRENT_BINARY_DIR}/${DATA_FILE} DIRECTORY)
	SET(copy-output ${CMAKE_CURRENT_BINARY_DIR}/${DATA_FILE})
	ADD_CUSTOM_COMMAND(
	   OUTPUT  ${copy-output}
	   COMMAND ${CMAKE_COMMAND} -E make_directory ${copy-dest-dir}
	   COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${DATA_FILE}" "${copy-output}"
	   COMMENT "Copying ${DATA_FILE} to ${copy-output}"
	   DEPENDS ${DATA_FILE}
	   VERBATIM
	)
ENDFOREACH()
add_custom_target(DataCopy ALL DEPENDS ${DATAS})

IF (NOT GLSLC)
	message(FATAL_ERROR "Could not find glslc executable")
ENDIF()

if(GLSLC)
	SET(SHADER_DIR "shaders")
	SET(SHADER_FILES ${SHADER_DIR}/*.frag ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.geom  ${SHADER_DIR}/*.comp)
	FILE(GLOB_RECURSE SHADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${SHADER_FILES})
	FOREACH(SHADER ${SHADERS})
		SET(shader-input ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
		SET(shader-output ${CMAKE_CURRENT_BINARY_DIR}/${SHADER}.spv)
		ADD_CUSTOM_COMMAND (
		  OUTPUT ${shader-output}
		  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_DIR}"
		  COMMAND ${GLSLC} ${shader-input} -o ${shader-output}
		  COMMENT "Compiling ${shader-input}"
		  DEPENDS ${SHADER}
		  VERBATIM
		)
		SET(SHADER_OUTPUTS ${SHADER_OUTPUTS} ${shader-output})
	ENDFOREACH()

	ADD_CUSTOM_TARGET("${PROJECT_NAME}_BuildShaderHeader" ALL DEPENDS ${SHADER_OUTPUTS})
endif()

add_subdirectory(base)

FILE(GLOB SOURCES src/*.cpp)

if(WIN32)
	ADD_EXECUTABLE(${PROJECT_NAME} WIN32 ${MAIN_CPP} ${SOURCES} ${SHADERS})
	#TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PRIVATE
		#${BULLET_INCLUDE_DIR}
	#)
	TARGET_LINK_LIBRARIES(${PROJECT_NAME}
		base
		${Vulkan_LIBRARY}
		${WINLIBS}
		#${BULLET_LIBRARIES}
	)
else(WIN32)
	ADD_EXECUTABLE(${PROJECT_NAME} ${MAIN_CPP} ${SOURCES} ${SHADERS})
	TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PRIVATE
		#${BULLET_INCLUDE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/base
	)
	TARGET_LINK_LIBRARIES(${PROJECT_NAME}
		base
		vkvg
		#{BULLET_LIBRARIES}
	)
endif(WIN32)

if(RESOURCE_INSTALL_DIR)
	install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
