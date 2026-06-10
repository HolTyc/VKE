# VKE — Lightweight Vulkan 3D Engine

A minimal, high-performance 3D engine and application framework for Linux:
C++20, Vulkan, GLFW, GLM, Dear ImGui, tinyobjloader. Designed as a clean
foundation — no visual scripting, no shader graphs, no timelines.

## Setup (Debian/Ubuntu)

```bash
./setup.sh
```

or manually:

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
    libvulkan-dev vulkan-tools vulkan-validationlayers \
    glslang-tools spirv-tools libglfw3-dev libglm-dev
```

Dear ImGui and tinyobjloader are fetched automatically by CMake (FetchContent).

## Build & run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/sandbox
```

Use `-DCMAKE_BUILD_TYPE=Debug` to enable Vulkan validation layers.

## Directory structure

```
.
├── CMakeLists.txt           # build + GLSL→SPIR-V compilation + FetchContent deps
├── setup.sh                 # apt dependency installer
├── app/
│   └── main.cpp             # demo application (the "user code")
├── assets/
│   └── shaders/             # GLSL sources, compiled to build/assets/shaders/*.spv
│       ├── basic.vert/.frag # default Blinn-Phong forward shader
│       └── toon.vert/.frag  # example custom user shader
└── engine/
    ├── include/vke/         # public API
    │   ├── vke.hpp          # single include for engine users
    │   ├── Application.hpp  # engine entry point + game loop / event loop
    │   ├── Window.hpp       # GLFW wrapper
    │   ├── Renderer3D.hpp   # forward renderer facade
    │   ├── Scene.hpp        # entity container
    │   ├── Entity.hpp       # component holder
    │   ├── Components.hpp   # Transform, Camera, Light, MeshRenderer, Material
    │   ├── Mesh.hpp         # GPU mesh + OBJ loader + primitives
    │   ├── EditorGUI.hpp    # ImGui editor layer
    │   └── vulkan/          # backend (advanced users only)
    │       ├── Context.hpp  # instance/device/queues/helpers
    │       ├── Swapchain.hpp# swapchain + depth + render pass + sync
    │       └── Pipeline.hpp # graphics pipeline from SPIR-V pair
    └── src/                 # implementations (core/, vulkan/, scene/, render/, editor/)
```

## Quick start

```cpp
#include <vke/vke.hpp>

class MyApp : public vke::Application {
    using vke::Application::Application;

    void onStart() override {
        auto& cam = scene().createEntity("Camera");
        cam.transform().position = {0, 2, 6};
        cam.add<vke::CameraComponent>();

        auto& sun = scene().createEntity("Sun");
        sun.transform().rotation = {-50, 30, 0};
        sun.add<vke::LightComponent>().type = vke::LightComponent::Type::Directional;

        auto& cube = scene().createEntity("Cube");
        auto& mr = cube.add<vke::MeshRendererComponent>();
        mr.mesh = renderer().primitive(vke::Primitive::Cube);   // or loadModel("models/x.obj")
        mr.material.albedo = {1, 0.4, 0.1, 1};
    }

    void onUpdate(float dt) override { /* game logic */ }
};

int main() {
    vke::AppConfig cfg{.title = "My App", .mode = vke::RenderMode::Continuous};
    MyApp app(cfg);
    app.run();
}
```

## Key concepts

- **Render modes** — `RenderMode::Continuous` is a classic vsynced game loop.
  `RenderMode::OnDemand` blocks on OS events and renders only when input
  arrives (ideal for CAD-style viewers; idle CPU/GPU usage is ~zero). Switch at
  runtime via `setRenderMode()` or the editor's *Rendering* menu; force a frame
  with `requestRedraw()`.
- **Editor** — hierarchy (create/delete entities), inspector (Transform,
  Material, Light, Camera properties), stats overlay. Hold **RMB** and use
  **WASD/QE** (Shift = fast) to fly the camera. Disable entirely with
  `AppConfig::editor = false`.
- **Custom shaders** — drop `myshader.vert`/`myshader.frag` into
  `assets/shaders/` (same UBO/push-constant interface as `basic.*`), rebuild,
  then:
  ```cpp
  renderer().registerShader("myshader", "shaders/myshader.vert.spv",
                                        "shaders/myshader.frag.spv");
  material.shader = "myshader";
  ```
- **Advanced backend access** — `renderer().context()`, `.swapchain()`,
  `.currentCommandBuffer()` and `.pipelineLayout()` expose the raw Vulkan
  objects for custom passes between `beginFrame()` and `endFrame()`.

## Shader interface (set 0, binding 0 + push constants)

All scene shaders receive a global UBO (view/proj matrices, camera position,
ambient color, up to 8 lights) and per-draw push constants (model matrix,
albedo, shininess/specular). See `assets/shaders/basic.vert` for the exact
declarations — copy them into new shaders as a starting point.
