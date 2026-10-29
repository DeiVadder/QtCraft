// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityMesh.h"
#include "clientcore/light/QcEntityLight.h"
#include "common/core/QcVec3.h"

#include <QRectF>
#include <QString>

// The luminous threads between the children of a family and their parent (decision E-839; the Kjut,
// a signal and slot look): a thread from the middle of one body to the middle of the other, drawn
// as two crossed ribbons. Which child is tied to which parent: QcTethers.
namespace QcTetherMesher {

// Width of a drawn thread (m).
inline constexpr double thickness = 0.03;

// One thread: from the middle of the parent's body to the middle of the child's, with the texture.
struct Thread
{
    QcVec3 from;
    QcVec3 to;
    QString texture;
};

// A thread as two crossed ribbons seen from both sides, drawn in `light`.
void append(QcEntityMesh &mesh, const Thread &thread, const QRectF &uv, QcEntityLight light);

} // namespace QcTetherMesher
