// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/camera/QcCamera.h"
#include "common/core/QcChunkPos.h"
#include "common/world/QcBlockAccess.h"
#include "common/world/QcWorldLabel.h"

#include <QHash>
#include <QList>

/*! \brief A world label as drawn this frame: where on screen (pixels from the top left), how large
    and how opaque.

    \c {key} identifies the label while its chunk stays loaded. */
struct QcScreenLabel
{
    /*! Identifies the label while its chunk stays loaded. */
    quint64 key = 0;
    /*! The label's text. */
    QString text;
    /*! Its detail line; empty for none. */
    QString detail;
    /*! Whether it is a title (seen from further away). */
    bool heading = false;
    /*! Pixels from the left edge of the view. */
    double x = 0.0;
    /*! Pixels from the top edge of the view. */
    double y = 0.0;
    /*! Metres from the eye. */
    double distance = 0.0;
    /*! Size factor: near labels are larger. */
    double scale = 1.0;
    /*! Opacity, 0..1: labels fade out towards the end of their reach. */
    double opacity = 1.0;
    /*! Widest the label may be drawn (pixels before \c {scale}) without reaching into a label
        QcLabelProjector::labelSpacing further along; longer lines are cut. */
    double maxWidth = 0.0;
};

/*! \brief Picks the world labels to draw (decision E-222): those within reach of the camera (titles
    from further away), in front of it and not hidden behind an opaque full block, at most maxShown
    of the nearest.

    At most maxSightRays lines of sight are followed per frame, nearest labels first; labels
    beyond that budget are not drawn this frame. They come back farthest first, so that nearer
    labels are drawn over farther ones; near labels are larger, labels fade out towards the end
    of their reach. GUI-free and deterministic. */
class QcLabelProjector
{
public:
    /*! Metres within which a label shows. */
    static constexpr double labelReach = 12.0;
    /*! Metres within which a title shows. */
    static constexpr double headingReach = 48.0;
    /*! Labels drawn at most per frame. */
    static constexpr int maxShown = 48;
    /*! Lines of sight followed at most per frame. */
    static constexpr int maxSightRays = 48;
    /*! Labels of neighbouring podiums stand 2 m apart; a label is at most this wide in the
        world. */
    static constexpr double labelSpacing = 1.8;

    /*! \brief The camera and the size of the view the labels are drawn into. */
    struct View
    {
        /*! The camera. */
        QcCamera camera;
        /*! Size of the view in pixels. \qcshared */
        double width = 0.0;
        double height = 0.0;
    };

    /*! Returns the \a labels of the loaded chunks to draw for \a view, farthest first. \a world
        decides what hides a label; without one nothing does. */
    static QList<QcScreenLabel> visible(const QHash<QcChunkPos, QList<QcWorldLabel>> &labels, const View &view, const QcBlockAccess *world);
    /*! Returns whether an opaque full block of \a world lies between \a eye and \a point;
        transparent blocks on the way (glass, panes, leaves) do not stop the search. */
    static bool isHidden(const QcBlockAccess &world, const QcVec3 &eye, const QcVec3 &point);
};
