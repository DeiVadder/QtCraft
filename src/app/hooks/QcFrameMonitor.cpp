#include "QcFrameMonitor.h"

#include "QcThreadCpuClock.h"
#include "QcWorldView.h"

#include <algorithm>
#include <array>

QcFrameMonitor::QcFrameMonitor()
    : m_window(QcFramePhases::count, windowFrames)
{
}

void QcFrameMonitor::setDepth(Depth depth, QcWorldView *view)
{
    if (depth != m_depth) {
        m_depth = depth;
        clear();
    }
    if (view) {
        view->setCollectRenderSamples(depth == Depth::Full);
    }
}

QList<QcRenderSample> QcFrameMonitor::recordFrame(QcWorldView *view, qint64 entityNs, qint64 previewNs)
{
    if (m_depth == Depth::None) {
        return {};
    }

    // The first frame after a pause only starts the clocks.
    const qint64 intervalNs = m_interval.isValid() ? m_interval.nsecsElapsed() : -1;
    m_interval.start();

    QList<QcRenderSample> samples;
    if (m_depth == Depth::Full) {
        const qint64 threadCpuNs = QcThreadCpuClock::nowNs();
        m_lastGuiCpuMs = threadCpuNs >= 0 && m_lastThreadCpuNs >= 0 ? double(threadCpuNs - m_lastThreadCpuNs) / 1e6 : -1.0;
        m_lastThreadCpuNs = threadCpuNs;
        if (view) {
            samples = view->takeRenderSamples();
        }
        if (!samples.isEmpty()) {
            m_render = samples.constLast();
            m_gpuTimed = m_gpuTimed || m_render.gpuMs > 0.0;
        }
    }

    if (intervalNs >= 0) {
        std::array<float, QcFramePhases::count> values{};
        values[std::size_t(QcFramePhase::Interval)] = float(double(intervalNs) / 1e6);
        values[std::size_t(QcFramePhase::GuiCpu)] = float(std::max(0.0, m_lastGuiCpuMs));
        values[std::size_t(QcFramePhase::EntityMesh)] = float(double(entityNs) / 1e6);
        values[std::size_t(QcFramePhase::Preview)] = float(double(previewNs) / 1e6);
        values[std::size_t(QcFramePhase::RenderCpu)] = float(std::max(0.0, m_render.renderThreadCpuMs));
        values[std::size_t(QcFramePhase::WorldRender)] = float(m_render.worldRenderMs);
        values[std::size_t(QcFramePhase::Upload)] = float(m_render.uploadMs);
        values[std::size_t(QcFramePhase::Gpu)] = float(m_render.gpuMs);
        m_window.add(values);
    }
    return samples;
}

QcDebugSnapshot::Frame QcFrameMonitor::frame(const QcMeshScheduler &meshes, qsizetype meshedSections, Reader reader)
{
    QcDebugSnapshot::Frame frame;
    for (int phase = 0; phase < QcFramePhases::count; ++phase) {
        frame.phases[std::size_t(phase)] = {m_window.mean(phase), m_window.max(phase)};
    }
    const double interval = m_window.mean(int(QcFramePhase::Interval));
    frame.fps = interval > 0.0 ? 1000.0 / interval : 0.0;
    frame.gpuTimed = m_gpuTimed;
    frame.drawCalls = m_render.drawCalls;
    frame.visibleSections = m_render.visibleSections;
    frame.meshedSections = meshedSections;
    frame.meshBacklog = meshes.dirtyCount() + meshes.jobsInFlight();
    frame.lightJobsPending = meshes.lightJobsPending();

    // The scheduler counts since its last reset (the benchmark resets it): differences since the
    // reader's previous call, from zero after a reset.
    const QcMeshScheduler::Stats stats = meshes.stats();
    QcMeshScheduler::Stats &previous = m_previousMeshStats[std::size_t(reader)];
    const bool reset = stats.meshJobs < previous.meshJobs || stats.lightJobs < previous.lightJobs;
    const QcMeshScheduler::Stats before = reset ? QcMeshScheduler::Stats{} : previous;
    const quint64 meshJobs = stats.meshJobs - before.meshJobs;
    const quint64 lightJobs = stats.lightJobs - before.lightJobs;
    frame.meshJobMs = meshJobs > 0 ? (stats.meshTotalMs - before.meshTotalMs) / double(meshJobs) : 0.0;
    frame.lightJobMs = lightJobs > 0 ? (stats.lightTotalMs - before.lightTotalMs) / double(lightJobs) : 0.0;
    previous = stats;
    return frame;
}

void QcFrameMonitor::clear()
{
    m_window.clear();
    m_interval.invalidate();
    m_lastThreadCpuNs = -1;
    m_lastGuiCpuMs = -1.0;
    m_render = {};
}
