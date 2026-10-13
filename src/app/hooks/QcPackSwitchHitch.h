#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// The hitch of one texture pack switch in a benchmark run (QTCRAFT_BENCHMARK_PACK_SWITCH, decision
// E-332): from the moment the switch is asked for until one second after the new atlas arrived,
// the longest frame interval, the frames over 50 ms, and per frame the longest CPU time of the GUI
// thread, of the render thread and of the world renderer. All times come from one clock in
// nanoseconds; values outside that window are ignored.
class QcPackSwitchHitch
{
public:
    // How long frames are still watched after the atlas arrived (its upload and first frames).
    static constexpr qint64 afterNs = 1'000'000'000;
    // A frame interval at least this long counts as a slow frame.
    static constexpr qint64 slowFrameNs = 50'000'000;

    // The selection while the packs `packs` are switched on over `active`: them first, in their
    // order, then the others of `active` (each id once).
    static QStringList selectionWith(const QStringList &active, const QStringList &packs);

    // The switch was asked for (clears an earlier measurement).
    void start(qint64 nowNs);
    // The new atlas arrived; only the first call after start() counts.
    void atlasReady(qint64 nowNs);
    bool started() const { return m_startNs >= 0; }
    // True from start() until afterNs after atlasReady() (without an atlas: until the end).
    bool watching(qint64 nowNs) const;

    void addFrame(qint64 nowNs, qint64 intervalNs);
    void addGuiCpu(qint64 nowNs, double ms);
    void addRender(qint64 nowNs, double renderThreadCpuMs, double worldRenderMs);

    // Milliseconds from start() to atlasReady(), -1 while no atlas arrived.
    double atlasMs() const;
    double worstFrameMs() const { return double(m_worstFrameNs) / 1e6; }
    int frames() const { return m_frames; }
    int slowFrames() const { return m_slowFrames; }
    double guiCpuMaxMs() const { return m_guiCpuMaxMs; }
    double renderCpuMaxMs() const { return m_renderCpuMaxMs; }
    double worldRenderMaxMs() const { return m_worldRenderMaxMs; }
    // "key=value" pairs separated by spaces, for the log line of the benchmark.
    QString summary() const;

private:
    qint64 m_startNs = -1;
    qint64 m_readyNs = -1;
    qint64 m_worstFrameNs = 0;
    int m_frames = 0;
    int m_slowFrames = 0;
    double m_guiCpuMaxMs = 0.0;
    double m_renderCpuMaxMs = 0.0;
    double m_worldRenderMaxMs = 0.0;
};
