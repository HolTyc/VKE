#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

struct Light {
    vec4 position;   // xyz = world position, w = type (0 dir, 1 point, 2 spot)
    vec4 color;      // rgb = color, w = intensity
    vec4 direction;  // xyz = forward direction, w = range
    vec4 cone;       // x = cos(innerAngle), y = cos(outerAngle) (spot lights)
};

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 ambient;
    Light lights[8];
    int lightCount;
} ubo;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 albedo;
    vec4 params; // x = shininess, y = specular strength
} push;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.camPos.xyz - fragWorldPos);
    vec3 albedo = push.albedo.rgb * fragColor;

    vec3 result = ubo.ambient.rgb * albedo;

    for (int i = 0; i < ubo.lightCount; ++i) {
        Light l = ubo.lights[i];
        vec3 L;
        float atten = 1.0;

        if (l.position.w < 0.5) {
            // Directional light
            L = normalize(-l.direction.xyz);
        } else {
            // Point light with smooth range falloff
            vec3 toLight = l.position.xyz - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);
            float range = max(l.direction.w, 1e-3);
            atten = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
            atten *= atten;
            if (l.position.w > 1.5) // spot: fade between inner and outer cone
                atten *= smoothstep(l.cone.y, l.cone.x, dot(-L, normalize(l.direction.xyz)));
        }

        float ndotl = max(dot(N, L), 0.0);
        vec3  H     = normalize(L + V);
        float spec  = pow(max(dot(N, H), 0.0), max(push.params.x, 1.0)) * push.params.y;

        vec3 radiance = l.color.rgb * l.color.w * atten;
        result += (albedo * ndotl + vec3(spec) * ndotl) * radiance;
    }

    // Manual gamma correction (swapchain uses a UNORM format so ImGui colors stay exact)
    outColor = vec4(pow(result, vec3(1.0 / 2.2)), push.albedo.a);
}
