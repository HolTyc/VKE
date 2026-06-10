#pragma once

#include "core/Base.h"

#include <typeindex>
#include <unordered_map>

using EntityID = uint32_t;
inline constexpr EntityID NullEntity = 0;

class Entity;
class Renderer2D;

// Barebones ECS registry. One pool per component type, keyed by entity id.
// Node-based storage keeps component references stable across inserts, which
// keeps the API simple (Get<T>() returns a plain reference you can hold for
// the rest of the frame).
class Scene {
public:
    Scene() = default;
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(EntityID entity);
    bool IsValid(EntityID entity) const;

    const std::vector<EntityID>& GetEntities() const { return m_Entities; }
    size_t GetEntityCount() const { return m_Entities.size(); }

    // Runs scripts (NativeScriptComponent). Called by Application while playing.
    void OnUpdate(float dt);
    // Submits every Transform+Sprite entity to the renderer.
    void OnRender(Renderer2D& renderer);

    // ---- Component access ---------------------------------------------------
    template<typename T, typename... Args>
    T& Add(EntityID entity, Args&&... args) {
        auto& pool = GetPool<T>();
        return pool.Data.insert_or_assign(entity, T{ std::forward<Args>(args)... }).first->second;
    }

    template<typename T>
    T& Get(EntityID entity) { return GetPool<T>().Data.at(entity); }

    template<typename T>
    T* TryGet(EntityID entity) {
        auto* pool = FindPool<T>();
        if (!pool)
            return nullptr;
        auto it = pool->Data.find(entity);
        return it != pool->Data.end() ? &it->second : nullptr;
    }

    template<typename T>
    bool Has(EntityID entity) const {
        const auto* pool = FindPool<T>();
        return pool && pool->Data.count(entity) > 0;
    }

    template<typename T>
    void Remove(EntityID entity) {
        if (auto* pool = FindPool<T>())
            pool->Data.erase(entity);
    }

    // Iterate all entities owning every listed component:
    //   scene.Each<TransformComponent, SpriteComponent>([](EntityID e, auto& t, auto& s) { ... });
    template<typename... Ts, typename Fn>
    void Each(Fn&& fn) {
        for (EntityID e : m_Entities) {
            if ((Has<Ts>(e) && ...))
                fn(e, Get<Ts>(e)...);
        }
    }

private:
    struct IPool {
        virtual ~IPool() = default;
        virtual void Remove(EntityID) = 0;
    };

    template<typename T>
    struct Pool : IPool {
        std::unordered_map<EntityID, T> Data;
        void Remove(EntityID e) override { Data.erase(e); }
    };

    template<typename T>
    Pool<T>& GetPool() {
        auto key = std::type_index(typeid(T));
        auto it = m_Pools.find(key);
        if (it == m_Pools.end())
            it = m_Pools.emplace(key, std::make_unique<Pool<T>>()).first;
        return *static_cast<Pool<T>*>(it->second.get());
    }

    template<typename T>
    Pool<T>* FindPool() const {
        auto it = m_Pools.find(std::type_index(typeid(T)));
        return it != m_Pools.end() ? static_cast<Pool<T>*>(it->second.get()) : nullptr;
    }

    std::unordered_map<std::type_index, std::unique_ptr<IPool>> m_Pools;
    std::vector<EntityID> m_Entities;
    EntityID m_NextID = 1;
};
