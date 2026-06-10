# VulkanEngine

A lightweight 2D game engine / graphical application framework in C++20 on Vulkan,
with a minimal Unity/GameMaker-inspired editor (Dear ImGui, docking).

## Dependencies (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y build-essential cmake git \
    libvulkan-dev vulkan-tools vulkan-validationlayers spirv-tools glslang-tools \
    libglfw3-dev libglm-dev
```

(Dear ImGui and stb_image are fetched automatically by CMake.)

## Build & Run

```bash
cmake -B build
cmake --build build -j$(nproc)
cd build && ./VulkanEngine        # run from build/ so shaders/ resolves
```

## Layout

```
shaders/        GLSL sources, compiled to SPIR-V at build time
src/
  core/         Application (engine facade), Window, Input, common utils
  vk/           Vulkan backend: context/device, swapchain, pipeline, texture
  render/       Batched 2D quad renderer + camera
  ecs/          Scene registry, Entity handle, components, script base class
  editor/       ImGui layer + editor panels (hierarchy, inspector, engine)
  main.cpp      Demo application
```

## Quick API tour

```cpp
Application app({ .Name = "My Game" });
Entity e = app.GetScene().CreateEntity("Player");
e.Add<SpriteComponent>().Tex = Texture::Create("player.png");
e.Add<NativeScriptComponent>().Bind<MyScript>();   // MyScript : ScriptableEntity
app.Run();
```

- **F1** toggles the editor. The Engine panel switches between a continuous
  game loop and event-driven rendering (redraw only on input — for tools/apps).
- Custom shaders: compile GLSL to SPIR-V, then
  `renderer.SetCustomPipeline(std::make_shared<Pipeline>("my.vert.spv", "my.frag.spv", renderer.GetRenderPass()))`.
  Vertex inputs and the push-constant view-projection matrix must match
  `src/render/Vertex.h`.
- Full control: `app.SetCustomRenderCallback([](VkCommandBuffer cmd){ ... })`
  records raw Vulkan inside the active render pass, after the sprite pass.
