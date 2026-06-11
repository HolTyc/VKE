# VKE — Lightweight Vulkan 3D Engine

A minimal, high-performance 3D engine and application framework for Linux:
C++20, Vulkan, GLFW, GLM, Dear ImGui, tinyobjloader. Designed as a clean
foundation — no visual scripting, no shader graphs, no timelines.

This repo contains the engine (`engine/`) plus a set of games/apps built on
top of it (`projects/`).

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

Dear ImGui, tinyobjloader and (for the Backrooms) miniaudio are fetched
automatically by CMake (`FetchContent`) — the first configure needs network
access.

## Build & run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Every project binary lands directly in `build/`:

```bash
./build/sandbox      # minimal example scene
./build/backrooms    # The Backrooms — Level 0
./build/vke-editor   # project editor with hot-reloaded C++ scripting
```

Use `-DCMAKE_BUILD_TYPE=Debug` (e.g. configure a separate `build-debug/`
directory) to enable Vulkan validation layers — recommended whenever you're
working on rendering code; validation errors print to stderr prefixed
`[vulkan]`.

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j$(nproc)
```

`run.sh` runs the configure/build/launch sequence for the sandbox in one go.

## Scripting

VKE editor projects support hot-reloaded C++ scripts in each project's
`scripts/` folder. Scripts attach to entities, expose Inspector properties, read
input, and run in Play mode.

See [docs/Scripting.md](docs/Scripting.md) for the full scripting guide with
examples.

## Projects

### sandbox

A minimal scene (`projects/sandbox/`) demonstrating the basic engine API —
a good starting point for new projects.

```bash
./build/sandbox
```

### backrooms — The Backrooms, Level 0

An endless first-person walk through the Backrooms (`projects/backrooms/`),
with procedurally generated levels, flickering lights, a flashlight and
synthesized ambient audio. See `projects/backrooms/README.md` for details.

```bash
./build/backrooms
```

**Controls:** mouse — look · WASD — walk · Shift — hurry · F — flashlight ·
Esc — pause menu (resume / fullscreen toggle / quit) ·
F1 — edit mode (engine editor over the live game; F1 or Esc returns to play)

## Adding a new project

```
projects/mygame/CMakeLists.txt   # just: vke_add_project(mygame)  (+ extra deps if any)
projects/mygame/src/*.cpp        # all sources, globbed recursively
projects/mygame/shaders/*.{vert,frag}   # optional, compiled to build/assets/shaders/mygame/
projects/mygame/assets/          # optional, copied to build/assets/mygame/
```

Then re-run `cmake -S . -B build` once (new project directories need a
reconfigure; new files inside an existing project do not).

## Directory structure

```
.
├── CMakeLists.txt           # build + GLSL→SPIR-V compilation + FetchContent deps
├── setup.sh                 # apt dependency installer
├── run.sh                   # configure + build + run sandbox
├── cmake/VkeProject.cmake   # vke_add_project() helper
├── assets/
│   └── shaders/             # engine GLSL sources, compiled to build/assets/shaders/*.spv
│       ├── basic.vert/.frag # default Blinn-Phong forward shader
│       └── toon.vert/.frag  # example custom shader
├── engine/
│   ├── include/vke/         # public API
│   │   ├── vke.hpp          # single include for engine users
│   │   ├── Application.hpp  # engine entry point + game loop / event loop
│   │   ├── Window.hpp       # GLFW wrapper
│   │   ├── Renderer3D.hpp   # forward renderer facade + post-processing
│   │   ├── Scene.hpp        # entity container
│   │   ├── Entity.hpp       # component holder
│   │   ├── Components.hpp   # Transform, Camera, Light, MeshRenderer, Material
│   │   ├── Mesh.hpp         # GPU mesh + OBJ loader + primitives
│   │   ├── EditorGUI.hpp    # ImGui editor layer
│   │   └── vulkan/          # backend (advanced users only)
│   │       ├── Context.hpp     # instance/device/queues/helpers
│   │       ├── Swapchain.hpp   # swapchain + depth + render pass + sync
│   │       ├── Pipeline.hpp    # graphics pipeline from SPIR-V pair
│   │       └── PostProcess.hpp # offscreen target + fullscreen filter pass
│   └── src/                 # implementations (core/, vulkan/, scene/, render/, editor/)
└── projects/
    ├── sandbox/             # minimal example app
    └── backrooms/           # The Backrooms — Level 0
```

## Quick start (writing a new app)

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

For the full engine API, conventions and gotchas (component reference, custom
shaders, post-processing, editor controls, etc.), see `CLAUDE.md`.
