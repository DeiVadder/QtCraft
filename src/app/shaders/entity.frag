#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vLight;
layout(location = 2) in float vDistance;
layout(location = 3) in vec4 vTint;
#ifdef QC_SHADOWS
// Where the shadow map is read, the parts of vLight and the moving lights (entity.vert, hand.vert).
layout(location = 4) in vec3 vShadowAt;
layout(location = 5) in vec4 vLightParts;
layout(location = 6) in vec3 vMoving;
#endif

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    vec4 params;
    vec4 extra;
    // The members of the vertex stages' block (entity.vert, hand.vert), unused here: the stages of a
    // pipeline declare the identical block (QRhi's GL backend makes it a struct uniform that must
    // match across stages; test_frame_uniforms).
    vec4 atlas;
    vec4 animation;
    vec4 drawCamera;
    vec4 lights[16];
} frame;

layout(binding = 1) uniform sampler2D atlas;

#ifdef QC_SHADOWS
const vec3 warmLight = vec3(1.0, 0.86, 0.66);
#endif
#ifdef QC_SHADOWS
// The classic fog. Defined before the shadow functions so that the Metal shader keeps the frame
// block at buffer 0 as block.frag does: QRhi does not bind the buffers again when the next pipeline
// uses the same bindings, and the block and entity pipelines share the receivers' bindings
// (decision E-903).
float fogAt(float distance)
{
    return clamp((distance - frame.params.x) / max(frame.params.y - frame.params.x, 0.001), 0.0, 1.0);
}
#endif
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
    // The material maps (decision E-908): x 1 while they are bound, y 1 for the moving lights'
    // highlights, z the strength of the relief, w of the highlights and reflections.
    vec4 material;
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

void main()
{
    vec4 texel = texture(atlas, vTexCoord) * vTint;
    // QcSpriteShape::minOpaqueAlpha follows this threshold: held sprites get sides on every drawn texel.
    if (texel.a < 0.1)
        discard;
#ifdef QC_SHADOWS
    float fog = fogAt(vDistance);
    vec3 light = vLight;
    // Entities have no normal here: the lookup moves towards the light only. Without shadows the
    // light of the vertex as without them.
    if (shadow.toLight.w > 0.0)
        light = max(shadowedLight(vLightParts, skyColour(vShadowAt, vec3(0.0), length(vShadowAt))), vMoving);
    vec3 lit = texel.rgb * light;
#else
    vec3 lit = texel.rgb * vLight;
    float fog = clamp((vDistance - frame.params.x) / max(frame.params.y - frame.params.x, 0.001), 0.0, 1.0);
#endif
    // Drawn without blending: a drawn texel is opaque, whatever alpha its mip level gives it
    // (decision E-332; a screenshot or the transparent target of a preview reads the alpha).
    fragColor = vec4(mix(lit, frame.fogColor.rgb, fog), 1.0);
}
