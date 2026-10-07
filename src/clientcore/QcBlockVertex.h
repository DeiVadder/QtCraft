#pragma once

// Vertex layout shared by the mesher and the GPU pipeline.
struct QcBlockVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    // Combined face shading and ambient occlusion in [0, 1].
    float shade = 1.0f;
    // Sky light in [0, 1]; scaled by the time of day in the shader.
    float skyLight = 1.0f;
};
