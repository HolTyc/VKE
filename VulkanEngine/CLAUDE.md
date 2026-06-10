# CLAUDE.md — VulkanEngine

A lightweight C++20 2D game engine on Vulkan (Linux) with a batched quad renderer,
a barebones ECS, native C++ scripting, and a minimal Dear ImGui editor.
This file tells you how to build games with it. Read the "Hard rules" section
before writing any code.

## Build & run

```bash
cmake -B build && cmake --build build -j$(nproc)
cd build && ./VulkanEngine        # MUST run from build/ — shaders/*.spv are loaded by relative path
```

- First configure needs network (CMake FetchContent pulls Dear ImGui `v1.91.8-docking` and stb).
- System deps if missing: `sudo apt install -y build-essential cmake git libvulkan-dev vulkan-tools vulkan-validationlayers spirv-tools glslang-tools libglfw3-dev libglm-dev`
- All `src/**/*.cpp` are globbed into the single `VulkanEngine` executable. To make a game,
  edit `src/main.cpp` (or replace it) and add new headers/sources anywhere under `src/`.
  Re-run `cmake -B build` after adding files (glob uses CONFIGURE_DEPENDS, a plain rebuild
  usually picks them up, but reconfiguring is the safe path).
- Debug builds enable Vulkan validation layers automatically; warnings print to stderr
  prefixed `[vulkan]`. Treat any validation message your change introduces as a bug.

## Architecture map

```
src/Engine.h        ← single include for game code; include only this
src/core/           Application (facade + main loop), Window (GLFW), Input, Base.h (logging, VK_CHECK)
src/vk/             VulkanContext (device/queues/descriptors), Swapchain, Pipeline, Texture
src/render/         Renderer2D (batched quads, frame lifecycle), Camera2D, Vertex.h (QuadVertex contract)
src/ecs/            Scene (registry), Entity (handle), Components.h, ScriptableEntity (script base)
src/editor/         ImGuiLayer (backend), EditorLayer (Hierarchy / Inspector / Engine panels)
shaders/            sprite.vert/.frag — compiled to build/shaders/*.spv at build time
```

Frame flow (driven by `Application::Run`, you never call these yourself):
`Scene::OnUpdate(dt)` (scripts) → `Renderer2D::BeginFrame/BeginScene` → `Scene::OnRender`
(every Transform+Sprite entity) → custom render callback → ImGui/editor → `EndFrame`.

## Writing a game — the canonical pattern

```cpp
#include "Engine.h"

class PlayerController : public ScriptableEntity {
protected:
    void OnCreate() override { /* runs once, entity is valid here */ }
    void OnUpdate(float dt) override {
        auto& t = Get<TransformComponent>();
        if (Input::IsKeyDown(GLFW_KEY_D)) t.Position.x += 5.0f * dt;
        // spawn:    Entity e = GetEntity().GetScene()->CreateEntity("Bullet");
        // self-kill: GetEntity().Destroy();  // safe mid-update (snapshot iteration)
    }
    void OnDestroy() override {}
};

int main() {
    Application app({ .Name = "My Game", .Width = 1600, .Height = 900 });
    Scene& scene = app.GetScene();

    Entity player = scene.CreateEntity("Player");          // Tag + Transform auto-added
    player.Get<TransformComponent>().Position = { 0, 0 };  // world units, Y is UP
    auto& sprite = player.Add<SpriteComponent>();
    sprite.Tex = Texture::Create("assets/player.png");     // nullptr Tex => flat Color quad
    sprite.Color = { 1, 1, 1, 1 };                         // multiplies the texture
    player.Add<NativeScriptComponent>().Bind<PlayerController>(/* ctor args forwarded */);

    app.SetUpdateCallback([&](float dt) {                  // per-frame logic outside ECS
        if (Input::IsKeyDown(GLFW_KEY_ESCAPE)) app.Close();
    });

    app.Run();                                             // blocks until window closes
    return 0;
}
```

Component queries: `e.Has<T>()`, `e.TryGet<T>()` (nullptr if absent), `e.Remove<T>()`,
`scene.Each<TransformComponent, SpriteComponent>([](EntityID id, auto& t, auto& s){ ... });`

Camera: `app.GetCamera()` → `Camera2D { Position, Zoom, OrthoSize }`. OrthoSize is the
visible half-height in world units (default 5 ⇒ y ∈ [-5, 5] at zoom 1); width follows aspect.

Game state (score, level, etc.): plain C++ — keep it in main(), in script members, or in a
singleton you own. There is no serialization layer; scenes are built in code.

## Hard rules (violating these crashes or misrenders)

1. **Construction order in main():** create `Application` FIRST, then textures/entities.
   Everything holding GPU resources (e.g. `shared_ptr<Texture>` locals) must be destroyed
   before the Application — declaring them after `app` guarantees that. Never store
   textures in globals/statics.
2. **Rotation is radians** everywhere in code (`TransformComponent::Rotation`). Only the
   editor Inspector displays degrees.
3. **World space is Y-up**, origin at camera center by default. Quads are centered on
   `Position` with full extents `Scale` (a Scale of {1,1} is a 1×1 world-unit quad).
4. **Run from `build/`** or shader loading aborts at startup.
5. Don't call `Renderer2D::BeginFrame/EndFrame/BeginScene/EndScene` — `Application::Run`
   owns the frame. Game code draws by creating entities, not by calling DrawQuad
   (DrawQuad is only meaningful inside the scene pass).
6. Max 20,000 quads per frame (`Renderer2D::MaxQuads`); excess quads are dropped with a
   one-time warning. Batches flush on texture change — group sprites by texture (use one
   atlas) if draw-call count matters; interleaving N textures costs up to N draw calls.
7. `Texture::Create(path)` returns **nullptr** on a missing/unreadable file — check it.
   `Texture::Create(w, h, pixels)` takes tightly-packed RGBA8.
8. One `Application` per process (singleton, `Application::Get()`).

## Engine behavior you can rely on

- **Render modes:** `RenderMode::Continuous` (vsynced game loop, the default) or
  `RenderMode::EventDriven` (redraws only on input/`RequestRedraw()` — use for tool-style
  apps). Set via `ApplicationConfig::Mode` or `app.SetRenderMode()` at runtime.
- **Editor:** F1 toggles. Hierarchy (create/destroy/select), Inspector (edit Tag/Transform/
  Sprite, add Sprite component), Engine panel (FPS, quad/draw-call stats, play-pause for
  scripts, render mode, clear color, camera). `ApplicationConfig{ .ShowEditor = false }`
  ships without it. "Pause Scripts" stops `Scene::OnUpdate` but the update callback still runs.
- **Scripts** are instantiated lazily on the first `Scene::OnUpdate` after binding (not at
  Bind time), so `OnCreate` won't run if the app starts paused (`.StartPlaying = false`).
- **dt** is clamped to 0.1 s after stalls; scripts can assume sane dt.
- Input is polling-only (`Input::IsKeyDown/IsMouseButtonDown/GetMousePosition`).
  GLFW key constants; mouse position is in window pixels, NOT world units. To convert,
  invert the camera mapping manually (no helper exists yet).
- Logging: `ENGINE_INFO/WARN/ERROR(fmt, ...)` printf-style from `core/Base.h`.

## Advanced rendering (only when asked for custom visuals)

- **Custom sprite shader for all batches:** write GLSL matching the contract below, add the
  files to `SHADER_SOURCES` in CMakeLists.txt, then:
  `app.GetRenderer().SetCustomPipeline(std::make_shared<Pipeline>("shaders/my.vert.spv", "shaders/my.frag.spv", app.GetRenderer().GetRenderPass()));`
  Contract (see `src/render/Vertex.h`): vertex inputs `vec2 inPosition(0)`, `vec2 inUV(1)`,
  `vec4 inColor(2)`; a 64-byte vertex-stage push constant (mat4 view-proj); descriptor
  set 0 binding 0 = combined image sampler. Fragment stage is otherwise free.
- **Raw Vulkan pass:** `app.SetCustomRenderCallback([](VkCommandBuffer cmd){ ... })` records
  after the sprite pass, inside the active render pass (1 color attachment, no depth,
  dynamic viewport/scissor already set). Build pipelines against
  `app.GetRenderer().GetRenderPass()`.
- There is no depth buffer: draw order = entity creation order. For layering, create
  background entities first (or sort by a convention you add).

## Extending the engine itself

- New component: add a struct to `src/ecs/Components.h` — no registration needed, pools are
  created on first use. To make it editable, add a block in `EditorLayer::DrawInspector`
  and an entry in the "Add Component" popup.
- New per-entity behavior that needs rendering: prefer composing Transform+Sprite via
  scripts over touching the renderer.
- Match the existing style: `m_` members, `VkStruct s{ VK_STRUCTURE_TYPE_... }` partial
  init (the `-Wmissing-field-initializers` suppression in CMakeLists exists for this),
  `VK_CHECK` on every Vulkan call that returns VkResult.
- Known intentional omissions (do NOT add unless the user asks): scene serialization,
  asset browser, animation system, audio, physics, texture atlas packing, mouse picking
  in the editor.
