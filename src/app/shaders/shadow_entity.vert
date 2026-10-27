#version 440

// The depth of creatures, players and dropped items seen from the sun (decision E-903), as
// shadow.vert for the entity meshes (QcEntityVertex).

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
layout(location = 4) in vec4 tint;
layout(location = 6) in vec4 origin;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out float vAlpha;

layout(std140, binding = 0) uniform Caster {
    mat4 lightViewProjection;
    vec4 drawCamera;
} caster;

void main()
{
    vTexCoord = texCoord;
    vAlpha = tint.a;
    vec3 relative = position + origin.xyz - caster.drawCamera.xyz;
    gl_Position = caster.lightViewProjection * vec4(relative, 1.0);
}
