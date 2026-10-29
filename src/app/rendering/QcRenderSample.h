// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

// What the world renderer measured in one frame on the render thread (benchmark only).
struct QcRenderSample
{
    // CPU time of the render thread since the previous frame (scene graph, synchronisation and
    // world rendering together); -1 when the platform has no thread CPU clock.
    double renderThreadCpuMs = -1.0;
    // Wall time of QcWorldRenderer::render(), uploads included.
    double worldRenderMs = 0.0;
    // Wall time spent creating and filling section and entity buffers.
    double uploadMs = 0.0;
    quint64 uploadBytes = 0;
    // GPU time of the last completed frame (whole window); 0 without timestamps (QSG_RHI_PROFILE=1).
    double gpuMs = 0.0;
    int drawCalls = 0;
    int visibleSections = 0;
    quint64 indicesDrawn = 0;
    int sections = 0;
    // Bytes in all section vertex and index buffers.
    quint64 sectionBytes = 0;
};
