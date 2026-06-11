# VKE — Vulkan Engines

Two lightweight C++20/Vulkan engines for Linux, each with its own build and
games. They are independent — pick the directory for what you want to work
on.

## [VulkanEngine3D/](VulkanEngine3D/)

A 3D engine (forward Blinn-Phong renderer, ECS scene, ImGui editor,
post-processing) plus games built on it (`projects/sandbox`, `projects/backrooms`
— "The Backrooms, Level 0").

```bash
cd VulkanEngine3D
./setup.sh                                       # install dependencies (Debian/Ubuntu)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/sandbox      # or ./build/backrooms
```

See [`VulkanEngine3D/README.md`](VulkanEngine3D/README.md) for build options
(debug/validation layers, adding new projects) and
[`VulkanEngine3D/CLAUDE.md`](VulkanEngine3D/CLAUDE.md) for the engine API.

## [VulkanEngine/](VulkanEngine/)

A 2D engine (batched quad renderer, ECS, native C++ scripting, Unity-style
ImGui editor).

```bash
cd VulkanEngine
sudo apt install -y build-essential cmake git \
    libvulkan-dev vulkan-tools vulkan-validationlayers spirv-tools glslang-tools \
    libglfw3-dev libglm-dev
cmake -B build && cmake --build build -j$(nproc)
cd build && ./VulkanEngine        # must run from build/ — shaders are loaded by relative path
```

See [`VulkanEngine/README.md`](VulkanEngine/README.md) for the API and layout,
and [`VulkanEngine/CLAUDE.md`](VulkanEngine/CLAUDE.md) for the full engine
reference.

## Common dependencies

Both engines need a Vulkan-capable GPU/driver, CMake ≥ 3.21, and:
`build-essential cmake git libvulkan-dev vulkan-tools vulkan-validationlayers
glslang-tools spirv-tools libglfw3-dev libglm-dev`. Dear ImGui and other small
dependencies are fetched automatically by CMake (`FetchContent`) on first
configure (needs network access).
