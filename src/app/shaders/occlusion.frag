#version 440

// Screen-space ambient occlusion of the realistic mode (decision E-902), at half the scene's size:
// for each pixel the share of sample points in the hemisphere above its surface that the scene's
// depth shows hidden behind nearer geometry. Points are relative to the camera (decision E-883).

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Occlusion {
    // Projection x the camera's rotation and its inverse.
    mat4 viewProjection;
    mat4 inverseViewProjection;
    // x: radius in blocks, y: strength, z: samples, w: 1 when the depth holds the clip depth 0..1
    vec4 params;
    // x, y: the distance where it starts and ends fading out, z: nearest distance (the hand),
    // w: 1 when texture coordinates grow with the clip y (framebuffer and clip y run the same way),
    // else -1
    vec4 view;
    // xy: one texel of the depth texture, zw: one pixel of this target
    vec4 texel;
} occlusion;

layout(binding = 1) uniform sampler2D depth;

const int maxSamples = 16;
// Sample points start this far above the surface, against self-occlusion of flat faces.
const float lift = 0.03;

vec2 uvOf(vec2 ndc)
{
    return vec2(0.5 + 0.5 * ndc.x, 0.5 + 0.5 * ndc.y * occlusion.view.w);
}

// The scene's point (relative to the camera) seen at `uv`.
vec3 pointAt(vec2 uv)
{
    float d = textureLod(depth, uv, 0.0).r;
    float z = occlusion.params.w > 0.5 ? d : d * 2.0 - 1.0;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * occlusion.view.w);
    vec4 point = occlusion.inverseViewProjection * vec4(ndc, z, 1.0);
    return point.xyz / point.w;
}

// The smaller of the two differences towards the neighbours: the side that lies on the same surface
// at an edge.
vec3 closer(vec3 p, vec3 before, vec3 after)
{
    vec3 forward = after - p;
    vec3 backward = p - before;
    return dot(forward, forward) < dot(backward, backward) ? forward : backward;
}

// Interleaved gradient noise: a different turn of the samples for neighbouring pixels, which the
// composite's blur averages out.
float noise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main()
{
    // The centre of a depth texel this half-sized pixel covers: the depth is read unfiltered, and
    // the neighbours one texel away then lie on whole texels too.
    vec2 uv = (floor(gl_FragCoord.xy) * 2.0 + 0.5) * occlusion.texel.xy;
    if (textureLod(depth, uv, 0.0).r >= 1.0) {
        fragColor = vec4(1.0);
        return;
    }
    vec3 p = pointAt(uv);
    float dist = length(p);
    float fade = 1.0 - smoothstep(occlusion.view.x, occlusion.view.y, dist);
    if (isnan(dist) || isinf(dist) || dist < occlusion.view.z || fade <= 0.0) {
        fragColor = vec4(1.0);
        return;
    }

    vec2 tx = vec2(occlusion.texel.x, 0.0);
    vec2 ty = vec2(0.0, occlusion.texel.y);
    vec3 dx = closer(p, pointAt(uv - tx), pointAt(uv + tx));
    vec3 dy = closer(p, pointAt(uv - ty), pointAt(uv + ty));
    vec3 n = cross(dx, dy);
    if (dot(n, n) <= 1e-30) {
        fragColor = vec4(1.0);
        return;
    }
    n = normalize(n);
    n = dot(n, p) > 0.0 ? -n : n;
    vec3 helper = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, n));
    vec3 bitangent = cross(n, tangent);

    float radius = occlusion.params.x;
    int count = int(occlusion.params.z + 0.5);
    float turn = 6.2831853 * noise(gl_FragCoord.xy);
    float hidden = 0.0;
    for (int i = 0; i < maxSamples; ++i) {
        if (i >= count)
            break;
        // A spiral over the hemisphere, denser towards the normal (cosine-weighted), the sample
        // points nearer the surface more often.
        float t = (float(i) + 0.5) / float(count);
        float angle = float(i) * 2.3999632 + turn;
        float sine = sqrt(t);
        vec3 dir = (tangent * cos(angle) + bitangent * sin(angle)) * sine + n * sqrt(1.0 - t);
        float reach = radius * mix(0.2, 1.0, fract(float(i) * 0.618034 + turn * 0.159155));
        vec3 q = p + n * lift + dir * reach;
        vec4 clip = occlusion.viewProjection * vec4(q, 1.0);
        if (clip.w <= 1e-4)
            continue;
        vec2 sampleUv = uvOf(clip.xy / clip.w);
        if (any(lessThan(sampleUv, vec2(0.0))) || any(greaterThan(sampleUv, vec2(1.0))))
            continue;
        float scene = length(pointAt(sampleUv));
        // The hand drawn in front of the world hides nothing.
        if (scene < occlusion.view.z)
            continue;
        float point = length(q);
        // Hidden when the scene lies in front of the sample point, counted fully only when that
        // geometry is near the pixel (not a far wall in front of a distant hill).
        if (scene < point - lift)
            hidden += smoothstep(0.0, 1.0, radius / max(abs(dist - scene), 1e-4));
    }

    float open = 1.0 - occlusion.params.y * fade * hidden / float(max(count, 1));
    fragColor = vec4(vec3(clamp(open, 0.0, 1.0)), 1.0);
}
