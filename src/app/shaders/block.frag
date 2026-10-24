#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vLight;
layout(location = 2) in float vDistance;
layout(location = 3) in float vCutout;
// World position, for the footprint where atlas coordinates jump between faces.
layout(location = 4) in vec3 vPosition;
// Animated textures (decision E-878): the next frame and the share of it, the same over a face.
layout(location = 5) in vec2 vNextTexCoord;
layout(location = 6) in float vBlend;

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
    vec4 animation;
} frame;

layout(binding = 1) uniform sampler2D atlas;

// Upper bound of the sample loop (frame.atlas.w is at most this): a constant bound the compiler
// can unroll.
const int maxSamples = 8;
// A cell is 10 units: a gutter of 1, the tile of 8, a gutter of 1 (QcAtlasLayout); the last mip
// level whose tiles do not mix with their neighbours has 10 pixels per cell.
const float gutterShare = 0.1;
const float tileShare = 0.8;
const float lastCellPixels = 10.0;

// Takes the alpha of a minified texel from up to `alphaBias` levels further down (at most one per
// level of minification), for a calmer cut-out edge (decision E-724); `lod` is the texel's level.
vec4 calmCutout(vec4 texel, vec2 uv, float lod, float alphaBias, float lastLevel)
{
    if (alphaBias > 0.0 && lod > 0.0)
        texel.a = textureLod(atlas, uv, min(lod + alphaBias * min(lod, 1.0), lastLevel)).a;
    return texel;
}

// The atlas filtered along the pixel's footprint (decision E-721): QRhi has no anisotropic
// samplers, so where a surface is seen at a flat angle the shader takes several samples along the
// long side of the footprint, at the mip level of its short side, and averages them. The count is
// a power of two below the ratio of the long side to the short one (1 below a ratio of 2), at most
// frame.atlas.w; a footprint of at most one texel (near surfaces) takes one sample of level 0,
// frame.atlas.w = 1 (the setting "Off") is the plain mip-mapped lookup of the full chain: cheapest,
// with the rings on far grass and without the calmer leaf edges.
// Samples wrap within the tile (neighbouring faces repeat it) and the level never goes past the
// last one whose tiles do not mix, so far away grass does not pick up the colours of other tiles.
// A face shows one tile, so atlas coordinates that change by more than a tile between neighbouring
// pixels come from two faces (a cube edge, distant faces side by side): there the level follows the
// distance in the world between the pixels (`worldStep`), with one sample.
// Every lookup takes its level or its derivatives (`dx`, `dy`: of the atlas coordinates along the
// screen axes) from the caller, which computes them in uniform control flow: the second lookup of
// a blended face runs only on some pixels of a 2 x 2 quad (decision E-878).
vec4 sampleAtlas(vec2 uv, vec2 dx, vec2 dy, float worldStep, float alphaBias)
{
    if (frame.atlas.w < 1.5)
        return textureGrad(atlas, uv, dx, dy);

    vec2 grid = frame.atlas.xy;
    vec2 pixels = grid * frame.atlas.z;
    vec2 texelsX = dx * pixels;
    vec2 texelsY = dy * pixels;
    float squareX = dot(texelsX, texelsX);
    float squareY = dot(texelsY, texelsY);
    if (max(squareX, squareY) <= 1.0)
        return textureLod(atlas, uv, 0.0);

    float tilePixels = frame.atlas.z * tileShare;
    float lastLevel = log2(max(frame.atlas.z / lastCellPixels, 1.0));
    float majorSquare = max(squareX, squareY);
    if (majorSquare > tilePixels * tilePixels) {
        float edgeLod = clamp(log2(max(worldStep * tilePixels, 1.0)), 0.0, lastLevel);
        return calmCutout(textureLod(atlas, uv, edgeLod), uv, edgeLod, alphaBias, lastLevel);
    }

    // Logarithms of the squares halved: no square roots.
    float majorLog = 0.5 * log2(majorSquare);
    float ratioLog = majorLog - 0.5 * log2(max(min(squareX, squareY), 1e-8));
    float samplesLog = min(floor(ratioLog), log2(frame.atlas.w));
    float lod = clamp(majorLog - samplesLog, 0.0, lastLevel);
    if (samplesLog < 0.5)
        return calmCutout(textureLod(atlas, uv, lod), uv, lod, alphaBias, lastLevel);

    // Along the axis in tile units: the samples wrap with fract() within [0, 1) of the tile.
    int count = int(exp2(samplesLog) + 0.5);
    vec2 axis = (squareX >= squareY ? dx : dy) * grid / tileShare;
    vec2 cell = floor(uv * grid);
    vec2 inTile = (uv * grid - cell - gutterShare) / tileShare;
    vec2 first = inTile - axis * (0.5 - 0.5 / float(count));
    vec2 step = axis / float(count);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < maxSamples; ++i) {
        if (i >= count)
            break;
        vec2 p = fract(first + step * float(i));
        sum += textureLod(atlas, (cell + gutterShare + p * tileShare) / grid, lod);
    }
    return calmCutout(sum / float(count), uv, lod, alphaBias, lastLevel);
}

void main()
{
    // Derivatives before any branch that depends on the pixel. The next frame of a blended face is
    // the same picture moved by whole cells, so it has the derivatives of vTexCoord.
    vec2 dx = dFdx(vTexCoord);
    vec2 dy = dFdy(vTexCoord);
    vec3 stepX = dFdx(vPosition);
    vec3 stepY = dFdy(vPosition);
    float worldStep = sqrt(max(dot(stepX, stepX), dot(stepY, stepY)));
    vec4 texel = sampleAtlas(vTexCoord, dx, dy, worldStep, vCutout);
    if (vBlend > 0.0)
        texel = mix(texel, sampleAtlas(vNextTexCoord, dx, dy, worldStep, vCutout), vBlend);
#ifndef QC_SOLID
    if (texel.a < 0.1)
        discard;
#endif
    vec3 lit = texel.rgb * vLight;
    float fog = clamp((vDistance - frame.params.x) / max(frame.params.y - frame.params.x, 0.001), 0.0, 1.0);
#ifdef QC_OPAQUE
    // Solid blocks (block_solid.frag.qsb, no discard: the GPU rejects hidden fragments before
    // shading them, decision E-756) and cut-out blocks (block_opaque.frag.qsb), both without
    // blending: a drawn texel is opaque, whatever alpha its mip level gives it (decision E-332).
    fragColor = vec4(mix(lit, frame.fogColor.rgb, fog), 1.0);
#else
    fragColor = vec4(mix(lit, frame.fogColor.rgb, fog), texel.a);
#endif
}
