#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable
layout(location = 0) in vec2 inUV;
layout(location = 1) in flat uint inTexOffset;
layout(location = 2) in mat3 inTBN;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outSpecular;
layout(location = 2) out vec4 outNormal;
layout(binding = 2) uniform sampler2D samplers[];

void main() {
    vec2 uv = vec2(inUV.x, inUV.y);
    outColor = texture(samplers[nonuniformEXT(inTexOffset)], uv);
    outSpecular = texture(samplers[nonuniformEXT(inTexOffset + 2)], uv);
    vec3 tNormal = inTBN * normalize(texture(samplers[nonuniformEXT(inTexOffset + 1)], uv).xyz * 2.0 - 1.0);
    outNormal = vec4(tNormal, 1.0);
}