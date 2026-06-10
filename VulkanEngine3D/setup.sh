#!/usr/bin/env bash
# VKE — dependency setup for Debian/Ubuntu-based distributions.
# Dear ImGui and tinyobjloader are fetched automatically by CMake (FetchContent),
# so only system-level packages are installed here.
set -e

sudo apt update
sudo apt install -y \
    build-essential cmake git pkg-config \
    libvulkan-dev vulkan-tools vulkan-validationlayers \
    glslang-tools spirv-tools \
    libglfw3-dev \
    libglm-dev

echo
echo "Verifying installation..."
vulkaninfo --summary | head -n 20 || echo "WARNING: vulkaninfo failed — check your GPU driver (mesa-vulkan-drivers or vendor driver)."
glslangValidator --version | head -n 1
echo
echo "Done. Build with:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build -j\$(nproc)"
echo "  ./build/sandbox"
