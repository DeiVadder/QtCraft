#version 440

// One doubling of the bloom chain (decision E-899): the dual-filter upsample (a tent of eight taps)
// of the smaller, already blurred level, mixed with this level's halving by the scatter, so every
// level adds its radius and the sum keeps the brightness of the source.

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Bloom {
    // xy: one texel of the smaller level, zw: one pixel of the target
    vec4 texel;
    // z: scatter
    vec4 params;
} bloom;

// The smaller level and this level's halving.
layout(binding = 1) uniform sampler2D smaller;
layout(binding = 2) uniform sampler2D halving;

void main()
{
    vec2 uv = gl_FragCoord.xy * bloom.texel.zw;
    vec2 t = bloom.texel.xy * 0.5;
    vec3 sum = texture(smaller, uv + vec2(-2.0 * t.x, 0.0)).rgb;
    sum += texture(smaller, uv + vec2(2.0 * t.x, 0.0)).rgb;
    sum += texture(smaller, uv + vec2(0.0, -2.0 * t.y)).rgb;
    sum += texture(smaller, uv + vec2(0.0, 2.0 * t.y)).rgb;
    sum += texture(smaller, uv + vec2(-t.x, -t.y)).rgb * 2.0;
    sum += texture(smaller, uv + vec2(t.x, -t.y)).rgb * 2.0;
    sum += texture(smaller, uv + vec2(-t.x, t.y)).rgb * 2.0;
    sum += texture(smaller, uv + vec2(t.x, t.y)).rgb * 2.0;
    fragColor = vec4(mix(texture(halving, uv).rgb, sum / 12.0, bloom.params.z), 1.0);
}
