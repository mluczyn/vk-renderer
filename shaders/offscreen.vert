#version 460
#extension GL_EXT_debug_printf : enable
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inUV;
layout(location = 0) out vec2 outUV;
layout(location = 1) out uint outTexOffset;
layout(location = 2) out mat3 outTBN;
layout(location = 5) out vec4 outWorldPos;
layout(push_constant) uniform PushData {
    mat4 model;
    mat4 mvp;
} push;
layout(binding = 0) restrict readonly buffer PerModelData {
    uint matIdx[];
} perModelData;
out gl_PerVertex {
        vec4 gl_Position;
};

void main() {
    outTexOffset = perModelData.matIdx[gl_DrawID] * 3;
    gl_Position = push.mvp * vec4(inPos, 1.0);
    outUV = vec2(inUV.x, 1.0 - inUV.y);
    vec3 N = normalize(vec3(push.model * vec4(inNormal, 0.0)));
    vec3 T = normalize(vec3(push.model * vec4(inTangent, 0.0)));
    vec3 B = cross(N, T);
    outTBN = mat3(T, B, N);
    outWorldPos = push.model * vec4(inPos, 1.0);
}	