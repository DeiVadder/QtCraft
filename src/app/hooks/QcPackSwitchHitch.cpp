#include "app/hooks/QcPackSwitchHitch.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QStringList QcPackSwitchHitch::selectionWith(const QStringList &active, const QStringList &packs)
{
    QStringList selection;
    const QStringList candidates = packs + active;
    for (const QString &id : candidates) {
        if (!selection.contains(id)) {
            selection.append(id);
        }
    }
    return selection;
}

void QcPackSwitchHitch::start(qint64 nowNs)
{
    *this = QcPackSwitchHitch();
    m_startNs = nowNs;
}

void QcPackSwitchHitch::atlasReady(qint64 nowNs)
{
    if (started() && m_readyNs < 0) {
        m_readyNs = nowNs;
    }
}

bool QcPackSwitchHitch::watching(qint64 nowNs) const
{
    if (!started() || nowNs < m_startNs) {
        return false;
    }
    return m_readyNs < 0 || nowNs <= m_readyNs + afterNs;
}

void QcPackSwitchHitch::addFrame(qint64 nowNs, qint64 intervalNs)
{
    if (!watching(nowNs)) {
        return;
    }
    ++m_frames;
    m_worstFrameNs = std::max(m_worstFrameNs, intervalNs);
    if (intervalNs >= slowFrameNs) {
        ++m_slowFrames;
    }
}

void QcPackSwitchHitch::addGuiCpu(qint64 nowNs, double ms)
{
    if (watching(nowNs)) {
        m_guiCpuMaxMs = std::max(m_guiCpuMaxMs, ms);
    }
}

void QcPackSwitchHitch::addRender(qint64 nowNs, double renderThreadCpuMs, double worldRenderMs)
{
    if (watching(nowNs)) {
        m_renderCpuMaxMs = std::max(m_renderCpuMaxMs, renderThreadCpuMs);
        m_worldRenderMaxMs = std::max(m_worldRenderMaxMs, worldRenderMs);
    }
}

double QcPackSwitchHitch::atlasMs() const
{
    return started() && m_readyNs >= 0 ? static_cast<double>(m_readyNs - m_startNs) / 1e6 : -1.0;
}

QString QcPackSwitchHitch::summary() const
{
    return u"atlas_ms=%1 worst_frame_ms=%2 frames=%3 slow_frames=%4 gui_cpu_max_ms=%5 render_cpu_max_ms=%6 world_render_max_ms=%7"_s
        .arg(atlasMs(), 0, 'f', 1)
        .arg(worstFrameMs(), 0, 'f', 2)
        .arg(m_frames)
        .arg(m_slowFrames)
        .arg(m_guiCpuMaxMs, 0, 'f', 2)
        .arg(m_renderCpuMaxMs, 0, 'f', 2)
        .arg(m_worldRenderMaxMs, 0, 'f', 2);
}
