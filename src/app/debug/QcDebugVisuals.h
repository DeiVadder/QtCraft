// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcDebugOptions.h"
#include "common/core/QcVec3.h"

#include <QElapsedTimer>
#include <QHash>

struct QcSessionParts;

// The debug visualisations drawn into the world (decision E-225): builds the lines of the switched
// on ones every frame (QcDebugLines) and hands them to the view; remembers which sections were
// meshed again while their flashing is on. Nothing is built or drawn while all are off.
class QcDebugVisuals
{
public:
    explicit QcDebugVisuals(const QcSessionParts &parts);

    // A section got a new mesh (QcMeshScheduler::meshReady).
    void meshed(quint64 sectionKey, QcDebugOptions options);
    // Once per frame; `alpha` interpolates entities within the tick, `feet` is the drawn position.
    void update(QcDebugOptions options, double alpha, const QcVec3 &feet);
    void clear();

private:
    const QcSessionParts &m_parts;
    QElapsedTimer m_clock;
    // Section key -> time of its latest mesh (m_clock milliseconds).
    QHash<quint64, qint64> m_meshedAt;
    bool m_drawing = false;
};
