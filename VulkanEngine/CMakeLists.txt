cmake_minimum_required(VERSION 3.20)
project(VulkanEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ------------------------------------------------------------------
# System dependencies (install via apt, see README)
# ------------------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)

# ------------------------------------------------------------------
# Fetched dependencies: Dear ImGui (docking branch) + stb_image
# ------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8-docking
)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(imgui stb)

# ImGui has no CMakeLists of its own — build it as a static lib.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

# ------------------------------------------------------------------
# Engine executable
# ------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(VulkanEngine ${ENGINE_SOURCES})

target_include_directories(VulkanEngine PRIVATE src ${stb_SOURCE_DIR})
target_link_libraries(VulkanEngine PRIVATE Vulkan::Vulkan glfw glm::glm imgui)
target_compile_definitions(VulkanEngine PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLFW_INCLUDE_VULKAN
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -Wno-missing-field-initializers: VkStruct s{ STYPE } zero-inits the rest by design
    target_compile_options(VulkanEngine PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# ------------------------------------------------------------------
# Shader compilation (GLSL -> SPIR-V) at build time
# ------------------------------------------------------------------
find_program(GLSLC glslc)
find_program(GLSLANG_VALIDATOR glslangValidator)

set(SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/shaders/sprite.vert
    ${CMAKE_SOURCE_DIR}/shaders/sprite.frag
)
set(SPIRV_OUTPUTS "")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(FNAME ${SHADER} NAME)
    set(SPV ${CMAKE_BINARY_DIR}/shaders/${FNAME}.spv)
    if(GLSLC)
        add_custom_command(OUTPUT ${SPV}
            COMMAND ${GLSLC} ${SHADER} -o ${SPV}
            DEPENDS ${SHADER} COMMENT "Compiling ${FNAME}")
    elseif(GLSLANG_VALIDATOR)
        add_custom_command(OUTPUT ${SPV}
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPV}
            DEPENDS ${SHADER} COMMENT "Compiling ${FNAME}")
    else()
        message(FATAL_ERROR "No GLSL compiler found. Install 'glslc' or 'glslang-tools'.")
    endif()
    list(APPEND SPIRV_OUTPUTS ${SPV})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPIRV_OUTPUTS})
add_dependencies(VulkanEngine CompileShaders)
