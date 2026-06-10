#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <string>

namespace vke {

class Mesh;

// Base class for all components attached to an Entity.
struct Component {
    virtual ~Component() = default;
};

// Position / rotation (Euler angles, degrees) / scale. Every entity has one.
struct Transform : Component {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // pitch (x), yaw (y), roll (z) in degrees
    glm::vec3 scale{1.0f};

    glm::mat4 rotationMatrix() const {
        glm::mat4 r{1.0f};
        r = glm::rotate(r, glm::radians(rotation.y), {0.0f, 1.0f, 0.0f});
        r = glm::rotate(r, glm::radians(rotation.x), {1.0f, 0.0f, 0.0f});
        r = glm::rotate(r, glm::radians(rotation.z), {0.0f, 0.0f, 1.0f});
        return r;
    }

    glm::mat4 matrix() const {
        glm::mat4 m = glm::translate(glm::mat4{1.0f}, position) * rotationMatrix();
        return glm::scale(m, scale);
    }

    glm::vec3 forward() const { return glm::vec3(rotationMatrix() * glm::vec4(0, 0, -1, 0)); }
    glm::vec3 right()   const { return glm::vec3(rotationMatrix() * glm::vec4(1, 0, 0, 0)); }
    glm::vec3 up()      const { return glm::vec3(rotationMatrix() * glm::vec4(0, 1, 0, 0)); }
};

// Perspective camera. The renderer uses the first entity whose camera has primary == true.
struct CameraComponent : Component {
    float fov      = 60.0f;  // vertical field of view, degrees
    float nearClip = 0.1f;
    float farClip  = 500.0f;
    bool  primary  = true;
};

struct LightComponent : Component {
    enum class Type { Directional = 0, Point = 1, Spot = 2 };

    Type      type = Type::Point;
    glm::vec3 color{1.0f};
    float     intensity = 1.0f;
    float     range     = 10.0f; // point/spot lights: falloff radius
    // Spot lights only: cone half-angles in degrees (direction = transform forward).
    float     innerAngle = 12.0f; // full brightness inside this
    float     outerAngle = 25.0f; // fades to zero at this
};

// Simple material: a tint, Blinn-Phong parameters and the name of a registered shader.
// Advanced users can register custom GLSL shader pairs via Renderer3D::registerShader.
struct Material {
    glm::vec4   albedo{1.0f};
    float       shininess = 32.0f;
    float       specular  = 0.5f;
    std::string shader    = "basic";
};

struct MeshRendererComponent : Component {
    std::shared_ptr<Mesh> mesh;
    Material material;
};

} // namespace vke
