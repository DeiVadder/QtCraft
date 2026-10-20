#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vLight;
layout(location = 2) in float vDistance;
layout(location = 3) in float vCutout;
// World position, for the footprint where atlas coordinates jump between faces.
layout(location = 4) in vec3 vPosition;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    vec4 params;
    vec4 extra;
    // x, y: the atlas's grid of cells, z: pixels per cell at its largest level, w: most samples
    // along a slanted footprint
    vec4 atlas;
} frame;

layout(binding = 1) uniform sampler2D atlas;

// Upper bound of the sample loop (frame.atlas.w is at most this).
const int maxSamples = 16;
// A cell is 10 units: a gutter of 1, the tile of 8, a gutter of 1 (QcAtlasLayout); the last mip
// level whose tiles do not mix with their neighbours has 10 pixels per cell.
const float gutterShare = 0.1;
const float tileShare = 0.8;
const float lastCellPixels = 10.0;

// The atlas filtered along the pixel's footprint (decision E-721): QRhi has no anisotropic
// samplers, so where a surface is seen at a flat angle the shader takes up to frame.atlas.w samples
// along the long side of the footprint, at the mip level of its short side, and averages them.
// Samples wrap within the tile (neighbouring faces repeat it) and the level stays at or above the
// last one whose tiles do not mix, so far away grass does not pick up the colours of other tiles.
// A face shows one tile, so atlas coordinates that change by more than a tile between neighbouring
// pixels come from two faces (a cube edge, distant faces side by side): there the level follows the
// distance in the world between the pixels, with one sample.
// `alphaBias` > 0 takes the alpha of minified texels from that many levels further down (at most
// one per level of minification), for a calmer cut-out edge (decision E-724).
vec4 sampleAtlas(vec2 uv, vec3 position, float alphaBias)
{
    vec2 grid = frame.atlas.xy;
    float tilePixels = frame.atlas.z * tileShare;
    vec2 pixels = grid * frame.atlas.z;
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    float worldStep = max(length(dFdx(position)), length(dFdy(position)));
    float lengthX = length(dx * pixels);
    float lengthY = length(dy * pixels);
    float major = max(lengthX, lengthY);
    float minor = max(min(lengthX, lengthY), 1e-4);
    float lastLevel = log2(max(frame.atlas.z / lastCellPixels, 1.0));

    vec4 texel;
    float lod;
    if (major > tilePixels) {
        lod = clamp(log2(max(worldStep * tilePixels, 1.0)), 0.0, lastLevel);
        texel = textureLod(atlas, uv, lod);
    } else {
        float samples = clamp(ceil(major / minor), 1.0, frame.atlas.w);
        lod = clamp(log2(max(major / samples, 1.0)), 0.0, lastLevel);
        if (samples < 1.5) {
            texel = textureLod(atlas, uv, lod);
        } else {
            vec2 axis = lengthX >= lengthY ? dx : dy;
            vec2 tileOrigin = (floor(uv * grid) + gutterShare) / grid;
            vec2 tileSize = tileShare / grid;
            vec4 sum = vec4(0.0);
            for (int i = 0; i < maxSamples; ++i) {
                if (float(i) >= samples)
                    break;
                vec2 p = uv + axis * ((float(i) + 0.5) / samples - 0.5);
                sum += textureLod(atlas, tileOrigin + mod(p - tileOrigin, tileSize), lod);
            }
            texel = sum / samples;
        }
    }
    if (alphaBias > 0.0 && lod > 0.0)
        texel.a = textureLod(atlas, uv, min(lod + alphaBias * min(lod, 1.0), lastLevel)).a;
    return texel;
}

void main()
{
    vec4 texel = sampleAtlas(vTexCoord, vPosition, vCutout);
    if (texel.a < 0.1)
        discard;
    vec3 lit = texel.rgb * vLight;
    float fog = clamp((vDistance - frame.params.x) / max(frame.params.y - frame.params.x, 0.001), 0.0, 1.0);
#ifdef QC_OPAQUE
    // Opaque and cut-out blocks (block_opaque.frag.qsb, no blending): a drawn texel is opaque,
    // whatever alpha its mip level gives it (decision E-332).
    fragColor = vec4(mix(lit, frame.fogColor.rgb, fog), 1.0);
#else
    fragColor = vec4(mix(lit, frame.fogColor.rgb, fog), texel.a);
#endif
}
