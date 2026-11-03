// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

/*! \brief The view of a hurt player tips away from the hit and back (decision E-971), like the
    reference's damage tilt.

    The tilt rises within a tick or two and eases back over the rest of the hurt time. A hit from
    the front tips the view up, one from a side rolls it away from that side, a hit without a
    direction (a fall, fire) rolls it. */
namespace QcHurtTilt {

/*! The largest tilt in degrees. */
inline constexpr double maxDegrees = 7.0;

/*! \brief How far the view tips: \c {pitch} up and \c {roll} clockwise (the head leaning to the
    right), in degrees. \qcfields */
struct Tilt
{
    double pitch = 0.0;
    double roll = 0.0;

    bool operator==(const Tilt &) const = default;
};

/*! Returns the tilt at \a progress through the hurt time (0 at the hit, 1 at its end; none
    outside) for a hit from \a fromYaw degrees relative to the view (0 in front, 90 to the right;
    none for a hit without a direction). */
[[nodiscard]] Tilt at(double progress, std::optional<double> fromYaw);

/*! Returns the strength of the tilt at \a progress through the hurt time, 0..1: the reference's
    sin(π (1 - progress)⁴), 0 outside [0, 1]. */
[[nodiscard]] double strength(double progress);

} // namespace QcHurtTilt
