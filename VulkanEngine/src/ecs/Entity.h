#pragma once

#include "ecs/Scene.h"

// Lightweight handle: an id + a scene pointer. Copy it freely.
class Entity {
public:
    Entity() = default;
    Entity(EntityID id, Scene* scene) : m_ID(id), m_Scene(scene) {}

    template<typename T, typename... Args>
    T& Add(Args&&... args) { return m_Scene->Add<T>(m_ID, std::forward<Args>(args)...); }

    template<typename T>
    T& Get() { return m_Scene->Get<T>(m_ID); }

    template<typename T>
    T* TryGet() { return m_Scene->TryGet<T>(m_ID); }

    template<typename T>
    bool Has() const { return m_Scene->Has<T>(m_ID); }

    template<typename T>
    void Remove() { m_Scene->Remove<T>(m_ID); }

    void Destroy() { m_Scene->DestroyEntity(m_ID); m_ID = NullEntity; }

    EntityID GetID() const { return m_ID; }
    Scene* GetScene() const { return m_Scene; }

    operator bool() const { return m_ID != NullEntity && m_Scene && m_Scene->IsValid(m_ID); }
    bool operator==(const Entity& other) const { return m_ID == other.m_ID && m_Scene == other.m_Scene; }

private:
    EntityID m_ID = NullEntity;
    Scene* m_Scene = nullptr;
};
