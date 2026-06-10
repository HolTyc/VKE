#pragma once

#include "ecs/ScriptableEntity.h"
#include "vk/Texture.h"

#include <glm/glm.hpp>

struct TagComponent {
    std::string Name = "Entity";
};

struct TransformComponent {
    glm::vec2 Position{ 0.0f, 0.0f };
    glm::vec2 Scale{ 1.0f, 1.0f };
    float Rotation = 0.0f; // radians
};

struct SpriteComponent {
    glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::shared_ptr<Texture> Tex; // nullptr => flat color (white texture)
};

struct NativeScriptComponent {
    std::unique_ptr<ScriptableEntity> Instance;
    std::function<std::unique_ptr<ScriptableEntity>()> Instantiate;

    template<typename T, typename... Args>
    void Bind(Args... args) {
        Instantiate = [args...]() -> std::unique_ptr<ScriptableEntity> {
            return std::make_unique<T>(args...);
        };
    }
};
