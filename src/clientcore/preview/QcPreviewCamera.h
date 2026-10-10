#pragma once

#include "QcCamera.h"
#include "QcVec3.h"

// View of the character preview: the figure stands at the origin and turns on the spot (dragging
// turns it), the camera looks at it from +Z along -Z and zooms from the whole body to the head.
// GUI-free so the controls can be tested without a window.
class QcPreviewCamera
{
public:
    enum class Side { Front, Back };

    // The figure's yaw (QcEntityMesher::Placement) when its face looks at the camera.
    static constexpr double frontYaw = 180.0;
    // The default view: a little turned, so the figure shows its depth.
    static constexpr double defaultTurn = -25.0;
    // Dragging by one logical pixel turns the figure this far.
    static constexpr double degreesPerPixel = 0.5;
    // One wheel step (120 units, a notch) changes the zoom by this share.
    static constexpr double zoomPerStep = 0.12;
    // Vertical field of view, narrow like a portrait lens.
    static constexpr float fieldOfView = 30.0f;
    // Camera distance and the height it looks at, without and with full zoom.
    static constexpr double farDistance = 4.4;
    static constexpr double nearDistance = 1.5;
    static constexpr double bodyHeight = 0.92;
    static constexpr double headHeight = 1.55;

    // The figure's yaw in degrees, in [0, 360).
    double modelYaw() const { return m_modelYaw; }
    void setModelYaw(double yaw);
    // 0 shows the whole body, 1 the head and shoulders.
    double zoom() const { return m_zoom; }
    void setZoom(double zoom);

    // Mouse dragged by `pixels` to the right: the figure turns with it (its front to the right).
    void drag(double pixels);
    // Wheel turned by `angleDelta` (120 per notch, positive away from the user): zooms in.
    void wheel(int angleDelta);
    // Shows the figure straight from the front or from behind, keeping the zoom.
    void show(Side side);
    // The side of the figure that faces the camera (front while turned less than 90 degrees away).
    Side side() const;
    // Default turn and no zoom.
    void reset();

    // Where the camera stands and looks (QcCamera conventions).
    QcCamera camera() const;
    // The point the camera looks at.
    QcVec3 target() const;
    double distance() const;

private:
    double m_modelYaw = frontYaw + defaultTurn;
    double m_zoom = 0.0;
};
