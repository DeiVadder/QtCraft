#include "QcClientWeather.h"

#include "QcPhysics.h"
#include "QcSoundKeys.h"

#include <algorithm>
#include <cmath>

namespace {

float ease(float shown, float target)
{
    if (shown < target) {
        return std::min(target, shown + QcClientWeather::easePerTick);
    }
    return std::max(target, shown - QcClientWeather::easePerTick);
}

} // namespace

void QcClientWeather::receive(QcMsg::Weather weather)
{
    m_received = weather;
}

void QcClientWeather::lightning(const QcMsg::Lightning &strike, const QcVec3 &listener, quint64 tick)
{
    ++m_strikes;
    m_flashTick = tick;

    const QcVec3 towards = strike.position - listener;
    const double distance = towards.length();
    if (distance > thunderHearing) {
        return;
    }

    QcSoundCue cue{QString::fromLatin1(QcSoundKeys::weatherThunder), strike.position, 1.0f, 1.0f, QcSoundCue::Origin::Local, 0};
    if (distance > thunderPlacement) {
        cue.position = listener + towards * (thunderPlacement / distance);
        cue.volume = float(thunderPlacement / distance);
    }
    const auto delay = quint64(std::llround(distance / speedOfSound / QcPhysics::tickDuration));
    m_thunderOnItsWay.append({tick + delay, cue});
}

QList<QcSoundCue> QcClientWeather::tick(quint64 tick)
{
    m_rain = ease(m_rain, m_received.rain);
    m_thunder = ease(m_thunder, m_received.thunder);

    QList<QcSoundCue> due;
    for (auto it = m_thunderOnItsWay.begin(); it != m_thunderOnItsWay.end();) {
        if (it->dueTick <= tick) {
            due.append(it->cue);
            it = m_thunderOnItsWay.erase(it);
        } else {
            ++it;
        }
    }
    return due;
}

QcPrecipitation QcClientWeather::precipitationAt(int y) const
{
    if (m_rain <= 0.0f) {
        return QcPrecipitation::None;
    }
    return QcWeather::precipitationAt(m_received.climate, y);
}

float QcClientWeather::flash(double ticks) const
{
    if (!m_flashTick) {
        return 0.0f;
    }

    const double since = ticks - double(*m_flashTick);
    if (since < 0.0 || since >= flashTicks) {
        return 0.0f;
    }
    // Stroke (0-2), gap (2-3), second stroke (3-5), fading out to flashTicks.
    if (since < 2.0) {
        return 1.0f;
    }
    if (since < 3.0) {
        return 0.25f;
    }
    if (since < 5.0) {
        return 0.8f;
    }
    return float(0.8 * (1.0 - (since - 5.0) / (flashTicks - 5.0)));
}
