#version 460
#define LIGHT_COUNT 2
layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;
layout (push_constant) uniform PushData {
    vec3 cameraPos;
    mat4 inverseVP;
} push; 
layout (binding = 0) uniform sampler2D samplers[4];
struct Light {
    vec4 pos;
    vec3 color;
    float radius;
};
layout (binding = 1) uniform UBO {
    Light lights[LIGHT_COUNT];
} ubo;
void main() {
    vec2 UV = inUV;
    vec3 albedo = texture(samplers[0], UV).rgb;
    const float ambient = 0.15;
    vec3 diffuse = vec3(0.0, 0.0, 0.0);
    vec3 specular = vec3(0.0, 0.0, 0.0);
    //const vec3 fragPos = vec3((push.inverseVP * vec4(2.0 * UV - 1.0,0.0,1.0)).rg, texture(samplers[2],UV).b);
    const vec3 fragPos = texture(samplers[2], UV).rgb;
    const vec3 normal = normalize(texture(samplers[3], UV).rgb);
    const vec3 spec = texture(samplers[1], UV).rgb;
    for(int j = 0; j < LIGHT_COUNT; j++) {
        vec3 lightDir = ubo.lights[j].pos.xyz - fragPos;
        float dist = length(lightDir);
        lightDir = normalize(lightDir);
        float atten = ubo.lights[j].radius / (pow(dist, 2.0) + 1.0);
        diffuse += albedo * ubo.lights[j].color * atten * max(0.0, dot(normal, lightDir));
        vec3 cameraDir = push.cameraPos - fragPos;
        vec3 mid = normalize(lightDir + cameraDir);
        float specInt = max(0.0, dot(mid, normal));
        specular += spec * ubo.lights[j].color * atten * pow(specInt, 8.0);
    }
    vec3 fragColor = specular + diffuse + ambient * albedo;
    outColor = vec4(fragColor, 1.0);
}