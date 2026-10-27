#version 440

// One triangle covering the target (decision E-899): the post-processing passes of the realistic
// mode. The fragment shaders find their texel from gl_FragCoord, which matches the rows of a
// texture drawn by an earlier pass on every backend, so no flip is needed.
layout(location = 0) out vec2 vNdc;

void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    vNdc = corner;
    gl_Position = vec4(corner, 0.0, 1.0);
}
