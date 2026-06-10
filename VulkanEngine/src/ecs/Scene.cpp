#include "ecs/Scene.h"
#include "ecs/Entity.h"
#include "ecs/Components.h"
#include "render/Renderer2D.h"

#include <algorithm>

Scene::~Scene() {
    // Give scripts their OnDestroy before pools are torn down.
    for (EntityID e : m_Entities) {
        if (auto* nsc = TryGet<NativeScriptComponent>(e)) {
            if (nsc->Instance)
                nsc->Instance->OnDestroy();
        }
    }
}

Entity Scene::CreateEntity(const std::string& name) {
    EntityID id = m_NextID++;
    m_Entities.push_back(id);
    Add<TagComponent>(id, name);
    Add<TransformComponent>(id);
    return Entity(id, this);
}

void Scene::DestroyEntity(EntityID entity) {
    auto it = std::find(m_Entities.begin(), m_Entities.end(), entity);
    if (it == m_Entities.end())
        return;

    if (auto* nsc = TryGet<NativeScriptComponent>(entity)) {
        if (nsc->Instance)
            nsc->Instance->OnDestroy();
    }
    for (auto& [type, pool] : m_Pools)
        pool->Remove(entity);
    m_Entities.erase(it);
}

bool Scene::IsValid(EntityID entity) const {
    return std::find(m_Entities.begin(), m_Entities.end(), entity) != m_Entities.end();
}

void Scene::OnUpdate(float dt) {
    // Iterate a snapshot so scripts can create/destroy entities mid-update.
    std::vector<EntityID> snapshot = m_Entities;
    for (EntityID e : snapshot) {
        auto* nsc = TryGet<NativeScriptComponent>(e);
        if (!nsc)
            continue;
        if (!nsc->Instance && nsc->Instantiate) {
            nsc->Instance = nsc->Instantiate();
            nsc->Instance->m_Entity = Entity(e, this);
            nsc->Instance->OnCreate();
        }
        if (nsc->Instance)
            nsc->Instance->OnUpdate(dt);
    }
}

void Scene::OnRender(Renderer2D& renderer) {
    Each<TransformComponent, SpriteComponent>(
        [&](EntityID, TransformComponent& transform, SpriteComponent& sprite) {
            renderer.DrawQuad(transform.Position, transform.Scale, transform.Rotation,
                              sprite.Color, sprite.Tex);
        });
}
