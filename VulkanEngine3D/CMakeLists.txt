cmake_minimum_required(VERSION 3.21)
project(VulkanEngine3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------- dependencies
find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm QUIET) # header-only; falls back to system include path

include(FetchContent)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE)

FetchContent_Declare(tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v2.0.0rc13
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(imgui tinyobjloader)

# Dear ImGui core + GLFW/Vulkan backends, built as a static library.
add_library(imgui_vulkan STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui_vulkan PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_vulkan PUBLIC Vulkan::Vulkan glfw)

# ------------------------------------------------------ shaders (GLSL -> SPIR-V)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/assets/shaders/*.vert
    ${CMAKE_SOURCE_DIR}/assets/shaders/*.frag)

set(SPIRV_OUTPUTS)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV ${CMAKE_BINARY_DIR}/assets/shaders/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets/shaders
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME}")
    list(APPEND SPIRV_OUTPUTS ${SPV})
endforeach()
add_custom_target(vke_shaders ALL DEPENDS ${SPIRV_OUTPUTS})

# ---------------------------------------------------------------- engine (vke)
file(GLOB_RECURSE VKE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/engine/src/*.cpp)

add_library(vke STATIC ${VKE_SOURCES})
target_include_directories(vke PUBLIC ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(vke PUBLIC Vulkan::Vulkan glfw imgui_vulkan tinyobjloader)
if(TARGET glm::glm)
    target_link_libraries(vke PUBLIC glm::glm)
endif()
target_compile_definitions(vke PUBLIC
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    VKE_ASSET_DIR="${CMAKE_BINARY_DIR}/assets")
add_dependencies(vke vke_shaders)

# ------------------------------------------------------------- game projects
# Every projects/<name>/CMakeLists.txt is added automatically; inside it call
# vke_add_project(<name>) — see cmake/VkeProject.cmake for the expected layout.
include(cmake/VkeProject.cmake)

file(GLOB VKE_PROJECT_LISTS ${CMAKE_SOURCE_DIR}/projects/*/CMakeLists.txt)
foreach(PROJECT_LIST ${VKE_PROJECT_LISTS})
    get_filename_component(PROJECT_DIR ${PROJECT_LIST} DIRECTORY)
    add_subdirectory(${PROJECT_DIR})
endforeach()
