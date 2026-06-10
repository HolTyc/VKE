#version 450
// Example of a user-injected custom shader: banded "toon" lighting with a rim term.

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

struct Light {
    vec4 position;   // w = type (0 dir, 1 point, 2 spot)
    vec4 color;      // w = intensity
    vec4 direction;  // w = range
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
    vec4 params;
} push;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.camPos.xyz - fragWorldPos);
    vec3 albedo = push.albedo.rgb * fragColor;

    vec3 result = ubo.ambient.rgb * albedo;
    const float bands = 3.0;

    for (int i = 0; i < ubo.lightCount; ++i) {
        Light l = ubo.lights[i];
        vec3 L;
        float atten = 1.0;

        if (l.position.w < 0.5) {
            L = normalize(-l.direction.xyz);
        } else {
            vec3 toLight = l.position.xyz - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);
            float range = max(l.direction.w, 1e-3);
            atten = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
            atten *= atten;
            if (l.position.w > 1.5) // spot: fade between inner and outer cone
                atten *= smoothstep(l.cone.y, l.cone.x, dot(-L, normalize(l.direction.xyz)));
        }

        float d = max(dot(N, L), 0.0) * atten;
        d = floor(d * bands + 0.5) / bands;
        result += albedo * d * l.color.rgb * l.color.w;
    }

    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    result += vec3(rim * 0.3);

    outColor = vec4(pow(result, vec3(1.0 / 2.2)), push.albedo.a);
}
