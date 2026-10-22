#pragma once

#include "clientcore/camera/QcCamera.h"
#include "clientcore/camera/QcFrustum.h"
#include "common/core/QcAabb.h"

#include <optional>

struct QcClientEntity;
class QcContentRegistry;

// Which entities a frame builds (decision E-862): only those whose box may show in the camera's
// view. The entity meshes are built on the GUI thread every frame, so an entity behind the camera
// or beside the view costs nothing.
namespace QcEntityCulling {

// Held items reach about a metre beyond a body, a cape or a flame a little less; the margin also
// covers the frame's interpolation.
inline constexpr double margin = 2.5;
// The far plane of the culling planes lies beyond any entity the server sends; only the sides and
// the near plane cull.
inline constexpr float farPlane = 4096.0f;

// The clip planes of `camera` for a view of `width` x `height`; none for a view without a size
// (then every entity is built).
[[nodiscard]] std::optional<QcFrustum::Planes> viewPlanes(const QcCamera &camera, double width, double height);

// The box an entity may be drawn in this frame: its body (the creature's size, else 1 x 2 m)
// around its feet at the start and at the end of the client tick, grown by `margin`; for an item
// frame or a painting its box on the wall, grown by `margin`.
[[nodiscard]] QcAabb viewBox(const QcClientEntity &entity, const QcContentRegistry &content);

// Whether a box may show: always without planes, else unless it lies outside one of them.
[[nodiscard]] bool mayBeSeen(const std::optional<QcFrustum::Planes> &planes, const QcAabb &box);

} // namespace QcEntityCulling
