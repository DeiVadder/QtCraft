#pragma once

// What the renderer learned about the GPU, for code that prepares textures in other threads
// (decision E-300): the largest texture edge. Thread-safe.
namespace QcGpuLimits {

// Until a renderer reported its limit: an edge every desktop GPU QtCraft runs on supports.
inline constexpr int assumedMaxTextureSize = 8192;

// The reported largest texture edge, or assumedMaxTextureSize before any report.
int maxTextureSize();
// Called by the renderers with QRhi::TextureSizeMax; values below 1 are ignored.
void report(int maxTextureSize);

} // namespace QcGpuLimits
