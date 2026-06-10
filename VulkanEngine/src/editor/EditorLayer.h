#pragma once

#include "ecs/Scene.h"

class Application;

// The lightweight editor: scene hierarchy, component inspector and an
// engine/stats panel. Deliberately minimal — no asset browsers, animation
// timelines or visual scripting. Toggle with F1 at runtime.
class EditorLayer {
public:
    void OnImGuiRender(Application& app);

private:
    void DrawHierarchy(Application& app);
    void DrawInspector(Application& app);
    void DrawEnginePanel(Application& app);

    EntityID m_Selected = NullEntity;
};
