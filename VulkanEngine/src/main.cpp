// Demo application: a player sprite driven by a script (WASD), a spinning
// quad, and a colored background grid — all manageable live through the
// editor (F1 to toggle).
#include "Engine.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Game scripts: subclass ScriptableEntity, override the lifecycle hooks.
// ---------------------------------------------------------------------------
class PlayerController : public ScriptableEntity {
protected:
    void OnCreate() override {
        ENGINE_INFO("PlayerController attached to '%s'", Get<TagComponent>().Name.c_str());
    }

    void OnUpdate(float dt) override {
        auto& transform = Get<TransformComponent>();
        const float speed = 4.0f;
        if (Input::IsKeyDown(GLFW_KEY_A) || Input::IsKeyDown(GLFW_KEY_LEFT))
            transform.Position.x -= speed * dt;
        if (Input::IsKeyDown(GLFW_KEY_D) || Input::IsKeyDown(GLFW_KEY_RIGHT))
            transform.Position.x += speed * dt;
        if (Input::IsKeyDown(GLFW_KEY_S) || Input::IsKeyDown(GLFW_KEY_DOWN))
            transform.Position.y -= speed * dt;
        if (Input::IsKeyDown(GLFW_KEY_W) || Input::IsKeyDown(GLFW_KEY_UP))
            transform.Position.y += speed * dt;
    }
};

class Spinner : public ScriptableEntity {
public:
    explicit Spinner(float speed = 1.5f) : m_Speed(speed) {}

protected:
    void OnUpdate(float dt) override {
        Get<TransformComponent>().Rotation += m_Speed * dt;
    }

private:
    float m_Speed;
};

// ---------------------------------------------------------------------------
// A procedural checkerboard so the demo needs no asset files on disk.
// ---------------------------------------------------------------------------
static std::shared_ptr<Texture> MakeCheckerTexture() {
    constexpr uint32_t size = 64, cell = 8;
    std::vector<uint32_t> pixels(size * size);
    for (uint32_t y = 0; y < size; y++)
        for (uint32_t x = 0; x < size; x++)
            pixels[y * size + x] = (((x / cell) + (y / cell)) % 2) ? 0xFFFFFFFF : 0xFF303030;
    return Texture::Create(size, size, pixels.data(), "checkerboard");
}

int main() {
    Application app({
        .Name = "VulkanEngine Sandbox",
        .Width = 1600,
        .Height = 900,
        .Mode = RenderMode::Continuous, // switch to EventDriven in the Engine panel
    });

    Scene& scene = app.GetScene();
    auto checker = MakeCheckerTexture();

    // Player: textured sprite + behavior script.
    Entity player = scene.CreateEntity("Player");
    player.Get<TransformComponent>().Position = { 0.0f, 0.0f };
    auto& playerSprite = player.Add<SpriteComponent>();
    playerSprite.Tex = checker;
    playerSprite.Color = { 0.4f, 0.9f, 1.0f, 1.0f }; // tint
    player.Add<NativeScriptComponent>().Bind<PlayerController>();

    // A spinning textured quad.
    Entity spinner = scene.CreateEntity("Spinner");
    spinner.Get<TransformComponent>().Position = { 3.0f, 1.5f };
    spinner.Get<TransformComponent>().Scale = { 1.5f, 1.5f };
    spinner.Add<SpriteComponent>().Tex = checker;
    spinner.Add<NativeScriptComponent>().Bind<Spinner>(2.0f);

    // Background grid of flat-colored quads (one batched draw call).
    for (int y = -3; y <= 3; y++) {
        for (int x = -6; x <= 6; x++) {
            Entity tile = scene.CreateEntity("Tile");
            auto& transform = tile.Get<TransformComponent>();
            transform.Position = { (float)x, (float)y };
            transform.Scale = { 0.85f, 0.85f };
            auto& sprite = tile.Add<SpriteComponent>();
            float hue = ((x + 6) / 12.0f + (y + 3) / 6.0f) * 0.5f;
            sprite.Color = { 0.15f + 0.25f * std::abs(std::sin(hue * 6.28f)),
                             0.15f + 0.25f * std::abs(std::sin(hue * 6.28f + 2.1f)),
                             0.20f + 0.25f * std::abs(std::sin(hue * 6.28f + 4.2f)),
                             1.0f };
        }
    }

    // Optional per-frame logic outside the ECS:
    app.SetUpdateCallback([&](float) {
        if (Input::IsKeyDown(GLFW_KEY_ESCAPE))
            app.Close();
    });

    app.Run();
    return 0;
}
