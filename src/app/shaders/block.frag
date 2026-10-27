#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vLight;
layout(location = 2) in float vDistance;
layout(location = 3) in float vCutout;
// Position relative to the camera (decision E-883), for the footprint where atlas coordinates
// jump between faces.
layout(location = 4) in vec3 vPosition;
// Animated textures (decision E-878): the next frame and the share of it, the same over a face.
layout(location = 5) in vec2 vNextTexCoord;
layout(location = 6) in float vBlend;
// Face shading and ambient occlusion of the vertex (block.vert).
layout(location = 7) in float vShade;
#ifdef QC_SHADOWS
// The parts of vLight and the face's normal (block.vert).
layout(location = 8) in vec4 vLightParts;
layout(location = 9) in vec3 vNormal;
#endif

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
    // Unused here, declared as in block.vert: the stages of a pipeline declare the identical block
    // (QRhi's GL backend makes it a struct uniform that must match across stages; test_frame_uniforms).
    vec4 drawCamera;
    // The moving lights of the realistic mode (decision E-902), the first animation.z of them: xyz
    // relative to the camera, w the level (0..15) plus 16 times the colour (0 warm, 1 glow).
    vec4 lights[16];
} frame;

layout(binding = 1) uniform sampler2D atlas;

// The moving lights (decision E-902), as QcMovingLights::brightness: one level less per block,
// counted from the cell in front of the surface at `p` (relative to the camera) facing `n`, as
// block light reaches a face from the air in front of it; a light more than a block behind the
// surface's plane does not reach it. The brightest one by colour wins, like block light.
const vec3 warmLight = vec3(1.0, 0.86, 0.66);
const vec3 glowLight = vec3(0.78, 1.0, 0.42);

vec3 movingLight(vec3 p, vec3 n, int count)
{
    vec3 sum = vec3(0.0);
    vec3 front = p + 0.5 * n;
    for (int i = 0; i < 16; ++i) {
        if (i >= count)
            break;
        vec4 light = frame.lights[i];
        float colour = floor(light.w / 16.0);
        float level = light.w - 16.0 * colour;
        float facing = clamp(dot(n, light.xyz - p) + 1.0, 0.0, 1.0);
        float brightness = clamp((level - length(light.xyz - front)) / 15.0, 0.0, 1.0) * facing;
        sum = max(sum, brightness * (colour > 0.5 ? glowLight : warmLight));
    }
    return sum;
}

#ifdef QC_SHADOWS
// The sun shadows of the realistic mode (decision E-903), the same code in block.frag and entity.frag.
layout(std140, binding = 3) uniform Shadow {
    // Per cascade: positions relative to the camera into the coordinates of its tile (x, y in
    // 0..1) and the depth stored in the map (QcShadowCascades::textureMatrix).
    mat4 cascades[3];
    // Per cascade: x how far a point moves along its normal, y towards the light before the
    // lookup (blocks), against acne on the faces that cast the shadow.
    vec4 offsets[3];
    // xyz: towards the light, w: the shadows' strength (0: none).
    vec4 toLight;
    // rgb: the light's colour on lit surfaces, w: the share of the sky light left in a shadow.
    vec4 litTint;
    // rgb: the colour of the sky's fill in a shadow, w: the cascade count.
    vec4 shadowTint;
    // x, y: one texel of the map in texture coordinates, z: one texel of a tile, w: lookups per
    // pixel (1, 4 or 9).
    vec4 texel;
    // x, y: the distance from the camera where the shadows begin and end to fade out.
    vec4 fade;
} shadow;

layout(binding = 4) uniform sampler2DShadow shadowMap;

// Hardware-compared lookups around `uv`, each a bilinear 2 x 2 texel filter: 1 alone, 4 half a
// texel apart (a 3 x 3 texel tent), 9 a texel apart (4 x 4 texels).
float shadowFiltered(vec2 uv, float depth)
{
    vec2 step = shadow.texel.xy;
    if (shadow.texel.w < 1.5)
        return texture(shadowMap, vec3(uv, depth));
    if (shadow.texel.w < 4.5) {
        float sum = texture(shadowMap, vec3(uv + vec2(-0.5, -0.5) * step, depth));
        sum += texture(shadowMap, vec3(uv + vec2(0.5, -0.5) * step, depth));
        sum += texture(shadowMap, vec3(uv + vec2(-0.5, 0.5) * step, depth));
        sum += texture(shadowMap, vec3(uv + vec2(0.5, 0.5) * step, depth));
        return 0.25 * sum;
    }
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x)
            sum += texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * step, depth));
    }
    return sum / 9.0;
}

// How much of the light reaches `p` (relative to the camera, `distance` from it) on a surface
// facing `n` (zero when it has no normal): from the first cascade whose tile holds it, faded out
// towards the last tile's edge and the end of the shadow distance; 1 outside every cascade.
float sunVisibility(vec3 p, vec3 n, float distance)
{
    int count = int(shadow.shadowTint.w + 0.5);
    float margin = 2.5 * shadow.texel.z;
    for (int i = 0; i < 3; ++i) {
        if (i >= count)
            break;
        // Without a normal (plants, entities) the whole offset goes towards the light.
        float towards = dot(n, n) > 0.5 ? shadow.offsets[i].y : shadow.offsets[i].x;
        vec3 q = p + n * shadow.offsets[i].x + shadow.toLight.xyz * towards;
        vec4 s = shadow.cascades[i] * vec4(q, 1.0);
        vec2 inside = min(s.xy, 1.0 - s.xy);
        float edge = min(inside.x, inside.y);
        if (edge < margin)
            continue;
        float lit = shadowFiltered(vec2((s.x + float(i)) / float(count), s.y), clamp(s.z, 0.0, 1.0));
        if (i == count - 1)
            lit = mix(1.0, lit, smoothstep(margin, 0.08, edge));
        return mix(lit, 1.0, smoothstep(shadow.fade.x, shadow.fade.y, distance));
    }
    return 1.0;
}

// The colour the sky light of a surface takes: lit where the light reaches it and the surface faces
// it, the shadow's share of the sky light in the shadow's fill colour elsewhere, blended by the
// shadows' strength (white without shadows: the classic light).
vec3 skyColour(vec3 p, vec3 n, float distance)
{
    float strength = shadow.toLight.w;
    if (strength <= 0.0)
        return vec3(1.0);
    float facing = dot(n, n) > 0.5 ? smoothstep(0.0, 0.1, dot(n, shadow.toLight.xyz)) : 1.0;
    float lit = facing > 0.0 ? facing * sunVisibility(p, n, distance) : 0.0;
    vec3 shaded = mix(shadow.shadowTint.rgb * shadow.litTint.w, shadow.litTint.rgb, lit);
    return mix(vec3(1.0), shaded, strength);
}

// The light of the vertex (vLight) from its parts, with the sky light coloured by skyColour();
// the floor and the block light do not change.
vec3 shadowedLight(vec4 parts, vec3 colour)
{
    vec3 sky = max(parts.y * colour, vec3(parts.z));
    return parts.x * max(sky, parts.w * warmLight);
}
#endif

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
    vec3 light = vLight;
#ifdef QC_SHADOWS
    // Without shadows (no light above the horizon, overcast) the light of the vertex as without them.
    if (shadow.toLight.w > 0.0)
        light = shadowedLight(vLightParts, skyColour(vPosition, vNormal, vDistance));
#endif
    // Torches in hands and fireflies (realistic mode only): the surface's normal from the
    // derivatives of its position, turned towards the camera (every drawn face is seen from its
    // front; crossed planes from either side).
    int movingCount = int(frame.animation.z + 0.5);
    vec3 across = cross(stepX, stepY);
    if (movingCount > 0 && dot(across, across) > 1e-30) {
        vec3 n = normalize(across);
        n = dot(n, vPosition) > 0.0 ? -n : n;
        light = max(light, vShade * movingLight(vPosition, n, movingCount));
    }
    vec3 lit = texel.rgb * light;
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
