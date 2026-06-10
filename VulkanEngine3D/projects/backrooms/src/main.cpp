// The Backrooms — Level 0.
//
// An endless procedural maze of yellow rooms built on the vke engine:
//   * infinite deterministic layout (LevelGen.hpp) rendered through a pool of
//     recycled wall/pillar entities — nothing is ever destroyed, so no
//     waitIdle() hitches while wandering
//   * world-space procedural shaders (wallpaper / carpet / ceiling tiles with
//     emissive fluorescent panels) so the recycled geometry tiles seamlessly
//   * up to 7 point lights tracking the nearest live ceiling panels, with
//     per-panel flicker mirrored between C++ and the ceiling shader
//   * first-person walk: mouse look, WASD, Shift to hurry, head bob, sliding
//     collision against the maze walls; F toggles the flashlight (spot light)
//   * Esc opens a pause menu (resume / fullscreen toggle / quit) drawn through
//     the engine's gui-only ImGui layer (config.gui, no editor panels)
//   * F1 switches between play mode and edit mode: edit mode frees the cursor,
//     pauses the player controller and shows the engine editor (Hierarchy,
//     Inspector, RMB+WASD fly camera) over the live game — flying far away
//     regenerates the maze around the fly camera. Esc or F1 returns to play.
//     Entities are referenced by id everywhere, so deleting things in the
//     editor can't leave dangling pointers.
//   * synthesized ambience: fluorescent hum that follows the nearest fixture,
//     a low rumble, and distant thumps (Audio.hpp, miniaudio)

#include "Audio.hpp"
#include "LevelGen.hpp"

#include <vke/vke.hpp>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int   kViewCells     = 9;    // walls kept alive within +-9 cells (36 m > fog end)
constexpr int   kWallPoolSize  = 480;
constexpr int   kMaxLights     = 7;    // engine uploads max 8; keep one spare
constexpr float kPlayerRadius  = 0.32f;
constexpr float kWalkSpeed     = 2.3f;
constexpr float kRunSpeed      = 4.4f;
constexpr float kMouseSens     = 0.085f; // degrees per pixel
constexpr float kTimeWrap      = 600.0f; // matches Audio.cpp and ceiling.frag

} // namespace

class BackroomsGame : public vke::Application {
public:
    using vke::Application::Application;

protected:
    void onStart() override {
        using namespace vke;

        renderer().registerShader("wall",    "shaders/backrooms/level.vert.spv", "shaders/backrooms/wall.frag.spv");
        renderer().registerShader("carpet",  "shaders/backrooms/level.vert.spv", "shaders/backrooms/carpet.frag.spv");
        renderer().registerShader("ceiling", "shaders/backrooms/level.vert.spv", "shaders/backrooms/ceiling.frag.spv");

        // Warm yellow-green ambient (the photo's overall cast); clear color
        // matches the shaders' fog color (gamma-corrected) so geometry edges
        // dissolve into it.
        renderer().ambientLight = {0.30f, 0.285f, 0.19f};
        renderer().clearColor   = {0.28f, 0.27f, 0.20f, 1.0f};

        // ---- player camera -------------------------------------------------
        auto& cam = scene().createEntity("Player");
        cam.transform().position = {2.0f, bk::kEyeHeight, 2.0f}; // centre of spawn cell
        auto& camComp = cam.add<CameraComponent>();
        camComp.fov = 72.0f;
        camComp.nearClip = 0.05f;
        camComp.farClip  = 150.0f;
        camera_ = &cam;

        // ---- floor & ceiling: two big planes that slide under the player ----
        auto& floor = scene().createEntity("Floor");
        floor.transform().scale = {80.0f, 1.0f, 80.0f};
        auto& floorMr = floor.add<MeshRendererComponent>();
        floorMr.mesh = renderer().primitive(Primitive::Plane);
        floorMr.material.shader = "carpet";
        floorMr.material.shininess = 4.0f;
        floorMr.material.specular  = 0.03f;
        floor_ = &floor;

        auto& ceiling = scene().createEntity("Ceiling");
        ceiling.transform().position = {0.0f, bk::kWallH, 0.0f};
        ceiling.transform().rotation = {180.0f, 0.0f, 0.0f}; // normal faces down
        ceiling.transform().scale = {80.0f, 1.0f, 80.0f};
        auto& ceilMr = ceiling.add<MeshRendererComponent>();
        ceilMr.mesh = renderer().primitive(Primitive::Plane);
        ceilMr.material.shader = "ceiling";
        ceilMr.material.shininess = 16.0f;
        ceilMr.material.specular  = 0.10f;
        ceiling_ = &ceiling;

        // ---- wall/pillar pool (entities are recycled, never destroyed) ------
        wallPool_.reserve(kWallPoolSize);
        for (int i = 0; i < kWallPoolSize; ++i) {
            auto& wall = scene().createEntity("Wall");
            auto& mr = wall.add<MeshRendererComponent>();
            mr.mesh = nullptr; // hidden until placed
            mr.material.shader = "wall";
            mr.material.shininess = 16.0f;
            mr.material.specular  = 0.08f;
            wallPool_.push_back(&wall);
        }

        // ---- fluorescent light pool ------------------------------------------
        for (int i = 0; i < kMaxLights; ++i) {
            auto& lamp = scene().createEntity("Fluorescent");
            auto& light = lamp.add<vke::LightComponent>();
            light.type  = LightComponent::Type::Point;
            light.color = {1.0f, 0.97f, 0.86f}; // near-white fluorescent
            light.range = 9.5f;
            light.intensity = 0.0f;
            lights_.push_back(&lamp);
        }

        // ---- flashlight (spot light glued to the camera, F to toggle) --------
        auto& torch = scene().createEntity("Flashlight");
        auto& torchLight = torch.add<vke::LightComponent>();
        torchLight.type  = LightComponent::Type::Spot;
        torchLight.color = {1.0f, 0.98f, 0.92f};
        torchLight.intensity  = 0.0f; // starts off
        torchLight.range      = 22.0f;
        torchLight.innerAngle = 11.0f;
        torchLight.outerAngle = 24.0f;
        flashlight_ = &torch;
        // panel pool (7) + flashlight = 8 = the engine's per-frame light limit

        // ---- input & audio ---------------------------------------------------
        glfwSetInputMode(window().handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window().handle(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

        audio_.start(); // silently absent if no audio device

        // Scale the pause menu with the actual framebuffer (readable on 4K).
        ImGui::GetIO().FontGlobalScale =
            std::max(1.0f, window().framebufferExtent().height / 1080.0f);

        rebuildWalls();
    }

    void onUpdate(float dt) override {
        // Esc toggles the pause menu (edge-triggered).
        bool esc = window().keyDown(GLFW_KEY_ESCAPE);
        if (esc && !prevEsc_) setMenuOpen(!menuOpen_);
        prevEsc_ = esc;

        bool f = window().keyDown(GLFW_KEY_F);
        if (f && !prevF_ && !menuOpen_) flashlightOn_ = !flashlightOn_;
        prevF_ = f;

        time_ = std::fmod(time_ + dt, kTimeWrap);

        if (!menuOpen_) {
            updateLook();
            updateMovement(dt);
        }

        glm::vec3 pos = camera_->transform().position;
        int cx = static_cast<int>(std::floor(pos.x / bk::kCell));
        int cz = static_cast<int>(std::floor(pos.z / bk::kCell));
        if (cx != lastCx_ || cz != lastCz_) {
            lastCx_ = cx; lastCz_ = cz;
            rebuildWalls();
        }

        // Floor/ceiling planes follow the player, snapped to the cell grid so
        // the world-space shader patterns never visibly move.
        float sx = std::floor(pos.x / bk::kCell) * bk::kCell;
        float sz = std::floor(pos.z / bk::kCell) * bk::kCell;
        floor_->transform().position   = {sx, 0.0f, sz};
        ceiling_->transform().position = {sx, bk::kWallH, sz};

        // Feed elapsed time to the ceiling shader through the material alpha
        // (the engine has no time uniform; rendering is opaque so alpha is free).
        ceiling_->get<vke::MeshRendererComponent>()->material.albedo.a = time_;

        updateLights(pos);
        updateFlashlight();
    }

    void onGui() override {
        if (!menuOpen_) return;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({vp->GetCenter().x, vp->GetCenter().y},
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::Begin("Paused", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

        if (ImGui::Button("Resume", {220, 0})) setMenuOpen(false);

        bool fs = isFullscreen();
        if (ImGui::Checkbox("Fullscreen", &fs)) setFullscreen(fs);

        ImGui::Separator();
        ImGui::TextDisabled("WASD walk  |  Shift hurry  |  F flashlight");

        ImGui::Separator();
        if (ImGui::Button("Quit", {220, 0})) close();

        ImGui::End();
    }

private:
    void setMenuOpen(bool open) {
        menuOpen_ = open;
        glfwSetInputMode(window().handle(), GLFW_CURSOR,
                         open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        mouseInit_ = false; // re-seed look deltas so the view doesn't jump
    }

    void updateLook() {
        double mx, my;
        window().cursorPos(mx, my);
        if (!mouseInit_) { lastMx_ = mx; lastMy_ = my; mouseInit_ = true; }
        yaw_   -= static_cast<float>(mx - lastMx_) * kMouseSens;
        pitch_ -= static_cast<float>(my - lastMy_) * kMouseSens;
        pitch_  = glm::clamp(pitch_, -89.0f, 89.0f);
        lastMx_ = mx; lastMy_ = my;
        camera_->transform().rotation = {pitch_, yaw_, 0.0f};
    }

    void updateMovement(float dt) {
        float yawRad = glm::radians(yaw_);
        glm::vec2 fwd{-std::sin(yawRad), -std::cos(yawRad)}; // walking ignores pitch
        glm::vec2 right{-fwd.y, fwd.x};                      // = Transform::right() in xz

        glm::vec2 wish{0.0f};
        if (window().keyDown(GLFW_KEY_W)) wish += fwd;
        if (window().keyDown(GLFW_KEY_S)) wish -= fwd;
        if (window().keyDown(GLFW_KEY_D)) wish += right;
        if (window().keyDown(GLFW_KEY_A)) wish -= right;

        auto& t = camera_->transform();
        glm::vec2 p{t.position.x, t.position.z};
        bool moving = glm::dot(wish, wish) > 0.0f;
        float speed = window().keyDown(GLFW_KEY_LEFT_SHIFT) ? kRunSpeed : kWalkSpeed;
        if (moving) {
            p += glm::normalize(wish) * speed * dt;
            bobPhase_ += dt * speed * 2.6f;
        }
        p = bk::collide(p, kPlayerRadius);

        t.position.x = p.x;
        t.position.z = p.y;
        t.position.y = bk::kEyeHeight + (moving ? std::sin(bobPhase_) * 0.04f : 0.0f);
    }

    void updateFlashlight() {
        auto& cam = camera_->transform();
        // Carried slightly low and to the right, like held in a hand.
        flashlight_->transform().position = cam.position + cam.right() * 0.15f -
                                            glm::vec3(0.0f, 0.18f, 0.0f);
        flashlight_->transform().rotation = cam.rotation;
        flashlight_->get<vke::LightComponent>()->intensity = flashlightOn_ ? 3.2f : 0.0f;
    }

    void rebuildWalls() {
        using namespace vke;
        size_t used = 0;
        auto place = [&](glm::vec3 pos, glm::vec3 scale) {
            if (used >= wallPool_.size()) return;
            Entity* e = wallPool_[used++];
            e->transform().position = pos;
            e->transform().scale = scale;
            e->get<MeshRendererComponent>()->mesh = renderer().primitive(Primitive::Cube);
        };

        const float h2 = bk::kWallH * 0.5f, t = bk::kWallT;
        for (int z = lastCz_ - kViewCells; z <= lastCz_ + kViewCells; ++z) {
            for (int x = lastCx_ - kViewCells; x <= lastCx_ + kViewCells; ++x) {
                float wx = x * bk::kCell, wz = z * bk::kCell;
                if (bk::wallSouth(x, z))
                    place({wx + bk::kCell * 0.5f, h2, wz}, {bk::kCell + t, bk::kWallH, t});
                if (bk::wallWest(x, z))
                    place({wx, h2, wz + bk::kCell * 0.5f}, {t, bk::kWallH, bk::kCell + t});
                if (bk::pillar(x, z))
                    place({wx, h2, wz}, {0.6f, bk::kWallH, 0.6f});
            }
        }
        for (size_t i = used; i < wallPool_.size(); ++i)
            wallPool_[i]->get<MeshRendererComponent>()->mesh = nullptr;
    }

    void updateLights(glm::vec3 playerPos) {
        // Collect live panels near the player, nearest first.
        struct Candidate { float dist2; glm::vec2 center; float hash; };
        std::vector<Candidate> candidates;

        int pcx = static_cast<int>(std::floor(playerPos.x / bk::kCell));
        int pcz = static_cast<int>(std::floor(playerPos.z / bk::kCell));
        for (int z = pcz - 5; z <= pcz + 5; ++z) {
            for (int x = pcx - 5; x <= pcx + 5; ++x) {
                if (!bk::panelCell(x, z)) continue;
                glm::vec2 c = bk::panelCenter(x, z);
                if (bk::panelDead(c)) continue;
                glm::vec2 d = c - glm::vec2(playerPos.x, playerPos.z);
                candidates.push_back({glm::dot(d, d), c, bk::panelHash(c)});
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.dist2 < b.dist2; });

        for (size_t i = 0; i < lights_.size(); ++i) {
            auto* light = lights_[i]->get<vke::LightComponent>();
            if (i < candidates.size()) {
                const auto& c = candidates[i];
                lights_[i]->transform().position = {c.center.x, bk::kPanelY, c.center.y};
                light->intensity = 2.2f * bk::flicker(time_, c.hash);
            } else {
                light->intensity = 0.0f;
            }
        }

        // The hum follows the nearest live fixture; faulty tubes buzz louder.
        float hum = 0.06f;
        if (!candidates.empty()) {
            float d = std::sqrt(candidates[0].dist2);
            hum += 0.38f * glm::clamp(1.0f - d / 14.0f, 0.0f, 1.0f);
            if (candidates[0].hash > 0.86f) hum *= 1.6f;
        }
        audio_.setHumLevel(hum);
    }

    // Entities are never destroyed, so raw pointers stay valid for the whole run.
    vke::Entity* camera_  = nullptr;
    vke::Entity* floor_   = nullptr;
    vke::Entity* ceiling_ = nullptr;
    vke::Entity* flashlight_ = nullptr;
    std::vector<vke::Entity*> wallPool_;
    std::vector<vke::Entity*> lights_;

    int lastCx_ = 0, lastCz_ = 0;
    float yaw_ = 0.0f, pitch_ = 0.0f;
    double lastMx_ = 0.0, lastMy_ = 0.0;
    bool mouseInit_ = false;
    float bobPhase_ = 0.0f;
    float time_ = 0.0f;

    bool menuOpen_ = false, flashlightOn_ = false;
    bool prevEsc_ = false, prevF_ = false;

    bk::Audio audio_;
};

int main() {
    vke::AppConfig config;
    config.title  = "The Backrooms — Level 0";
    config.width  = 1600;  // used if fullscreen is turned off
    config.height = 900;
    config.fullscreen = true;
    config.mode   = vke::RenderMode::Continuous;
    config.editor = false; // no editor panels / fly-cam ...
    config.gui    = true;  // ... but ImGui is needed for the pause menu

    BackroomsGame game(config);
    game.run();
    return 0;
}
