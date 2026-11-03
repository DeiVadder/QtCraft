// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/camera/QcCamera.h"
#include "common/core/QcVec3.h"

/*! \brief View of the character preview: the figure stands at the origin and turns on the spot
    (dragging turns it), the camera looks at it from +Z along -Z and zooms from the whole body to
    the head.

    GUI-free so the controls can be tested without a window. */
class QcPreviewCamera
{
public:
    /*! The side of the figure facing the camera. */
    enum class Side {
        Front, // The face looks at the camera
        Back   // The back faces the camera
    };

    /*! The figure's yaw (QcEntityMesher::Placement) when its face looks at the camera. */
    static constexpr double frontYaw = 180.0;
    /*! The default view: a little turned, so the figure shows its depth. */
    static constexpr double defaultTurn = -25.0;
    /*! Dragging by one logical pixel turns the figure this far. */
    static constexpr double degreesPerPixel = 0.5;
    /*! One wheel step (120 units, a notch) changes the zoom by this share. */
    static constexpr double zoomPerStep = 0.12;
    /*! Vertical field of view, narrow like a portrait lens. */
    static constexpr float fieldOfView = 30.0f;
    /*! Camera distance and the height it looks at, without and with full zoom. \qcshared */
    static constexpr double farDistance = 4.4;
    static constexpr double nearDistance = 1.5;
    static constexpr double bodyHeight = 0.92;
    static constexpr double headHeight = 1.55;

    /*! Returns the figure's yaw in degrees, in [0, 360). */
    double modelYaw() const { return m_modelYaw; }
    /*! Sets the figure's yaw to \a yaw degrees (wrapped to [0, 360)); values that are not finite
        are refused with a warning. */
    void setModelYaw(double yaw);
    /*! Returns the zoom: 0 shows the whole body, 1 the head and shoulders. */
    double zoom() const { return m_zoom; }
    /*! Sets the zoom to \a zoom (clamped to 0..1); values that are not finite are
        refused with a warning. */
    void setZoom(double zoom);

    /*! Mouse dragged by \a pixels to the right: the figure turns with it (its front to the
        right). */
    void drag(double pixels);
    /*! Wheel turned by \a angleDelta (120 per notch, positive away from the user): zooms in. */
    void wheel(int angleDelta);
    /*! Shows the figure straight from \a side (front or behind), keeping the zoom. */
    void show(Side side);
    /*! Returns the side of the figure that faces the camera (front while turned less than 90
        degrees away). */
    Side side() const;
    /*! Default turn and no zoom. */
    void reset();

    /*! Returns where the camera stands and looks (QcCamera conventions). */
    QcCamera camera() const;
    /*! Returns the point the camera looks at. */
    QcVec3 target() const;
    /*! Returns the camera's distance from the point it looks at, at the current zoom. */
    double distance() const;

private:
    double m_modelYaw = frontYaw + defaultTurn;
    double m_zoom = 0.0;
};
