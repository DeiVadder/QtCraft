#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vLight;
layout(location = 2) in float vDistance;
layout(location = 3) in vec4 vTint;

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
} frame;

layout(binding = 1) uniform sampler2D atlas;

void main()
{
    vec4 texel = texture(atlas, vTexCoord) * vTint;
    // QcSpriteShape::minOpaqueAlpha follows this threshold: held sprites get sides on every drawn texel.
    if (texel.a < 0.1)
        discard;
    vec3 lit = texel.rgb * vLight;
    float fog = clamp((vDistance - frame.params.x) / max(frame.params.y - frame.params.x, 0.001), 0.0, 1.0);
    // Drawn without blending: a drawn texel is opaque, whatever alpha its mip level gives it
    // (decision E-332; a screenshot or the transparent target of a preview reads the alpha).
    fragColor = vec4(mix(lit, frame.fogColor.rgb, fog), 1.0);
}
