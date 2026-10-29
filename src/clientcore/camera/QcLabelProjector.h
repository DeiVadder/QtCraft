// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/camera/QcCamera.h"
#include "common/core/QcChunkPos.h"
#include "common/world/QcBlockAccess.h"
#include "common/world/QcWorldLabel.h"

#include <QHash>
#include <QList>

// A world label as drawn this frame: where on screen (pixels from the top left), how large and how
// opaque. `key` identifies the label while its chunk stays loaded.
struct QcScreenLabel
{
    quint64 key = 0;
    QString text;
    QString detail;
    bool heading = false;
    double x = 0.0;
    double y = 0.0;
    double distance = 0.0;
    double scale = 1.0;
    double opacity = 1.0;
    // Widest the label may be drawn (pixels before `scale`) without reaching into a label
    // QcLabelProjector::labelSpacing further along; longer lines are cut.
    double maxWidth = 0.0;
};

// Picks the world labels to draw (decision E-222): those within reach of the camera (titles from
// further away), in front of it and not hidden behind an opaque full block, at most maxShown of the
// nearest. At most maxSightRays lines of sight are followed per frame, nearest labels first; labels
// beyond that budget are not drawn this frame. They come back farthest first, so that nearer labels are drawn over farther ones; near
// labels are larger, labels fade out towards the end of their reach. GUI-free and deterministic.
class QcLabelProjector
{
public:
    static constexpr double labelReach = 12.0;
    static constexpr double headingReach = 48.0;
    static constexpr int maxShown = 48;
    static constexpr int maxSightRays = 48;
    // Labels of neighbouring podiums stand 2 m apart; a label is at most this wide in the world.
    static constexpr double labelSpacing = 1.8;

    struct View
    {
        QcCamera camera;
        // Size of the view in pixels.
        double width = 0.0;
        double height = 0.0;
    };

    // `world` decides what hides a label; without one nothing does.
    static QList<QcScreenLabel> visible(const QHash<QcChunkPos, QList<QcWorldLabel>> &labels, const View &view, const QcBlockAccess *world);
    // Whether an opaque full block lies between the eye and the point; transparent blocks on the way
    // (glass, panes, leaves) do not stop the search.
    static bool isHidden(const QcBlockAccess &world, const QcVec3 &eye, const QcVec3 &point);
};
