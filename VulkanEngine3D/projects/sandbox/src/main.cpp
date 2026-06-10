// VKE sandbox — demonstrates the full user-facing API:
//   * launching the engine (Application subclass + AppConfig)
//   * creating entities and attaching components (Transform, MeshRenderer, Camera, Light)
//   * loading meshes (built-in primitives; Renderer3D::loadModel("models/foo.obj") for OBJ files)
//   * injecting a custom GLSL shader pair and assigning it to a material
//   * per-frame game logic (onUpdate) and custom ImGui windows (onGui)
//   * the editor GUI: hierarchy, inspector, stats, RMB+WASD fly camera

#include <vke/vke.hpp>

#include <imgui.h>

#include <array>

class DemoApp : public vke::Application {
public:
    using vke::Application::Application;

protected:
    void onStart() override {
        using namespace vke;

        // ---- camera ------------------------------------------------------
        auto& camera = scene().createEntity("Editor Camera");
        camera.transform().position = {0.0f, 3.0f, 8.0f};
        camera.transform().rotation = {-15.0f, 0.0f, 0.0f};
        camera.add<CameraComponent>().fov = 60.0f;

        // ---- lights ------------------------------------------------------
        auto& sun = scene().createEntity("Sun");
        sun.transform().rotation = {-50.0f, 30.0f, 0.0f}; // direction = transform forward
        auto& sunLight = sun.add<LightComponent>();
        sunLight.type = LightComponent::Type::Directional;
        sunLight.color = {1.0f, 0.96f, 0.9f};
        sunLight.intensity = 1.2f;

        auto& lamp = scene().createEntity("Blue Lamp");
        lamp.transform().position = {2.5f, 2.5f, 2.0f};
        auto& lampLight = lamp.add<LightComponent>();
        lampLight.type = LightComponent::Type::Point;
        lampLight.color = {0.3f, 0.55f, 1.0f};
        lampLight.intensity = 4.0f;
        lampLight.range = 12.0f;

        // ---- geometry ----------------------------------------------------
        auto& ground = scene().createEntity("Ground");
        ground.transform().scale = {14.0f, 1.0f, 14.0f};
        auto& groundMr = ground.add<MeshRendererComponent>();
        groundMr.mesh = renderer().primitive(Primitive::Plane);
        groundMr.material.albedo = {0.45f, 0.45f, 0.5f, 1.0f};
        groundMr.material.shininess = 8.0f;
        groundMr.material.specular = 0.1f;

        auto& cube = scene().createEntity("Spinning Cube");
        cube.transform().position = {-1.8f, 0.75f, 0.0f};
        cube.transform().scale = glm::vec3{2.5f};
        auto& cubeMr = cube.add<MeshRendererComponent>();
        cubeMr.mesh = renderer().primitive(Primitive::Cube);
        cubeMr.material.albedo = {0.9f, 0.35f, 0.1f, 1.0f};
        cubeMr.material.shininess = 100.0f;
        cubeId_ = cube.id();

        // ---- custom shader injection (advanced API) ------------------------
        // GLSL sources live in assets/shaders/; CMake compiles them to SPIR-V.
        renderer().registerShader("toon", "shaders/toon.vert.spv", "shaders/toon.frag.spv");
        renderer().registerShader("normal", "shaders/basic.vert.spv", "shaders/normal.frag.spv");
        renderer().registerShader("rim", "shaders/basic.vert.spv", "shaders/rim.frag.spv");
        renderer().registerShader("checker", "shaders/basic.vert.spv", "shaders/checker.frag.spv");
        renderer().registerShader("matcap", "shaders/basic.vert.spv", "shaders/matcap.frag.spv");

        struct ShaderDemo {
            const char* name;
            const char* shader;
            glm::vec3 position;
            glm::vec4 albedo;
            float shininess;
            float specular;
        };

        const std::array shaderDemos = {
            ShaderDemo{"Toon Sphere", "toon", {2.0f, 1.0f, -2.8f}, {0.2f, 0.8f, 0.45f, 1.0f}, 32.0f, 0.5f},
            ShaderDemo{"Normal Sphere", "normal", {4.1f, 1.0f, -1.4f}, {1.0f, 1.0f, 1.0f, 1.0f}, 32.0f, 0.5f},
            ShaderDemo{"Rim Sphere", "rim", {4.1f, 1.0f, 1.4f}, {0.35f, 0.4f, 0.95f, 1.0f}, 3.0f, 0.9f},
            ShaderDemo{"Checker Sphere", "checker", {2.0f, 1.0f, 2.8f}, {0.95f, 0.75f, 0.25f, 1.0f}, 8.0f, 0.15f},
            ShaderDemo{"Matcap Sphere", "matcap", {-0.2f, 1.0f, 3.4f}, {0.75f, 0.35f, 0.28f, 1.0f}, 0.8f, 0.35f},
        };

        for (const auto& demo : shaderDemos) {
            auto& sphere = scene().createEntity(demo.name);
            sphere.transform().position = demo.position;
            sphere.transform().scale = glm::vec3{1.35f};
            auto& sphereMr = sphere.add<MeshRendererComponent>();
            sphereMr.mesh = renderer().primitive(Primitive::Sphere);
            sphereMr.material.shader = demo.shader;
            sphereMr.material.albedo = demo.albedo;
            sphereMr.material.shininess = demo.shininess;
            sphereMr.material.specular = demo.specular;
        }

        // To load an external model instead:
        //   auto& mr = entity.add<MeshRendererComponent>();
        //   mr.mesh = renderer().loadModel("models/teapot.obj");
    }

    void onUpdate(float dt) override {
        // Note: in RenderMode::OnDemand frames only run on input events, so
        // continuous animation like this is a Continuous-mode feature.
        if (auto* cube = scene().find(cubeId_))
            cube->transform().rotation.y += 40.0f * dt;
    }

    void onGui() override {
        // User ImGui windows render alongside the editor panels.
        ImGui::Begin("Demo Controls");
        ImGui::TextWrapped("This window comes from DemoApp::onGui(). "
                           "Use the Hierarchy to add entities and the Inspector "
                           "to tweak components.");
        if (auto* cube = scene().find(cubeId_)) {
            auto* mr = cube->get<vke::MeshRendererComponent>();
            ImGui::ColorEdit4("Cube color", &mr->material.albedo.x);
        }
        ImGui::End();
    }

private:
    uint32_t cubeId_ = 0;
};

int main() {
    vke::AppConfig config;
    config.title = "VKE Sandbox — Vulkan 3D Engine";
    config.width = 1600;
    config.height = 900;
    config.mode = vke::RenderMode::Continuous; // OnDemand for CAD-style apps
    config.editor = true;

    DemoApp app(config);
    app.run();
    return 0;
}
