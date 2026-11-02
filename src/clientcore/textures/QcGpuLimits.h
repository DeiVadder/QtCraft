// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief What the renderer learned about the GPU, for code that prepares textures in other threads
    (decision E-300): the largest texture edge.

    Thread-safe. */
namespace QcGpuLimits {

/*! Until a renderer reported its limit: an edge every desktop GPU QtCraft runs on supports. */
inline constexpr int assumedMaxTextureSize = 8192;

/*! Returns the reported largest texture edge, or assumedMaxTextureSize before any report. */
int maxTextureSize();
/*! Takes \a maxTextureSize, called by the renderers with QRhi::TextureSizeMax; values below 1
    are ignored. */
void report(int maxTextureSize);

} // namespace QcGpuLimits
