#version 450
// Stylized shader: diffuse lighting plus a Fresnel-style rim glow.

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
    vec4 params; // x = rim power, y = rim strength
} push;

float lightAttenuation(Light l, vec3 L, float dist) {
    if (l.position.w < 0.5)
        return 1.0;

    float range = max(l.direction.w, 1e-3);
    float atten = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
    atten *= atten;

    if (l.position.w > 1.5)
        atten *= smoothstep(l.cone.y, l.cone.x, dot(-L, normalize(l.direction.xyz)));

    return atten;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.camPos.xyz - fragWorldPos);
    vec3 albedo = push.albedo.rgb * fragColor;

    vec3 result = ubo.ambient.rgb * albedo * 0.7;

    for (int i = 0; i < ubo.lightCount; ++i) {
        Light l = ubo.lights[i];
        vec3 toLight = l.position.xyz - fragWorldPos;
        float dist = length(toLight);
        vec3 L = l.position.w < 0.5 ? normalize(-l.direction.xyz) : toLight / max(dist, 1e-4);
        float atten = lightAttenuation(l, L, dist);
        float diffuse = max(dot(N, L), 0.0);
        result += albedo * diffuse * l.color.rgb * l.color.w * atten;
    }

    float rimPower = max(push.params.x, 1.0);
    float rimStrength = max(push.params.y, 0.05);
    float rim = pow(1.0 - max(dot(N, V), 0.0), rimPower) * rimStrength;
    result += vec3(0.5, 0.8, 1.0) * rim;

    outColor = vec4(pow(result, vec3(1.0 / 2.2)), push.albedo.a);
}
