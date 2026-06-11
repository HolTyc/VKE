# CLAUDE.md — VKE (Vulkan 3D Engine)

VKE is a lightweight C++20/Vulkan 3D engine for Linux. This file is for agents
**building games/apps on top of the engine**. Engine internals live in
`engine/src/` — you rarely need to touch them; the public API is
`engine/include/vke/` and almost everything goes through `vke::Application`,
`vke::Scene` and `vke::Renderer3D`.

## Build, run, iterate

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # first time only (FetchContent needs network)
cmake --build build -j$(nproc)
./build/sandbox        # or ./build/backrooms — every project binary lands in build/
```

- Games live in `projects/<name>/` — one directory per game, auto-discovered by
  the root CMakeLists. To add a new game create:
  ```
  projects/mygame/CMakeLists.txt   # just: vke_add_project(mygame)  (+ extra deps if any)
  projects/mygame/src/*.cpp        # all sources, globbed recursively
  projects/mygame/shaders/*.{vert,frag}   # optional, compiled to build/assets/shaders/mygame/
  projects/mygame/assets/          # optional, copied to build/assets/mygame/
  ```
  then re-run `cmake -S . -B build` once (new project dirs need a reconfigure;
  new files inside an existing project do not). Register project shaders as
  `renderer().registerShader("foo", "shaders/mygame/foo.vert.spv", ...)`.
  See `cmake/VkeProject.cmake` for the helper, `projects/sandbox/` for a minimal
  example and `projects/backrooms/` for a full game (custom shaders, audio dep,
  entity pooling, procedural world).
- Debug build (`build-debug/`) enables Vulkan validation layers — use it whenever
  you touch rendering code, errors print to stderr prefixed `[vulkan]`.
- The app opens a window; for a smoke test run `timeout 5 ./build/sandbox` and
  check the exit code is 124 (survived) and stderr has no `[vulkan]`/exception output.
- Engine shaders in `assets/shaders/*.{vert,frag}` (and per-project shaders in
  `projects/<name>/shaders/`) are compiled to `build/assets/shaders/` **at build
  time** by CMake (glslangValidator).
  Adding a new shader file requires re-running the build (the glob is
  `CONFIGURE_DEPENDS`, so no manual reconfigure needed). There is no runtime
  GLSL compilation.
- Asset paths passed to the engine (`loadModel`, `registerShader`) are relative
  to `build/assets/` unless absolute. Put `.obj` files in `assets/models/` and
  note they are NOT auto-copied to the build dir — either add a CMake copy step
  or pass an absolute path.

## Anatomy of a game

```cpp
#include <vke/vke.hpp>

class MyGame : public vke::Application {
    using vke::Application::Application;

    void onStart() override   { /* build the scene: entities + components */ }
    void onUpdate(float dt) override { /* game logic, runs every frame before rendering */ }
    void onGui() override     { /* optional extra ImGui windows (only if config.editor) */ }
};

int main() {
    vke::AppConfig cfg;
    cfg.title  = "My Game";
    cfg.width  = 1600; cfg.height = 900;
    cfg.fullscreen = false;                    // true = borderless fullscreen on the primary
                                               // monitor at desktop resolution (width/height ignored)
    cfg.mode   = vke::RenderMode::Continuous;  // game loop; OnDemand = render only on input (CAD-style)
    cfg.editor = true;                         // editor panels + fly cam (implies ImGui)
    cfg.gui    = false;                        // ImGui *without* editor panels — set this
                                               // (with editor=false) for game menus/HUDs via onGui()
    MyGame app(cfg);
    app.run();   // blocking; calls onStart once, then the loop
}
```

Inside `Application` you have `scene()`, `renderer()`, `window()`, `close()`,
`setRenderMode()`, `requestRedraw()`, and `setFullscreen(bool)` /
`isFullscreen()` for runtime fullscreen↔windowed switching (the swapchain
rebuild is automatic). `setEditorVisible(bool)` / `editorVisible()` show/hide
the editor panels + fly cam at runtime (needs `cfg.editor = true`; `onGui()`
keeps running either way) — the pattern for an in-game "edit mode" is
`cfg.editor = true`, `setEditorVisible(false)` in `onStart()`, then toggle on a
key, pausing your own camera/input controller while the editor is visible (see
`projects/backrooms/`, F1).

## Scene & components

`Scene` is a flat list of `Entity` (no parent/child hierarchy — compose
transforms yourself if you need attachment). Entities are identified by
`uint32_t id()`; **store ids, not pointers**, across frames if entities can be
deleted (`scene().find(id)` returns nullptr after deletion).

```cpp
auto& e = scene().createEntity("Player");      // every entity gets a Transform automatically
e.transform().position = {0, 1, 0};            // glm::vec3; rotation is Euler DEGREES (pitch X, yaw Y, roll Z)
auto& mr = e.add<vke::MeshRendererComponent>();
mr.mesh = renderer().primitive(vke::Primitive::Cube);   // Cube | Sphere | Plane (unit-sized, cached)
mr.mesh = renderer().loadModel("models/ship.obj");      // or Wavefront OBJ (triangulated on load)
mr.material.albedo    = {1.0f, 0.4f, 0.1f, 1.0f};
mr.material.shininess = 64.0f;    // Blinn-Phong exponent (1–256)
mr.material.specular  = 0.5f;     // specular strength (0–1)
mr.material.shader    = "basic";  // any name registered via registerShader

scene().destroyEntity(id);        // call renderer().waitIdle() first if the entity owned a mesh
e.get<T>() / e.has<T>() / e.remove<T>();   // component access; get returns T* or nullptr
scene().forEach<T>([](vke::Entity& e, T& c){ ... });   // iterate all entities with component T
```

The four built-in components (`Components.hpp`):
- `Transform` — position/rotation/scale, plus `matrix()`, `forward()`, `right()`, `up()`.
- `MeshRendererComponent` — `shared_ptr<Mesh>` + `Material`. No mesh ⇒ skipped silently.
- `CameraComponent` — `fov` (vertical, degrees), `nearClip`, `farClip`, `primary`.
  The renderer uses the **first entity whose camera has `primary == true`**;
  with no camera you get a fallback view from (4,4,8). Keep camera scale at {1,1,1}.
- `LightComponent` — `Directional` (direction = transform's `forward()`,
  position ignored), `Point` (position used, `range` = falloff radius) or
  `Spot` (position + direction + `range`, cone via `innerAngle`/`outerAngle`
  degrees — e.g. a flashlight), with `color` and `intensity`. **Max 8 lights
  are uploaded per frame**; extras are silently dropped.

New component types: subclass `vke::Component`, then `e.add<MyComponent>()` just
works (storage is a per-entity type-indexed map). The renderer only knows the
four built-ins; drive custom behavior from `onUpdate` via `scene().forEach<MyComponent>`.

## Conventions (get these right)

- **Y-up, right-handed, forward = −Z** (camera at +Z looking at origin has rotation {0,0,0}).
- Rotation order: yaw (Y) → pitch (X) → roll (Z); all degrees.
- Units are arbitrary but primitives are unit-sized (cube edge 1, sphere diameter 1, plane 1×1 in XZ).
- `dt` in `onUpdate` is seconds, clamped to 0.1 max.
- FIFO present mode (vsynced) — don't write frame-rate-dependent logic; always scale by `dt`.
- In `RenderMode::OnDemand`, `onUpdate` only runs when an OS event arrives —
  continuous animation requires `Continuous` mode (or call `requestRedraw()` to force frames).

## Custom shaders

1. Create `assets/shaders/foo.vert` + `foo.frag`. **Copy the UBO/push-constant
   declarations from `basic.vert` verbatim** — the engine binds exactly one
   descriptor set (set 0, binding 0: view/proj/camPos/ambient/lights[8]/lightCount)
   plus 96 bytes of push constants (model mat4, albedo vec4, params vec4 where
   x = shininess, y = specular). Vertex inputs: location 0 pos, 1 normal, 2 color, 3 uv.
2. Rebuild (generates the `.spv` files).
3. In `onStart()`:
   ```cpp
   renderer().registerShader("foo", "shaders/foo.vert.spv", "shaders/foo.frag.spv");
   mr.material.shader = "foo";
   ```
   Unknown shader names silently fall back to `"basic"`.
4. Apply gamma manually in fragment shaders (`pow(c, vec3(1.0/2.2))`) — the
   swapchain is UNORM, not sRGB (chosen so ImGui colors render exactly).

## Post-processing (fullscreen filter)

One optional fullscreen post-process pass (e.g. the backrooms VHS filter):

```cpp
renderer().setPostProcessShader("shaders/mygame/fx.vert.spv", "shaders/mygame/fx.frag.spv"); // enables it
renderer().setPostProcessEnabled(false / true);   // runtime toggle
renderer().postProcessTime     = t;               // seconds — feed from onUpdate
renderer().postProcessStrength = 0.5f;            // shader convention: 0 = clean
renderer().postProcessParams   = {...};           // free vec4 for the shader
```

While enabled the scene renders into an offscreen texture (PostProcess.hpp:
one color+depth target per frame in flight) and the shader composites it onto
the swapchain; ImGui draws on top, unfiltered. The vertex shader must emit a
fullscreen triangle from `gl_VertexIndex` (no vertex buffers — copy
`projects/backrooms/shaders/vhs.vert`); the fragment shader gets
`sampler2D set 0 binding 0` (the scene) and a push block of two vec4s:
`timeRes` (time, strength, width, height) + `params`. Gotcha: the offscreen
scene pass must stay render-pass *compatible* with the swapchain pass —
identical attachment formats/sample counts **and identical subpass
dependencies** (see the NOTE in Swapchain::createRenderPass) — because all
scene pipelines are built against the swapchain pass but recorded in it.

## Editor

When `cfg.editor = true`: main menu bar (render-mode switch, quit), Hierarchy
(spawn primitives/lights/cameras, right-click → Delete), Inspector (edit any
built-in component, add/remove components, switch material shader), Stats
overlay. **RMB-hold + WASD/QE** (Shift = fast) flies the primary camera. Editor
panels and your `onGui()` windows coexist. The editor edits live state only —
there is no save/load; "authoring" a scene means writing `onStart()` code.

## Gotchas

- Call `renderer().waitIdle()` before destroying entities/meshes that may have
  been drawn in the last 2 frames (the editor's delete button already does this).
- `Mesh` is non-copyable and GPU-owned; share via the `shared_ptr` you got from
  `primitive()`/`loadModel()`. Meshes must be destroyed before the Renderer3D
  (automatic if they only live in the Scene).
- Push-constant block is exactly `sizeof(PushData)` = 96 bytes; if you change
  `PushData` (Renderer3D.hpp) you must update **every** shader's push block and vice versa.
- The GLSL `Light` struct (4 vec4s: position/color/direction/cone) must match
  `GpuLight` (Renderer3D.hpp) in **every** shader, same as `GlobalUBO`.
- Same for `GlobalUBO` — it is std140; vec4-align any new fields and mirror the
  GLSL struct in all shaders (`basic.*` and `toon.*` included).
- Rendering is forward, opaque-only: no alpha blending pipeline, no shadows,
  no textures yet. Albedo alpha < 1 will not blend.
- Back-face culling is OFF (`Pipeline.cpp`, `VK_CULL_MODE_NONE`) so any OBJ
  renders; enable `VK_CULL_MODE_BACK_BIT` for perf on closed meshes.
- One light type quirk: a directional light's transform position is irrelevant
  but still shown in the editor.
- `Entity::name` is not unique; `findByName` returns the first match.

## Extension points (in rough order of effort)

- **New shading models**: just custom shaders (above) — no C++ needed.
- **Textures**: add a sampler binding to a new descriptor set layout in
  `Renderer3D::createDescriptorResources` + image upload helpers in `VulkanContext`.
- **Custom draw code / extra passes**: between `beginFrame()` and `endFrame()`
  you're inside the main render pass with depth; `renderer().currentCommandBuffer()`,
  `.pipelineLayout()`, `.context()`, `.swapchain()` expose the raw Vulkan handles.
- **Render pass changes** (MSAA, offscreen viewport): `engine/src/vulkan/Swapchain.cpp`
  owns the render pass; pipelines and ImGui init both reference it.
