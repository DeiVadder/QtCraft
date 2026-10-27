#version 440

// The depth of section faces seen from the sun (decision E-903): one cascade of the shadow map per
// draw, its matrix at the uniform buffer's dynamic offset.

// Relative to the section's corner (QcChunkMesh::origin), as in block.vert.
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
// Per draw (QcDrawOrigins, decision E-883): xyz the section's corner relative to the render anchor.
layout(location = 6) in vec4 origin;

layout(location = 0) out vec2 vTexCoord;
// The alpha the atlas texel is multiplied with before the alpha test (1 for blocks).
layout(location = 1) out float vAlpha;

layout(std140, binding = 0) uniform Caster {
    // Positions relative to the camera into the cascade's clip space.
    mat4 lightViewProjection;
    // xyz: the camera relative to the render anchor (QcDrawOrigins::follow).
    vec4 drawCamera;
} caster;

void main()
{
    // The first frame of an animated texture: its alpha is the same in every frame of leaves.
    vTexCoord = texCoord;
    vAlpha = 1.0;
    vec3 relative = position + origin.xyz - caster.drawCamera.xyz;
    gl_Position = caster.lightViewProjection * vec4(relative, 1.0);
}
