// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/music/QcMusicDirector.h"

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/world/QcDayCycle.h"

#include <algorithm>

QcMusicDirector::QcMusicDirector(quint64 seed)
    : m_random(seed)
{
}

QcMusicDirector::Situation QcMusicDirector::menu()
{
    const QString slot = QString::fromLatin1(QcMusicLibrary::menuSlot);
    return {{slot}, slot};
}

QcMusicDirector::Situation QcMusicDirector::world(const QString &dimension, const QString &biome, double worldTime, float cave)
{
    Situation situation;
    if (!biome.isEmpty()) {
        situation.places.append(QLatin1StringView(QcMusicLibrary::biomePrefix) + biome);
    }
    if (dimension == QLatin1StringView(QcDimensionDefinition::overworldName)) {
        const char *slot = QcDayCycle::isNight(worldTime) ? QcMusicLibrary::nightSlot : QcMusicLibrary::daySlot;
        situation.places.append(QString::fromLatin1(cave >= caveLevel ? QcMusicLibrary::caveSlot : slot));
    }
    situation.family = QLatin1StringView(QcMusicLibrary::dimensionPrefix) + dimension;
    situation.places.append(situation.family);
    return situation;
}

double QcMusicDirector::draw(Pause pause)
{
    return pause.shortest + (pause.longest - pause.shortest) * m_random.generateDouble();
}

void QcMusicDirector::setState(State state, double seconds)
{
    m_state = state;
    setRemaining(seconds);
}

void QcMusicDirector::change()
{
    if (state() == State::Playing || state() == State::Asking) {
        setFadeRequested(true);
    } else if (state() == State::Waiting) {
        setRemaining(std::min(remaining(), draw(changeDelay)));
    }
}

void QcMusicDirector::setSource(QcMusicSource source)
{
    if (source != m_source) {
        m_source = source;
        change();
    }
}

void QcMusicDirector::setSituation(const Situation &next)
{
    if (next == m_situation) {
        return;
    }

    // Within a family the piece plays on and the pause stays; the next piece follows the new
    // place.
    const bool newFamily = next.family != m_situation.family;
    m_situation = next;
    if (newFamily) {
        change();
    }
}

void QcMusicDirector::setEnabled(bool enabled)
{
    if (!enabled) {
        setFadeRequested(false);
        setState(State::Off, 0.0);
    } else if (state() == State::Off) {
        setState(State::Waiting, draw(firstDelay));
    }
}

QcMusicDirector::Action QcMusicDirector::advance(double seconds)
{
    if (state() == State::Off) {
        return {};
    }
    if (fadeRequested() && state() == State::Playing) {
        setFadeRequested(false);
        setState(State::Fading, fadeSeconds);
        return {Action::Kind::FadeOut, {}};
    }

    setRemaining(remaining() - std::max(0.0, seconds));
    if (remaining() > 0.0 || state() == State::Asking) {
        return {};
    }
    if (state() == State::Waiting) {
        return choose();
    }

    setState(State::Waiting, draw(state() == State::Playing ? pauseAfterPiece() : changeDelay));
    return {};
}

QcMusicDirector::Pause QcMusicDirector::pauseAfterPiece() const
{
    if (generated()) {
        return generatedPause;
    }
    return situation().family == QLatin1StringView(QcMusicLibrary::menuSlot) ? menuPause : trackPause;
}

QcMusicDirector::Action QcMusicDirector::choose()
{
    if (m_source == QcMusicSource::Tracks) {
        if (const QString track = pickTrack(); !track.isEmpty()) {
            setGenerated(false);
            m_lastTrack = track;
            setState(State::Asking, 0.0);
            return {Action::Kind::PlayTrack, track};
        }
    }
    if (situation().family != QLatin1StringView(QcMusicLibrary::menuSlot)) {
        setGenerated(true);
        setState(State::Asking, 0.0);
        return {Action::Kind::PlayGenerated, {}};
    }

    // The menus compose nothing: without a track they stay silent until there is one.
    setState(State::Waiting, draw(menuPause));
    return {};
}

QString QcMusicDirector::pickTrack()
{
    if (!m_library) {
        return {};
    }

    for (const QString &slot : situation().places) {
        QStringList ids = m_library->tracksFor(slot);
        if (ids.size() > 1) {
            ids.removeAll(m_lastTrack);
        }
        if (!ids.isEmpty()) {
            return ids[m_random.bounded(static_cast<int>(ids.size()))];
        }
    }
    return {};
}

bool QcMusicDirector::started(double seconds)
{
    if (state() != State::Asking) {
        return false;
    }
    if (fadeRequested()) {
        // Asked for before the change: the piece for the new place follows soon instead.
        setFadeRequested(false);
        setState(State::Waiting, draw(changeDelay));
        return false;
    }
    if (seconds <= 0.0) {
        failed();
        return false;
    }

    setState(State::Playing, seconds);
    return true;
}

void QcMusicDirector::failed()
{
    if (state() == State::Asking) {
        setFadeRequested(false);
        setState(State::Waiting, draw(pauseAfterPiece()));
    }
}

void QcMusicDirector::interrupted()
{
    if (state() != State::Off) {
        setFadeRequested(false);
        setState(State::Waiting, draw(firstDelay));
    }
}
