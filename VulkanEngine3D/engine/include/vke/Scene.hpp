#pragma once

#include "Entity.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace vke {

// Flat container of entities. Lightweight by design: no hierarchy, no serialization.
class Scene {
public:
    Entity& createEntity(const std::string& name = "Entity") {
        entities_.push_back(std::make_unique<Entity>(nextId_++, name));
        return *entities_.back();
    }

    void destroyEntity(uint32_t id) {
        std::erase_if(entities_, [id](const auto& e) { return e->id() == id; });
    }

    Entity* find(uint32_t id) {
        for (auto& e : entities_)
            if (e->id() == id) return e.get();
        return nullptr;
    }

    Entity* findByName(const std::string& name) {
        for (auto& e : entities_)
            if (e->name == name) return e.get();
        return nullptr;
    }

    // First entity with a CameraComponent marked primary.
    Entity* primaryCamera() {
        for (auto& e : entities_)
            if (auto* cam = e->get<CameraComponent>(); cam && cam->primary) return e.get();
        return nullptr;
    }

    // Invokes fn(Entity&, T&) for every entity that has component T.
    template <typename T, typename F>
    void forEach(F&& fn) {
        for (auto& e : entities_)
            if (auto* c = e->get<T>()) fn(*e, *c);
    }

    std::vector<std::unique_ptr<Entity>>& entities() { return entities_; }

private:
    std::vector<std::unique_ptr<Entity>> entities_;
    uint32_t nextId_ = 1;
};

} // namespace vke
