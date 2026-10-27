#version 440

// The shadow map holds depth only (decision E-903). Cut-out faces (leaves, plants) and entities
// drop the texels the alpha test of their own shaders drops, so light falls through the gaps of a
// crown; solid faces (shadow_solid.frag.qsb, QC_SOLID) write every fragment without a lookup.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in float vAlpha;

layout(binding = 1) uniform sampler2D atlas;

void main()
{
#ifndef QC_SOLID
    // The largest level: the holes of a leaf texture stay holes at any distance from the sun.
    if (textureLod(atlas, vTexCoord, 0.0).a * vAlpha < 0.5)
        discard;
#endif
}
