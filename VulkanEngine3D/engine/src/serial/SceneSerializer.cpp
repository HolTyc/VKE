#include "vke/SceneSerializer.hpp"

#include "vke/Renderer3D.hpp"
#include "vke/Scene.hpp"
#include "vke/Script.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace vke {

using json = nlohmann::json;

namespace {

json toJson(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
json toJson(const glm::vec4& v) { return json::array({v.x, v.y, v.z, v.w}); }

glm::vec3 vec3From(const json& j, glm::vec3 fallback = glm::vec3{0.0f}) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

glm::vec4 vec4From(const json& j, glm::vec4 fallback = glm::vec4{0.0f}) {
    if (!j.is_array() || j.size() < 4) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

const char* lightTypeName(LightComponent::Type t) {
    switch (t) {
        case LightComponent::Type::Directional: return "directional";
        case LightComponent::Type::Point:       return "point";
        case LightComponent::Type::Spot:        return "spot";
    }
    return "point";
}

LightComponent::Type lightTypeFrom(const std::string& s) {
    if (s == "directional") return LightComponent::Type::Directional;
    if (s == "spot")        return LightComponent::Type::Spot;
    return LightComponent::Type::Point;
}

json propValueToJson(const PropValue& v) {
    return std::visit(
        [](const auto& value) -> json {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, glm::vec3>)
                return toJson(value);
            else
                return value;
        },
        v);
}

// JSON carries no PropType, so decode by JSON type; ScriptHost::applyProps
// later drops values whose type no longer matches the script member.
PropValue propValueFromJson(const json& j) {
    if (j.is_boolean())         return j.get<bool>();
    if (j.is_number_integer())  return j.get<int>();
    if (j.is_number_float())    return j.get<float>();
    if (j.is_array())           return vec3From(j);
    return j.get<std::string>();
}

} // namespace

std::string SceneSerializer::serialize(uint32_t skipEntityId) const {
    json root;
    root["version"] = 1;
    json& entities = root["entities"] = json::array();

    for (const auto& e : scene_.entities()) {
        if (e->id() == skipEntityId) continue;

        json je;
        je["name"] = e->name;

        const Transform& t = e->transform();
        je["transform"] = {{"position", toJson(t.position)},
                           {"rotation", toJson(t.rotation)},
                           {"scale", toJson(t.scale)}};

        if (const auto* cam = e->get<CameraComponent>())
            je["camera"] = {{"fov", cam->fov},
                            {"near", cam->nearClip},
                            {"far", cam->farClip},
                            {"primary", cam->primary}};

        if (const auto* light = e->get<LightComponent>())
            je["light"] = {{"type", lightTypeName(light->type)},
                           {"color", toJson(light->color)},
                           {"intensity", light->intensity},
                           {"range", light->range},
                           {"innerAngle", light->innerAngle},
                           {"outerAngle", light->outerAngle}};

        if (const auto* mr = e->get<MeshRendererComponent>()) {
            json jm;
            jm["mesh"] = mr->mesh ? mr->mesh->source : "";
            jm["material"] = {{"albedo", toJson(mr->material.albedo)},
                              {"shininess", mr->material.shininess},
                              {"specular", mr->material.specular},
                              {"shader", mr->material.shader}};
            je["meshRenderer"] = std::move(jm);
        }

        if (const auto* sc = e->get<ScriptComponent>(); sc && !sc->slots.empty()) {
            json& scripts = je["scripts"] = json::array();
            for (const auto& slot : sc->slots) {
                json js;
                js["type"] = slot.type;
                json& props = js["props"] = json::object();
                for (const auto& [name, value] : slot.props)
                    props[name] = propValueToJson(value);
                scripts.push_back(std::move(js));
            }
        }

        entities.push_back(std::move(je));
    }

    return root.dump(2);
}

bool SceneSerializer::deserialize(const std::string& text, std::string& error) {
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        error = "invalid JSON";
        return false;
    }
    if (!root.contains("entities") || !root["entities"].is_array()) {
        error = "missing \"entities\" array";
        return false;
    }

    renderer_.waitIdle(); // in-flight frames may still reference scene meshes
    scene_.clear();

    for (const json& je : root["entities"]) {
        Entity& e = scene_.createEntity(je.value("name", "Entity"));

        if (je.contains("transform")) {
            const json& jt = je["transform"];
            Transform& t = e.transform();
            t.position = vec3From(jt.value("position", json::array()));
            t.rotation = vec3From(jt.value("rotation", json::array()));
            t.scale    = vec3From(jt.value("scale", json::array()), glm::vec3{1.0f});
        }

        if (je.contains("camera")) {
            const json& jc = je["camera"];
            auto& cam = e.add<CameraComponent>();
            cam.fov      = jc.value("fov", 60.0f);
            cam.nearClip = jc.value("near", 0.1f);
            cam.farClip  = jc.value("far", 500.0f);
            cam.primary  = jc.value("primary", true);
        }

        if (je.contains("light")) {
            const json& jl = je["light"];
            auto& light = e.add<LightComponent>();
            light.type       = lightTypeFrom(jl.value("type", "point"));
            light.color      = vec3From(jl.value("color", json::array()), glm::vec3{1.0f});
            light.intensity  = jl.value("intensity", 1.0f);
            light.range      = jl.value("range", 10.0f);
            light.innerAngle = jl.value("innerAngle", 12.0f);
            light.outerAngle = jl.value("outerAngle", 25.0f);
        }

        if (je.contains("meshRenderer")) {
            const json& jm = je["meshRenderer"];
            auto& mr = e.add<MeshRendererComponent>();

            const std::string source = jm.value("mesh", "");
            if (source == "primitive:cube")
                mr.mesh = renderer_.primitive(Primitive::Cube);
            else if (source == "primitive:sphere")
                mr.mesh = renderer_.primitive(Primitive::Sphere);
            else if (source == "primitive:plane")
                mr.mesh = renderer_.primitive(Primitive::Plane);
            else if (source.rfind("model:", 0) == 0)
                mr.mesh = renderer_.loadModel(source.substr(6));

            if (jm.contains("material")) {
                const json& mat = jm["material"];
                mr.material.albedo    = vec4From(mat.value("albedo", json::array()), glm::vec4{1.0f});
                mr.material.shininess = mat.value("shininess", 32.0f);
                mr.material.specular  = mat.value("specular", 0.5f);
                mr.material.shader    = mat.value("shader", "basic");
            }
        }

        if (je.contains("scripts")) {
            auto& sc = e.add<ScriptComponent>();
            for (const json& js : je["scripts"]) {
                ScriptSlot slot;
                slot.type = js.value("type", "");
                if (js.contains("props"))
                    for (const auto& [name, jv] : js["props"].items())
                        slot.props.emplace_back(name, propValueFromJson(jv));
                sc.slots.push_back(std::move(slot));
            }
        }
    }

    return true;
}

bool SceneSerializer::saveToFile(const std::filesystem::path& path,
                                 uint32_t skipEntityId) const {
    std::ofstream out(path);
    if (!out) return false;
    out << serialize(skipEntityId);
    return static_cast<bool>(out);
}

bool SceneSerializer::loadFromFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path.string();
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return deserialize(text, error);
}

} // namespace vke
