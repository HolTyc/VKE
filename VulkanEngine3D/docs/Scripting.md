# VKE Scripting Guide

VKE scripts are hot-reloaded C++ classes that attach to entities in a VKE editor
project. They are meant for gameplay behavior: moving entities, reading input,
finding other entities, editing exposed values in the Inspector, and reacting
while Play mode is running.

The scripting layer is deliberately small. There is no Lua, Python, visual
scripting, or separate CMake target for scripts. Put `.cpp` files in your
project's `scripts/` folder, reload them in the editor, and attach the
registered classes to entities.

## Quick Start

Build the engine and launch the editor:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/vke-editor
```

Create a project from the Welcome window, or open one directly:

```bash
./build/vke-editor projects/First_game
```

A VKE editor project looks like this:

```text
my_project/
  project.json
  scenes/
    main.scene
  scripts/
    Rotator.cpp
  assets/
  .vke/
```

The editor compiles every `scripts/**/*.cpp` file into `.vke/scripts.so`. Build
errors appear in the Console panel.

## Your First Script

Create `scripts/Rotator.cpp`:

```cpp
#include <vke/Script.hpp>

class Rotator : public vke::Script {
public:
    float speed = 90.0f; // degrees per second

    void onUpdate(float dt) override {
        transform().rotation.y += speed * dt;
    }

    VKE_SCRIPT(Rotator, VKE_PROPERTY(speed))
};
```

In the editor:

1. Open your project.
2. Select an entity.
3. Click `Add Component`.
4. Open `Script`.
5. Choose `Rotator`.
6. Edit `speed` in the Inspector.
7. Press `Play`.

Change the `.cpp` file and click `Reload Scripts`, or press `Ctrl+R`, to rebuild
without restarting the editor.

## Script Anatomy

Every script is a default-constructible C++ class that derives from
`vke::Script`.

```cpp
#include <vke/Script.hpp>

class MyScript : public vke::Script {
public:
    void onStart() override {
        // Called once when the game enters Play mode.
    }

    void onUpdate(float dt) override {
        // Called every frame while Play mode is running.
    }

    void onDestroy() override {
        // Called before the script instance is destroyed or reloaded.
    }

    VKE_SCRIPT(MyScript)
};
```

Place `VKE_SCRIPT(ClassName, ...)` last in the class body. The class name passed
to `VKE_SCRIPT` is the name shown in the editor and stored in scene files.

## Lifecycle

| Moment | What happens |
| --- | --- |
| Open project | The editor compiles `scripts/**/*.cpp`, loads `.vke/scripts.so`, and creates script instances for existing script slots. |
| Edit mode | Script instances exist so the Inspector can edit their properties, but `onStart` and `onUpdate` do not run. |
| Play | `onStart` runs once, then `onUpdate(dt)` runs every frame. |
| Stop | The exact pre-play scene snapshot is restored. Runtime changes made by scripts are discarded. |
| Reload Scripts | The editor captures property values, unloads old instances, recompiles, reloads, recreates instances, and reapplies matching properties. |

Reloading preserves properties by name and type. If you rename a property or
change its type, existing saved values for that member are skipped and the C++
default is used.

## Inspector Properties

Expose editable and serialized members with `VKE_PROPERTY`.

Supported property types:

| C++ type | Inspector widget |
| --- | --- |
| `float` | drag float |
| `int` | drag int |
| `bool` | checkbox |
| `glm::vec3` | three float fields |
| `std::string` | text field |

Example with all supported types:

```cpp
#include <vke/Script.hpp>

class InspectorDemo : public vke::Script {
public:
    float speed = 3.0f;
    int mode = 0;
    bool enabled = true;
    glm::vec3 offset{0.0f, 1.0f, 0.0f};
    std::string targetName = "Sun";

    void onUpdate(float dt) override {
        if (!enabled) return;

        if (vke::Entity* target = scene().findByName(targetName)) {
            target->transform().rotation.y += speed * dt;
            target->transform().position = transform().position + offset;
        }
    }

    VKE_SCRIPT(InspectorDemo,
        VKE_PROPERTY(speed),
        VKE_PROPERTY(mode),
        VKE_PROPERTY(enabled),
        VKE_PROPERTY(offset),
        VKE_PROPERTY(targetName))
};
```

Scene JSON stores script slots like this:

```json
{
  "scripts": [
    { "type": "Rotator", "props": { "speed": 90.0 } }
  ]
}
```

Normally you do not need to edit this by hand. Use the Inspector and `Ctrl+S`.

## Useful API

These helpers are available inside every `vke::Script`:

| Helper | Use |
| --- | --- |
| `entity()` | The entity this script is attached to. |
| `transform()` | Shortcut for `entity().transform()`. |
| `scene()` | Access to all scene entities. |
| `app()` | Access to the running `vke::Application`. |
| `window()` | Access to the engine window. |
| `renderer()` | Access to the renderer facade. |
| `keyDown(code)` | True while a key is held. Include `<vke/Keys.hpp>` for key names. |
| `mouseButtonDown(code)` | True while a mouse button is held. Include `<vke/Keys.hpp>` for mouse names. |

Entity helpers:

```cpp
auto* light = entity().get<vke::LightComponent>();
if (!entity().has<vke::CameraComponent>()) {
    entity().add<vke::CameraComponent>();
}
entity().remove<vke::LightComponent>();
```

Transform values use degrees for rotation:

```cpp
transform().position = {0.0f, 2.0f, 6.0f};
transform().rotation = {-12.0f, 0.0f, 0.0f};
transform().scale = {1.0f, 1.0f, 1.0f};

glm::vec3 forward = transform().forward();
glm::vec3 right = transform().right();
glm::vec3 up = transform().up();
```

## Example: Keyboard Movement

```cpp
#include <vke/Script.hpp>
#include <vke/Keys.hpp>

#include <glm/geometric.hpp>

class KeyboardMover : public vke::Script {
public:
    float speed = 5.0f;
    float turnSpeed = 120.0f;

    void onUpdate(float dt) override {
        auto& t = transform();

        if (keyDown(vke::Key::A)) t.rotation.y += turnSpeed * dt;
        if (keyDown(vke::Key::D)) t.rotation.y -= turnSpeed * dt;

        glm::vec3 move{0.0f};
        if (keyDown(vke::Key::W)) move += t.forward();
        if (keyDown(vke::Key::S)) move -= t.forward();

        if (glm::length(move) > 0.001f) {
            t.position += glm::normalize(move) * speed * dt;
        }
    }

    VKE_SCRIPT(KeyboardMover,
        VKE_PROPERTY(speed),
        VKE_PROPERTY(turnSpeed))
};
```

Key names live in `vke::Key`, and mouse buttons live in `vke::Mouse`.

```cpp
if (keyDown(vke::Key::Space)) {
    transform().position.y += 4.0f * dt;
}

if (mouseButtonDown(vke::Mouse::Left)) {
    // Fire, select, interact, etc.
}
```

## Example: Pulsing Light

Attach this to an entity with a `LightComponent`.

```cpp
#include <vke/Script.hpp>

#include <algorithm>
#include <cmath>

class LightPulse : public vke::Script {
public:
    float minIntensity = 0.25f;
    float maxIntensity = 3.0f;
    float speed = 2.0f;
    float time = 0.0f;

    void onUpdate(float dt) override {
        auto* light = entity().get<vke::LightComponent>();
        if (!light) return;

        time += dt;
        const float wave = 0.5f + 0.5f * std::sin(time * speed);
        light->intensity = std::lerp(minIntensity, maxIntensity, wave);
    }

    VKE_SCRIPT(LightPulse,
        VKE_PROPERTY(minIntensity),
        VKE_PROPERTY(maxIntensity),
        VKE_PROPERTY(speed))
};
```

The `time` member is not listed in `VKE_PROPERTY`, so it is not shown in the
Inspector and is not serialized.

## Example: Follow Another Entity

```cpp
#include <vke/Script.hpp>

class FollowTarget : public vke::Script {
public:
    std::string targetName = "Player";
    glm::vec3 offset{0.0f, 3.0f, 7.0f};
    float followSpeed = 8.0f;

    void onUpdate(float dt) override {
        vke::Entity* target = scene().findByName(targetName);
        if (!target) return;

        glm::vec3 desired = target->transform().position + offset;
        transform().position += (desired - transform().position) * followSpeed * dt;
    }

    VKE_SCRIPT(FollowTarget,
        VKE_PROPERTY(targetName),
        VKE_PROPERTY(offset),
        VKE_PROPERTY(followSpeed))
};
```

Entity names are not required to be unique. `findByName` returns the first match.

## Editor Workflow

| Action | Where |
| --- | --- |
| Compile and load scripts | Opening a project does this automatically. |
| Reload scripts | Toolbar `Reload Scripts`, or `Ctrl+R` in Edit mode. |
| Attach script | Select entity -> `Add Component` -> `Script` -> choose class. |
| Edit properties | Select entity, then edit the script panel in the Inspector. |
| Save scene | `File` -> `Save Scene`, or `Ctrl+S`. |
| Run scripts | Press `Play`. |
| Stop play mode | Press `Stop` or `Esc`. |
| See compiler errors | Open `File` -> `Console`. |

You can validate scripts without opening a Vulkan window:

```bash
./build/vke-editor --compile-test path/to/project
```

Successful output lists every registered class and property:

```text
Rotator: speed(float)
KeyboardMover: speed(float) turnSpeed(float)
```

## Rules and Gotchas

- Include `<vke/Script.hpp>` in every script `.cpp`.
- Include `<vke/Keys.hpp>` when using `vke::Key::*` or `vke::Mouse::*`.
- Put script source files under the project's `scripts/` folder.
- Any number of script classes can live in one `.cpp`, but one class per file is easier to maintain.
- A script class must be default-constructible because the engine creates it with `new ClassName()`.
- Put `VKE_SCRIPT(...)` last in the class body.
- Only list simple member variables in `VKE_PROPERTY(...)`.
- Multiple scripts can be attached to the same entity.
- Scripts run only in Play mode. Inspector editing works in Edit mode because instances still exist.
- Stop restores the pre-play snapshot, so runtime script changes are not kept.
- `.vke/` contains generated script libraries and should stay out of version control.

## Troubleshooting

| Problem | Check |
| --- | --- |
| Script class is not in the Add Component menu | Open the Console. The project may not have compiled, or the class may be missing `VKE_SCRIPT`. |
| `scripts.so has no vkeGetScriptRegistry` | At least one compiled script class must include `VKE_SCRIPT(ClassName, ...)`. |
| Inspector says `Class missing from script library` | The scene references a script type that is no longer registered. Restore the class or remove the script slot. |
| Property value reset after reload | The property name or type changed. Matching is by property name and exact supported type. |
| Key names do not compile | Add `#include <vke/Keys.hpp>`. |
| Changes vanish after Stop | This is expected. Play mode restores the scene snapshot. Make persistent edits in Edit mode and save. |
| No `.cpp` files found | Add at least one `.cpp` file under `scripts/`. |
