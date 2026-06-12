#include <vke/Script.hpp>
#include <vke/Keys.hpp>

#include <glm/geometric.hpp>

class KeyboardMover : public vke::Script {
public:
    float speed = 5.0f;
    float turnSpeed = 120.0f;

    void onUpdate(float dt) override {
        auto& t = transform();

        if (keyDown(vke::Key::A)) t.rotation.y += turnSpeed * dt;
        if (keyDown(vke::Key::D)) t.rotation.y -= turnSpeed * dt;

        glm::vec3 move{0.0f};
        if (keyDown(vke::Key::W)) move += t.forward();
        if (keyDown(vke::Key::S)) move -= t.forward();

        if (glm::length(move) > 0.001f) {
            t.position += glm::normalize(move) * speed * dt;
        }
        if (keyDown(vke::Key::Space)) {
          transform().position.y += 4.0f * dt;
        }

        if (mouseButtonDown(vke::Mouse::Left)) {
          // Fire, select, interact, etc.
        }
    }

    VKE_SCRIPT(KeyboardMover,
        VKE_PROPERTY(speed),
        VKE_PROPERTY(turnSpeed))
};
