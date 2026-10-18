#include "app/debug/QcDebugVisuals.h"

#include "app/session/QcSessionParts.h"
#include "clientcore/debug/QcDebugLines.h"
#include "clientcore/session/QcClientSession.h"

QcDebugVisuals::QcDebugVisuals(const QcSessionParts &parts)
    : m_parts(parts)
{
    m_clock.start();
}

void QcDebugVisuals::meshed(quint64 sectionKey, QcDebugOptions options)
{
    if (options.shows(QcDebugOptions::Visual::RemeshFlash)) {
        m_meshedAt.insert(sectionKey, m_clock.elapsed());
    }
}

void QcDebugVisuals::update(QcDebugOptions options, double alpha, const QcVec3 &feet)
{
    using V = QcDebugOptions::Visual;
    const qint64 now = m_clock.elapsed();
    m_meshedAt.removeIf([now](const auto &entry) { return now - entry.value() >= QcDebugLines::flashMs; });

    QcDebugLines lines;
    if (options.shows(V::ChunkBorders)) {
        lines.addChunkBorders(feet);
    }
    if (options.shows(V::Hitboxes)) {
        lines.addHitboxes(*m_parts.session, alpha);
    }
    if (options.shows(V::MobGoals)) {
        if (const std::optional<QcMsg::ServerDebug> &server = m_parts.session->debug().mirror().latest()) {
            lines.addMobGoals(*m_parts.session, server->goals, alpha);
        }
    }
    if (options.shows(V::RemeshFlash)) {
        lines.addRemeshFlashes(m_meshedAt, now);
    }

    // The view keeps no lines while there are none, so a frame without visualisations costs nothing.
    if (lines.isEmpty() && !m_drawing) {
        return;
    }
    m_drawing = !lines.isEmpty();
    m_parts.view->setDebugLines(m_drawing ? std::make_shared<const QList<QcDebugLineVertex>>(lines.vertices()) : nullptr);
}

void QcDebugVisuals::clear()
{
    m_meshedAt.clear();
    m_drawing = false;
    if (m_parts.view) {
        m_parts.view->setDebugLines(nullptr);
    }
}
