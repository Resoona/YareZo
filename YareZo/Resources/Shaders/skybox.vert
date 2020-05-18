#version 450

layout (location = 0) in vec3 inPos;

layout (binding = 0) uniform UBO
{
    mat4 model;
    mat4 projection;
} ubo;

layout (location = 0) out vec3 outUVW;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    outUVW = inPos;
    outUVW.x *= -1.0;
    gl_Position = ubo.projection * ubo.model * vec4(inPos.xyz, 1.0);
}