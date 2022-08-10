#version 460
#extension GL_EXT_nonuniform_qualifier : enable
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inUV;
layout(location = 0) out vec2 outUV;
layout(location = 1) out uint outTexOffset;
layout(location = 2) out mat3 outTBN;
layout(push_constant) uniform PushData {
    mat4 vp;
} push;
struct PerMeshData{
    uint matIdx;
    uint modelMatrixBaseIndex;
};
layout(binding = 0) restrict readonly buffer Ubo0 {
    PerMeshData modelData[];
} ubo0;
layout(binding = 1) restrict readonly buffer Ubo1 {
    mat4 modelMatrices[];
} ubo1;
out gl_PerVertex {
        vec4 gl_Position;
};

void main() {
    outTexOffset = ubo0.modelData[gl_DrawID].matIdx * 3;
    uint modelMatrixIdx = ubo0.modelData[gl_DrawID].modelMatrixBaseIndex + gl_InstanceIndex;
    mat4 modelMatrix = ubo1.modelMatrices[nonuniformEXT(modelMatrixIdx)];
    gl_Position = push.vp * modelMatrix * vec4(inPos, 1.0);
    outUV = vec2(inUV.x, 1.0 - inUV.y);
    vec3 N = normalize(vec3(modelMatrix * vec4(inNormal, 0.0)));
    vec3 T = normalize(vec3(modelMatrix * vec4(inTangent, 0.0)));
    vec3 B = cross(N, T);
    outTBN = mat3(T, B, N);
}	