#version 440

// Relative to the section's corner (QcChunkMesh::origin): 0 to 16 along each axis.
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
// x: face shading and ambient occlusion, y: sky light, z: block light (unsigned normalised bytes);
// w: the surface byte (decision E-390: normal in the low three bits, material class in the high five).
layout(location = 2) in vec4 light;
// Per draw (QcDrawOrigins, decision E-883): xyz the section's corner relative to the render anchor,
// w the corner modulo cellPeriod in sixteens of a block, packed x + 64 y + 4096 z (QcRenderOrigin).
layout(location = 6) in vec4 origin;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vLight;
layout(location = 2) out float vDistance;
// 1 on alpha-tested blocks drawn in the opaque pass (leaves), else 0.
layout(location = 3) out float vCutout;
// Relative to the camera, for the footprint where atlas coordinates jump between faces.
layout(location = 4) out vec3 vPosition;
// Animated textures (decision E-878): the atlas coordinates of the next frame and how far the
// picture has blended into it (0: only vTexCoord), the same at every vertex of a face.
layout(location = 5) out vec2 vNextTexCoord;
layout(location = 6) out float vBlend;
// Face shading and ambient occlusion of the vertex, for the moving lights of block.frag.
layout(location = 7) out float vShade;

layout(std140, binding = 0) uniform Frame {
    // The projection of the camera's rotation: positions are relative to the camera (decision E-883).
    mat4 viewProjection;
    // The camera in that space: always the origin.
    vec4 cameraPosition;
    vec4 fogColor;
    // x: fog start, y: fog end, z: daylight factor, w: minimum light
    vec4 params;
    // w: floor light per unit of sky light
    vec4 extra;
    // x, y: the atlas's grid of cells
    vec4 atlas;
    // x: the animation time in ticks, wrapped where every animation starts over; y: the gain of
    // light sources and lava (above 1 in the HDR picture of the realistic mode, decision E-899)
    vec4 animation;
    // xyz: the camera relative to the render anchor (QcDrawOrigins::follow)
    vec4 drawCamera;
    vec4 lights[16];
} frame;

// One texel per atlas cell (QcTextureAtlas::animationMap): at the cell of an animated texture's
// first frame the frame count (+ 128 with interpolation), the frame time in ticks and the tile
// index of its second frame in two bytes; 0 for still textures.
layout(binding = 2) uniform sampler2D animations;

// Block light (torches) is warm and does not follow the time of day.
const vec3 blockTint = vec3(1.0, 0.86, 0.66);
// QcSurfaceMaterial::Cutout in the high five bits of the surface byte.
const float cutoutMaterial = 1.0;
// QcSurfaceMaterial::Liquid (lava) and Emissive (light sources).
const float glowingLiquidMaterial = 5.0;
const float emissiveMaterial = 6.0;
// Brightness of leaves varies by this much from corner to corner (decision E-724).
const float foliageVariation = 0.25;
// The world coordinates of the corner hash repeat after this many blocks
// (QcRenderOrigin::hashPeriod), so the hash stays exact in floats however far out the section lies.
const float cellPeriod = 1024.0;

// The section's corner modulo cellPeriod, unpacked from origin.w (whole sixteens: exact, as are
// the divisions by powers of two).
vec3 sectionCell()
{
    float packed = origin.w;
    float z = floor(packed / 4096.0);
    float y = floor((packed - z * 4096.0) / 64.0);
    float x = packed - z * 4096.0 - y * 64.0;
    return vec3(x, y, z) * 16.0;
}

// A value in [0, 1) for a block corner, the same for every face that shares it.
float cornerHash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1) * 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Divisions and floors below are biased away from whole numbers: GPUs may divide as a * rcp(b)
// (Metal fast math, Direct3D), so a quotient that should be whole can land just below it
// (test_texture_animation_shader ports this to float32 and checks it).
const float wholeBias = 1e-4;

// The atlas coordinates `uv` moved from the cell `first` into the cell of tile `index`; the tile
// index is whole, so the quotient (index + 0.5) / columns stays half a column clear of the row's
// edges.
vec2 inTile(vec2 uv, vec2 first, float index, float columns, vec2 grid)
{
    float row = floor((index + 0.5) / columns);
    vec2 cell = vec2(index - row * columns, row);
    return uv + (cell - first) / grid;
}

// Picks the frame of an animated texture by the time, as QcTextureAnimation::frameAt does: the
// first frame stays in the mesh, the others are cells further on in the atlas, so an animation
// needs no new mesh and no upload.
void animate(vec2 uv)
{
    vTexCoord = uv;
    vNextTexCoord = uv;
    vBlend = 0.0;
    // The map has one texel per cell of the grid (a 1 x 1 map of zeros without an atlas); read at
    // the texel's centre with the nearest filter (no texelFetch in GLSL ES 1.00).
    vec2 grid = frame.atlas.xy;
    vec2 cell = floor(uv * grid);
    vec4 entry = floor(textureLod(animations, (cell + 0.5) / grid, 0.0) * 255.0 + 0.5);
    float frames = entry.r - 128.0 * step(128.0, entry.r);
    if (frames < 2.0)
        return;

    // The time is wrapped on the CPU (QcWorldView::setAnimationTime); mod() may still give the full
    // loop or a hair below 0 instead of 0, which the corrections of the frame index absorb.
    float position = mod(frame.animation.x, frames * entry.g) / entry.g;
    float index = floor(position + wholeBias);
    index += frames * (step(index, -0.5) - step(frames, index));
    float next = index + 1.0;
    next -= frames * step(frames, next);
    float later = entry.b + 256.0 * entry.a - 1.0;
    float columns = grid.x;
    if (index > 0.5)
        vTexCoord = inTile(uv, cell, later + index, columns, grid);
    vNextTexCoord = next > 0.5 ? inTile(uv, cell, later + next, columns, grid) : uv;
    vBlend = entry.r >= 128.0 ? clamp(position - floor(position + wholeBias), 0.0, 1.0) : 0.0;
}

void main()
{
    animate(texCoord);
    // The floor rises with the sky light (decision E-723): nights in the open and under trees are
    // not black, caves without sky light keep the minimum.
    float sky = max(light.y * frame.params.z, frame.params.w + light.y * frame.extra.w);
    vLight = light.x * max(vec3(sky), light.z * blockTint);
    float material = floor(floor(light.w * 255.0 + 0.5) / 8.0);
    vCutout = material == cutoutMaterial ? 1.0 : 0.0;
    // Leaves get a little lighter and darker from corner to corner, so a crown is not one flat
    // green (decision E-724).
    vLight *= 1.0 - vCutout * foliageVariation * cornerHash(mod(position + sectionCell(), cellPeriod));
    // Light sources and lava shine beyond white in the realistic mode, so that they bloom; the
    // classic mode multiplies by exactly 1.
    if (material == emissiveMaterial || material == glowingLiquidMaterial)
        vLight *= frame.animation.y;
    // Both offsets from the anchor are small floats: the difference is exact to a few hundredths of a
    // millimetre near the camera.
    vShade = light.x;
    vec3 relative = position + origin.xyz - frame.drawCamera.xyz;
    vDistance = length(relative - frame.cameraPosition.xyz);
    vPosition = relative;
    gl_Position = frame.viewProjection * vec4(relative, 1.0);
}
