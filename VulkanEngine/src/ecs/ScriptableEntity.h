#pragma once

#include "ecs/Entity.h"

// Base class for game logic ("behaviors"). Subclass it, override the
// lifecycle hooks, then attach with:
//   entity.Add<NativeScriptComponent>().Bind<MyScript>();
class ScriptableEntity {
public:
    virtual ~ScriptableEntity() = default;

    Entity GetEntity() const { return m_Entity; }

protected:
    virtual void OnCreate() {}
    virtual void OnUpdate(float dt) {}
    virtual void OnDestroy() {}

    template<typename T>
    T& Get() { return m_Entity.Get<T>(); }

private:
    Entity m_Entity;
    friend class Scene;
};
