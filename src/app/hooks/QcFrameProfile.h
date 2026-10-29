// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/rendering/QcRenderSample.h"

#include <QList>
#include <QString>

// Benchmark breakdown of the frame time that does not depend on VSync: CPU time per frame of the
// GUI and the render thread, the world renderer with its uploads, entity meshing, GPU time and
// what was drawn. Collected on the GUI thread; render samples arrive through QcWorldView.
class QcFrameProfile
{
public:
    void clear();
    // Once per GUI frame: CPU time of the GUI thread since the previous frame (-1 if unknown) and
    // the time spent building entity and hand meshes and the character previews' meshes (E-231) in
    // this frame.
    void addGuiFrame(double guiCpuMs, double entityMeshMs, double previewMeshMs);
    void addRenderSamples(const QList<QcRenderSample> &samples);

    // "key=value" pairs separated by spaces, for the BENCH line.
    QString summary() const;

    static double average(const QList<double> &values);
    // Value below which 99 % of the values lie (nearest rank); 0 for no values.
    static double percentile99(QList<double> values);

private:
    QList<double> m_guiCpuMs;
    QList<double> m_entityMeshMs;
    QList<double> m_previewMeshMs;
    QList<QcRenderSample> m_render;
};
