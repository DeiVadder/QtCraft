// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

// The water of the realistic mode (decision E-906), with the block vertex shader of the shadow
// receivers (block_shadow.vert.qsb): moving waves in the normal, Fresnel between what lies behind
// the surface and what it mirrors, the sky, the sun or moon and (High) the world in the mirror,
// the picture behind bent by the waves and coloured by the water it crosses, caustics on the
// ground under shallow water, the sun's shadows on the surface (water_shadow.frag.qsb, QC_SHADOWS).
// The pixel's whole colour is written: it mixes in what lies behind from the copy of the scene.

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vLight;
layout(location = 2) in float vDistance;
layout(location = 3) in float vCutout;
layout(location = 4) in vec3 vPosition;
layout(location = 5) in vec2 vNextTexCoord;
layout(location = 6) in float vBlend;
layout(location = 7) in float vShade;
layout(location = 8) in vec4 vLightParts;
layout(location = 9) in vec3 vNormal;

layout(location = 0) out vec4 fragColor;

// As in block.frag (the stages of a pipeline declare the identical block).
layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    vec4 params;
    vec4 extra;
    vec4 atlas;
    vec4 animation;
    vec4 drawCamera;
    vec4 lights[16];
} frame;

layout(binding = 1) uniform sampler2D atlas;

#ifdef QC_SHADOWS
// The sun shadows (decision E-903), the same code as in block.frag.
layout(std140, binding = 3) uniform Shadow {
    mat4 cascades[3];
    vec4 offsets[3];
    vec4 toLight;
    vec4 litTint;
    vec4 shadowTint;
    vec4 texel;
    vec4 fade;
    // The material maps (decision E-908): x 1 while they are bound, y 1 for the moving lights'
    // highlights, z the strength of the relief, w of the highlights and reflections.
    vec4 material;
} shadow;

layout(binding = 4) uniform sampler2DShadow shadowMap;

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

float sunVisibility(vec3 p, vec3 n, float distance)
{
    int count = int(shadow.shadowTint.w + 0.5);
    float margin = 2.5 * shadow.texel.z;
    for (int i = 0; i < 3; ++i) {
        if (i >= count)
            break;
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

const vec3 warmLight = vec3(1.0, 0.86, 0.66);

vec3 shadowedLight(vec4 parts, vec3 colour)
{
    vec3 sky = max(parts.y * colour, vec3(parts.z));
    return parts.x * max(sky, parts.w * warmLight);
}
#endif

// The copy of the scene (water_behind.frag): colour, and the distance from the camera in alpha.
layout(binding = 5) uniform sampler2D behind;

// QcWaterUniforms.
layout(std140, binding = 6) uniform Water {
    mat4 inverseViewProjection;
    // rgb: the sky's zenith, w: the waves' time in seconds (repeating every loopSeconds)
    vec4 zenith;
    // rgb: the sky's horizon, w: 1 for the clip depth 0..1
    vec4 horizon;
    // rgb: the glow around the sun, w: the y of texture coordinates per y of NDC (+1 or -1)
    vec4 glow;
    // xyz: towards the sun or the moon, w: its strength (0: no highlight)
    vec4 toLight;
    // rgb: the light's colour
    vec4 lightColour;
    // xyz: the camera's world position modulo 64 blocks
    vec4 camera;
    // xy: one pixel in texture coordinates, z: wave octaves, w: refraction strength
    vec4 pixel;
    // x: steps of the screen-space reflections, y: strength of the caustics
    vec4 effects;
    // rgb: what the water takes of each colour per block (linear)
    vec4 extinction;
    // rgb: the water around a camera under water (display values)
    vec4 deep;
} water;

const float twoPi = 6.2831853;
// Every frequency in time is a whole multiple of 1 / loopSeconds, so the time can wrap without a jump
// (QcWaterLook::waveLoopSeconds).
const float loopSeconds = 600.0;
// The share of light the surface mirrors head on.
const float baseReflectance = 0.02;
// The distance the copy of the scene gives the sky (water_behind.frag).
const float farDistance = 60000.0;

// `speed` (radians per second) rounded to a whole number of turns per loop.
float looped(float speed)
{
    return floor(speed * loopSeconds / twoPi + 0.5) * twoPi / loopSeconds;
}

// The slope of the waves at `p` (world x, z modulo 64 blocks; every wave has a whole number of
// crests per 16 blocks, so the pattern repeats with the camera's modulo): pairs of waves crossing
// at an angle, each octave half as long, low and flat like the waves of a lake; the shorter waves
// fade out where a pixel covers more than a quarter of their length (no shimmer in the distance).
vec2 waveSlope(vec2 p, float footprint)
{
    const vec2 crests[8] = vec2[](vec2(2.0, 1.0), vec2(-1.0, 2.0), vec2(3.0, -2.0), vec2(-2.0, -3.0), vec2(5.0, 3.0), vec2(-4.0, 5.0),
                                  vec2(8.0, -5.0), vec2(-7.0, -8.0));
    float t = water.zenith.w;
    vec2 slope = vec2(0.0);
    int octaves = int(water.pixel.z + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= 2 * octaves)
            break;
        vec2 k = crests[i] * (twoPi / 16.0);
        float wavelength = twoPi / max(sqrt(dot(k, k)), 1e-3);
        float shown = 1.0 - smoothstep(0.25, 0.5, footprint / wavelength);
        float steepness = 0.035 * shown;
        float phase = dot(k, p) - looped(sqrt(1.6 * sqrt(dot(k, k)))) * t;
        slope += steepness * wavelength / twoPi * k * cos(phase);
    }
    return slope;
}

// Bright lines where light bent by the waves gathers on the ground under shallow water: three
// crossing waves, each warped by another; only the sharp tops of their sum stay. Periodic in 8
// blocks.
float caustic(vec2 p)
{
    vec2 q = p * (twoPi / 8.0);
    float t = water.zenith.w;
    float v = sin(2.0 * q.x + 1.3 * sin(q.y + looped(0.8) * t) + looped(0.6) * t);
    v += sin(2.0 * q.y + 1.3 * sin(q.x - looped(0.7) * t) - looped(0.5) * t);
    v += sin(q.x + q.y + 1.1 * sin(2.0 * (q.x - q.y) + looped(0.9) * t));
    return pow(1.0 - abs(v) / 3.0, 8.0);
}

// The sky in direction `dir`, as sky.frag's dome (without sun, moon and clouds), in display values.
vec3 skyDome(vec3 dir)
{
    float up = smoothstep(-0.02, 0.55, dir.y);
    vec3 colour = mix(water.horizon.rgb, water.zenith.rgb, up);
    vec2 across = normalize(dir.xz + vec2(1e-5, 0.0));
    vec2 sunAcross = normalize(water.toLight.xz + vec2(1e-5, 0.0));
    float towardsSun = max(dot(across, sunAcross), 0.0);
    return colour + water.glow.rgb * pow(towardsSun, 5.0) * (1.0 - smoothstep(0.0, 0.45, abs(dir.y)));
}

// The distance in the copy at `uv`, read at the centre of its texel: a filtered read would blend the
// distances of a near and a far surface at a silhouette into one between them.
float distanceAt(vec2 uv)
{
    return textureLod(behind, (floor(uv / water.pixel.xy) + 0.5) * water.pixel.xy, 0.0).a;
}

// Texture coordinates of the copy for a point relative to the camera; outside 0..1 when off screen.
vec2 screenOf(vec3 p, out float w)
{
    vec4 clip = frame.viewProjection * vec4(p, 1.0);
    w = clip.w;
    vec2 ndc = clip.xy / max(clip.w, 1e-4);
    return vec2(ndc.x * 0.5 + 0.5, ndc.y * 0.5 * water.glow.w + 0.5);
}

// What the mirror shows of the world drawn before the water (High: water.effects.x steps along the
// mirrored ray in growing strides); alpha is how sure the hit is (0: none, the sky stays).
vec4 screenReflection(vec3 p, vec3 r)
{
    int steps = int(water.effects.x + 0.5);
    float stride = 0.4;
    float travelled = 0.3;
    for (int i = 0; i < 32; ++i) {
        if (i >= steps)
            break;
        vec3 q = p + r * travelled;
        float w;
        vec2 uv = screenOf(q, w);
        if (w <= 0.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
            break;
        vec4 seen = vec4(textureLod(behind, uv, 0.0).rgb, distanceAt(uv));
        float depth = length(q) - seen.a;
        if (depth > 0.0 && depth < stride + 0.5 && seen.a < farDistance) {
            vec2 edge = min(uv, 1.0 - uv);
            float sure = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * (1.0 - float(i) / float(max(steps, 1)));
            return vec4(seen.rgb, sure);
        }
        travelled += stride;
        stride *= 1.2;
    }
    return vec4(0.0);
}

vec3 linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
    // Derivatives before any branch that depends on the pixel.
    vec2 dx = dFdx(vTexCoord);
    vec2 dy = dFdy(vTexCoord);
    vec3 stepX = dFdx(vPosition);
    vec3 stepY = dFdy(vPosition);
    float footprint = sqrt(max(dot(stepX, stepX), dot(stepY, stepY)));
    float fog = clamp((vDistance - frame.params.x) / max(frame.params.y - frame.params.x, 0.001), 0.0, 1.0);
    vec4 texel = textureGrad(atlas, vTexCoord, dx, dy);
    if (vBlend > 0.0)
        texel = mix(texel, textureGrad(atlas, vNextTexCoord, dx, dy), vBlend);

    vec3 view = vPosition / max(vDistance, 1e-4);
    vec3 face = dot(vNormal, vNormal) > 0.5 ? vNormal : vec3(0.0, 1.0, 0.0);
    // Waves on the top surface only; the falling sides keep their face.
    vec3 n = face;
    if (face.y > 0.5) {
        vec2 slope = waveSlope(vPosition.xz + water.camera.xz, footprint);
        n = normalize(vec3(-slope.x, 1.0, -slope.y));
    }
    bool fromBelow = dot(face, view) > 0.0;
    vec3 towardsEye = fromBelow ? -n : n;

    vec3 light = vLight;
    float sunLit = 1.0;
#ifdef QC_SHADOWS
    if (shadow.toLight.w > 0.0) {
        light = shadowedLight(vLightParts, skyColour(vPosition, face, vDistance));
        sunLit = sunVisibility(vPosition, face, vDistance);
    }
#endif
    // The colour the water scatters: the water texture (with its biome and pack colours) in the
    // light that reaches the surface.
    vec3 scattered = linear(texel.rgb * light) * 0.55;

    // What lies behind, bent by the waves; what lies in front of the surface is not bent into it.
    vec2 uv = gl_FragCoord.xy * water.pixel.xy;
    vec2 bend = (n.xz - face.xz) * water.pixel.w * 0.35 * 8.0 / (8.0 + vDistance);
    vec4 seen = vec4(texture(behind, uv + bend).rgb, distanceAt(uv + bend));
    if (seen.a < vDistance)
        seen = vec4(texture(behind, uv).rgb, distanceAt(uv));
    vec3 through = linear(seen.rgb);
    float cosine = clamp(dot(towardsEye, -view), 0.0, 1.0);

    vec3 colour;
    if (fromBelow) {
        // From below: the world above through Snell's window, beyond it the surface mirrors the
        // water below (total reflection), eased over the window's rim.
        float sine = 1.33 * sqrt(1.0 - cosine * cosine);
        float partial = baseReflectance + (1.0 - baseReflectance) * pow(1.0 - cosine, 5.0);
        float mirrored = mix(partial, 1.0, smoothstep(0.85, 1.0, sine));
        colour = mix(through, linear(water.deep.rgb), mirrored);
    } else {
        float crossed = max(seen.a - vDistance, 0.0);
        // Caustics on the ground under shallow water (Medium and High), where the sun reaches it.
        if (water.effects.y > 0.0 && seen.a < farDistance && crossed < 12.0) {
            vec3 ground = view * seen.a;
            float sunOnGround = water.toLight.w;
#ifdef QC_SHADOWS
            sunOnGround *= shadow.toLight.w > 0.0 ? sunVisibility(ground, vec3(0.0, 1.0, 0.0), seen.a) : 1.0;
#endif
            float depthBelow = max(vPosition.y - ground.y, 0.0);
            float strength = water.effects.y * sunOnGround * smoothstep(0.0, 0.5, depthBelow) * (1.0 - smoothstep(4.0, 12.0, depthBelow));
            through *= 1.0 + 1.6 * strength * caustic(ground.xz + water.camera.xz + water.toLight.xz * depthBelow);
        }
        vec3 kept = exp(-water.extinction.rgb * min(crossed, 400.0));
        vec3 refracted = through * kept + scattered * (1.0 - kept);

        // The mirror: the sky where the surface sees it (sky light), the world drawn before the water
        // on High.
        vec3 r = reflect(view, towardsEye);
        r.y = abs(r.y);
        float skyAccess = smoothstep(0.4, 0.95, vLightParts.y / max(frame.params.z, 1e-3));
        vec3 mirrored = linear(skyDome(r)) * mix(0.15, 1.0, skyAccess);
        if (water.effects.x > 0.0) {
            vec4 world = screenReflection(vPosition + towardsEye * 0.05, r);
            mirrored = mix(mirrored, linear(world.rgb), world.a);
        }
        float fresnel = baseReflectance + (1.0 - baseReflectance) * pow(1.0 - cosine, 5.0);
        colour = mix(refracted, mirrored, fresnel);

        // The sun's (or moon's) highlight, a sharp core in a softer glitter; none in a shadow.
        float facing = max(dot(r, normalize(water.toLight.xyz)), 0.0);
        float highlight = 6.0 * pow(facing, 1500.0) + 0.35 * pow(facing, 90.0);
        colour += linear(water.lightColour.rgb) * highlight * water.toLight.w * sunLit * skyAccess * mix(0.3, 1.0, fresnel);
    }

    vec3 display = pow(max(colour, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(mix(display, frame.fogColor.rgb, fog), 1.0);
}
