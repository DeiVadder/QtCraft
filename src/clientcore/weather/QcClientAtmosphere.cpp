// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/weather/QcClientAtmosphere.h"

#include <algorithm>

namespace {

// The fog `atmosphere` asks for; where it names nothing, the value of `shown` with share 0, so
// that only the share moves and the value does not drift.
QcAtmosphereFog targetOf(const QcBiomeAtmosphere &atmosphere, const QcAtmosphereFog &shown)
{
    QcAtmosphereFog fog = shown;
    fog.colourShare = atmosphere.fogColour ? 1.0 : 0.0;
    fog.colour = atmosphere.fogColour ? QcSky::colourOf(*atmosphere.fogColour) : shown.colour;
    fog.startShare = atmosphere.fogStart ? 1.0 : 0.0;
    fog.start = atmosphere.fogStart.value_or(shown.start);
    fog.endShare = atmosphere.fogEnd ? 1.0 : 0.0;
    fog.end = atmosphere.fogEnd.value_or(shown.end);
    return fog;
}

// A fog that replaced nothing yet takes the new value at once: only its share blends in.
QcAtmosphereFog startOf(QcAtmosphereFog shown, const QcAtmosphereFog &target)
{
    if (shown.colourShare <= 0.0) {
        shown.colour = target.colour;
    }
    if (shown.startShare <= 0.0) {
        shown.start = target.start;
    }
    if (shown.endShare <= 0.0) {
        shown.end = target.end;
    }
    return shown;
}

double mix(double a, double b, double t)
{
    return a + (b - a) * t;
}

} // namespace

void QcClientAtmosphere::receive(const QcMsg::Atmosphere &message)
{
    if (message.atmosphere == current()) {
        return;
    }

    const QcAtmosphereFog shown = fog();
    setCurrent(message.atmosphere);
    const QcAtmosphereFog target = targetOf(current(), shown);
    setTo(target);
    setFrom(startOf(shown, target));
    setProgress(0.0);
}

void QcClientAtmosphere::tick(double seconds)
{
    setProgress(std::min(1.0, progress() + seconds / blendSeconds));
}

QcAtmosphereFog QcClientAtmosphere::fog() const
{
    const double t = progress();
    const QcAtmosphereFog &a = from();
    const QcAtmosphereFog &b = to();
    QcAtmosphereFog fog;
    fog.colour = a.colour + (b.colour - a.colour) * static_cast<float>(t);
    fog.colourShare = mix(a.colourShare, b.colourShare, t);
    fog.start = mix(a.start, b.start, t);
    fog.startShare = mix(a.startShare, b.startShare, t);
    fog.end = mix(a.end, b.end, t);
    fog.endShare = mix(a.endShare, b.endShare, t);
    return fog;
}
