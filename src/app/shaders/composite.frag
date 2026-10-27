#version 440

// The last pass of the realistic mode (decision E-899): the HDR scene made linear, the height fog
// over it, the bloom added, then exposure and tonemapping (a
// highlight roll-off, see tonemap())
// back to display values in the output.

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Post {
    // Inverse of projection x the camera's rotation: positions relative to the camera (decision E-883).
    mat4 inverseViewProjection;
    // rgb: the fog colour (the horizon, display values); w: the height fog's density per block at
    // its base, already faded in caves (0: no height fog)
    vec4 fogColor;
    // x: the camera's height, y: the base of the height fog, z: how fast it thins above the base
    // (1 / scale height), w: the farthest distance it is gathered over (the end of the classic fog)
    vec4 fog;
    // xyz: towards the sun, w: how much brighter the fog is towards it
    vec4 sun;
    // x: exposure, y: bloom strength, z: 1 when the depth buffer holds the clip depth 0..1
    vec4 tone;
    // xy: one pixel of the output in texture coordinates, z: the distance from the camera where the
    // height fog begins, w: 1 when `occlusion` holds an ambient occlusion (else a white texel)
    vec4 size;
    // xy: one texel of the ambient occlusion (half the output's size) in texture coordinates
    vec4 occlusionTexel;
} post;

layout(binding = 1) uniform sampler2D scene;
layout(binding = 2) uniform sampler2D depth;
layout(binding = 3) uniform sampler2D bloom;
// The ambient occlusion at half size (decision E-902; occlusion.frag), 1 where open.
layout(binding = 4) uniform sampler2D occlusion;

const int fogSteps = 8;
// Below the base the fog thins out again over this many blocks, so caves and sea floors far
// below stay clear.
const float fogFloorStart = 48.0;
const float fogFloorEnd = 16.0;

// Density per block at height `h`: full up to the base, thinning exponentially above it.
float fogDensity(float h)
{
    float base = post.fog.y;
    float above = max(h - base, 0.0);
    float floorShare = smoothstep(base - fogFloorStart, base - fogFloorEnd, h);
    return post.fogColor.w * exp(-post.fog.z * above) * floorShare;
}

// The share of fog on the way from post.size.z to `dist` blocks in direction `dir` from the
// camera (the near ground stays clear), gathered in fogSteps steps: the density follows the
// height only, so the ray needs few samples.
float heightFog(vec3 dir, float dist)
{
    float start = post.size.z;
    if (post.fogColor.w <= 0.0 || dist <= start)
        return 0.0;
    float stepLength = (dist - start) / float(fogSteps);
    float optical = 0.0;
    for (int i = 0; i < fogSteps; ++i)
        optical += fogDensity(post.fog.x + dir.y * (start + stepLength * (float(i) + 0.5)));
    return 1.0 - exp(-optical * stepLength);
}

// Up to this linear value the curve is the identity: mid tones and shadows keep the classic
// brightness; above it highlights roll off smoothly towards 1 (slope 1 at the knee).
const float knee = 0.6;

float rolloff(float x)
{
    return x <= knee ? x : knee + (1.0 - knee) * (1.0 - exp(-(x - knee) / (1.0 - knee)));
}

// Half the curve per channel, half the curve on the brightest channel with the colour scaled by
// it: per channel alone bleaches bright colours towards white, the brightest channel alone
// flattens glowing lava into one saturated orange; the mix keeps the colours of the lit world and
// lets bright light turn towards white.
vec3 tonemap(vec3 x)
{
    float peak = max(x.r, max(x.g, x.b));
    vec3 huePreserving = peak > 1e-5 ? x * (rolloff(peak) / peak) : x;
    vec3 perChannel = vec3(rolloff(x.r), rolloff(x.g), rolloff(x.b));
    return mix(huePreserving, perChannel, 0.5);
}

// The ambient occlusion around `uv`, blurred: four filtered lookups between the half-sized texels
// average 4 x 4 of them, which hides the turn of the samples from pixel to pixel.
float ambientOcclusion(vec2 uv)
{
    if (post.size.w < 0.5)
        return 1.0;
    vec2 t = post.occlusionTexel.xy;
    float sum = texture(occlusion, uv + vec2(-t.x, -t.y)).r + texture(occlusion, uv + vec2(t.x, -t.y)).r
        + texture(occlusion, uv + vec2(-t.x, t.y)).r + texture(occlusion, uv + vec2(t.x, t.y)).r;
    return sum * 0.25;
}

void main()
{
    vec2 uv = gl_FragCoord.xy * post.size.xy;
    vec3 color = pow(max(texture(scene, uv).rgb, vec3(0.0)), vec3(2.2));
    float d = texture(depth, uv).r;
    // The occlusion darkens the light from around, not what shines itself: light sources and lava
    // (above 1 in the HDR scene) keep their brightness, and the sky (depth 1) takes none of the
    // blurred occlusion of the ground next to it.
    float shining = smoothstep(1.0, 2.0, max(color.r, max(color.g, color.b)));
    float ao = d >= 1.0 ? 1.0 : ambientOcclusion(uv);
    color *= mix(ao, 1.0, shining);

    // The pixel's point relative to the camera from its depth; the sky (depth 1) lies on the far
    // plane, beyond the farthest fog distance.
    float z = post.tone.z > 0.5 ? d : d * 2.0 - 1.0;
    vec4 point = post.inverseViewProjection * vec4(vNdc, z, 1.0);
    vec3 relative = point.xyz / point.w;
    float dist = length(relative);
    vec3 dir = relative / max(dist, 1e-4);
    float amount = heightFog(dir, min(dist, post.fog.w));
    vec3 fog = pow(post.fogColor.rgb, vec3(2.2));
    fog *= 1.0 + post.sun.w * pow(max(dot(dir, post.sun.xyz), 0.0), 8.0);
    color = mix(color, fog, amount);

    color += texture(bloom, uv).rgb * post.tone.y;
    if (any(isnan(color)) || any(isinf(color)))
        color = vec3(0.0);
    color = tonemap(color * post.tone.x);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
