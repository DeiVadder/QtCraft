#pragma once

#include "QcDebugSnapshot.h"
#include "QcMeshScheduler.h"
#include "QcPhaseWindow.h"
#include "QcRenderSample.h"

#include <QElapsedTimer>
#include <QList>

#include <array>

class QcWorldView;

// Frame statistics of the graphical client (decision E-225): mean and maximum over the latest
// frames of the frame interval, the GUI thread's CPU time and entity meshing, and the render
// thread's samples (render thread CPU, world renderer, uploads, GPU). Collects only what is asked
// for: nothing while the debug display is off, only the interval for the compact display,
// everything for the full display, the statistics log and the benchmark. Lives in the GUI thread.
class QcFrameMonitor
{
public:
    enum class Depth { None, Interval, Full };
    // Who reads the frame part: each has its own baseline for the mesh and light job times.
    enum class Reader { Display, StatsLog };
    static constexpr int readerCount = 2;
    // Two seconds at 120 Hz.
    static constexpr int windowFrames = 240;

    QcFrameMonitor();

    // What to collect from now on; the view hands out render samples only at Depth::Full.
    void setDepth(Depth depth, QcWorldView *view);
    Depth depth() const { return m_depth; }

    // Once per GUI frame with the time spent meshing entities and the hand, and the character
    // previews since the previous frame. Returns the render samples taken from the view this frame
    // (for the benchmark).
    QList<QcRenderSample> recordFrame(QcWorldView *view, qint64 entityNs, qint64 previewNs);
    // The GUI thread's CPU time of the last frame (-1 if unknown or not collected).
    double lastGuiCpuMs() const { return m_lastGuiCpuMs; }

    // The frame part of the debug data; mesh job times are those since `reader`'s previous call.
    QcDebugSnapshot::Frame frame(const QcMeshScheduler &meshes, qsizetype meshedSections, Reader reader);
    // Frame intervals in milliseconds, oldest first (graph).
    QList<float> intervalHistory() const { return m_window.history(int(QcFramePhase::Interval), windowFrames); }
    void clear();

private:
    Depth m_depth = Depth::None;
    QcPhaseWindow m_window;
    QElapsedTimer m_interval;
    qint64 m_lastThreadCpuNs = -1;
    double m_lastGuiCpuMs = -1.0;
    // The latest render sample; render frames do not pair up with GUI frames one to one.
    QcRenderSample m_render;
    bool m_gpuTimed = false;
    std::array<QcMeshScheduler::Stats, readerCount> m_previousMeshStats{};
};
