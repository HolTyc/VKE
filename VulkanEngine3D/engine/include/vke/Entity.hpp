#pragma once

#include "Components.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace vke {

// A named object in the scene holding an arbitrary set of components.
// Every entity is created with a Transform.
class Entity {
public:
    Entity(uint32_t id, std::string name) : name(std::move(name)), id_(id) {
        add<Transform>();
    }

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    template <typename T, typename... Args>
    T& add(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *component;
        components_[std::type_index(typeid(T))] = std::move(component);
        return ref;
    }

    template <typename T>
    T* get() const {
        auto it = components_.find(std::type_index(typeid(T)));
        return it == components_.end() ? nullptr : static_cast<T*>(it->second.get());
    }

    template <typename T>
    bool has() const {
        return components_.contains(std::type_index(typeid(T)));
    }

    template <typename T>
    void remove() {
        static_assert(!std::is_same_v<T, Transform>, "Transform cannot be removed");
        components_.erase(std::type_index(typeid(T)));
    }

    Transform& transform() const { return *get<Transform>(); }
    uint32_t id() const { return id_; }

    std::string name;

private:
    uint32_t id_;
    std::unordered_map<std::type_index, std::unique_ptr<Component>> components_;
};

} // namespace vke
