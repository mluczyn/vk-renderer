#pragma once
#include <string>
const std::string offscreenVertexShaderText = R"(
    #version 450
    layout(location = 0) in vec3 inPos;
    layout(location = 1) in vec3 inNormal;
    layout(location = 2) in vec3 inTangent;
    layout(location = 3) in vec2 inUV;
    layout(location = 0) out vec2 outUV;
    layout(location = 1) out mat3 outTBN;
    layout(location = 4) out vec4 outWorldPos;

    layout(push_constant) uniform PushData {
        mat4 model;
        mat4 view;
        mat4 proj;
    } push;

    out gl_PerVertex {
            vec4 gl_Position;
    };
    
    void main() {
        gl_Position = push.proj * push.view * push.model * vec4(inPos, 1.0);
        outUV = inUV;
        vec3 N = normalize(vec3(push.model * vec4(inNormal, 0.0)));
        vec3 T = normalize(vec3(push.model * vec4(inTangent, 0.0)));
        vec3 B = cross(N, T);
        outTBN = mat3(T, B, N);
        outWorldPos = push.model * vec4(inPos, 1.0);
    }	
)";

const std::string offscreenFragmentShaderText = R"(
    #version 450
    layout(location = 0) in vec2 inUV;
    layout(location = 1) in mat3 inTBN;
    layout(location = 4) in vec4 inWorldPos;
    layout(location = 0) out vec4 outColor;
    layout(location = 1) out vec4 outSpecular;
    layout(location = 2) out vec4 outPos;
    layout(location = 3) out vec4 outNormal;

    layout(binding = 0) uniform sampler2D samplers[3];

    void main() {
        outColor = texture(samplers[0], inUV);
        outSpecular = texture(samplers[2], inUV);
        outPos = inWorldPos;
        vec3 tNormal = inTBN * normalize(texture(samplers[1], inUV).xyz * 2.0 - 1.0);
        outNormal = vec4(tNormal, 1.0);
    }
)";

const std::string deferredVertexShaderText = R"(
    #version 450
    layout (location = 0) out vec2 outUV;

    void main() {
	    outUV = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
	    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
    }
)";

const std::string deferredFragmentShaderText = R"(
    #version 450
    #define SAMPLE_COUNT 4
    #define LIGHT_COUNT 2

    layout (location = 0) in vec2 inUV;
    layout (location = 0) out vec4 outColor;

    layout (push_constant) uniform PushData {
        vec3 cameraPos;
    } push; 

    layout (binding = 0) uniform sampler2DMS samplers[4];
    struct Light {
        vec4 pos;
        vec3 color;
        float radius;
    };
    layout (binding = 1) uniform UBO {
        Light lights[LIGHT_COUNT];
    } ubo;

    void main() {
        ivec2 UV = ivec2(inUV * textureSize(samplers[0]));

        vec3 albedo = vec3(0.0);
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            albedo += texelFetch(samplers[0], UV, i).rgb;
        }
        albedo /= float(SAMPLE_COUNT);

        const float ambient = 0.15;

        vec3 diffuse = vec3(0.0);
        vec3 specular = vec3(0.0);
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            const vec3 fragPos = texelFetch(samplers[2], UV, i).rgb;
            const vec3 normal = normalize(texelFetch(samplers[3], UV, i).rgb);
            const vec3 spec = texelFetch(samplers[1], UV, i).rgb;

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
        }
        diffuse /= float(SAMPLE_COUNT);
        specular /= float(SAMPLE_COUNT);

        vec3 fragColor = specular + diffuse + ambient * albedo;
        outColor = vec4(fragColor, 1.0);
    }
)";