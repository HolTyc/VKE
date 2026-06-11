#include <vke/Script.hpp>

// Spins the entity it is attached to. Edit `speed` in the Inspector, or change
// this file and hit "Reload Scripts" (Ctrl+R) in the editor to see it live.
class Rotator : public vke::Script {
public:
    float speed = 90.0f; // degrees per second

    void onUpdate(float dt) override {
        transform().rotation.y += speed * dt;
    }

    VKE_SCRIPT(Rotator, VKE_PROPERTY(speed))
};
