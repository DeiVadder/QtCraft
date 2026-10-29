// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcWorldPause.h"

#include "app/controllers/QcAudioSettings.h"
#include "app/controllers/QcSettingsController.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"

QcWorldPause::QcWorldPause(const QcSessionParts &parts, QcScreenState &screens, QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_screens(screens)
    , m_settings(settings)
{
    connect(&m_screens, &QcScreenState::overlayChanged, this, &QcWorldPause::update);
    connect(&m_screens, &QcScreenState::phaseChanged, this, &QcWorldPause::update);
    connect(&m_settings, &QcSettingsController::settingsChanged, this, &QcWorldPause::update);
    // Back in the menu the session is gone: the sounds are not held silent.
    connect(&m_screens, &QcScreenState::phaseChanged, this, &QcWorldPause::sessionChanged);
}

bool QcWorldPause::pausesWorld(QcScreenState::Overlay overlay)
{
    return QcScreenState::isMenu(overlay) && overlay != QcScreenState::Overlay::Debug;
}

bool QcWorldPause::paused() const
{
    return m_parts.session && m_parts.session->serverPaused();
}

QcWorldPause::Hint QcWorldPause::hint() const
{
    if (!m_wanted || !m_parts.session || paused()) {
        return Hint::None;
    }
    if (!m_parts.server) {
        return Hint::SharedServer;
    }
    return m_parts.session->players().size() > 1 ? Hint::OthersConnected : Hint::Pending;
}

void QcWorldPause::setWindowActive(bool active)
{
    if (m_windowActive == active) {
        return;
    }

    m_windowActive = active;
    // Like the reference game: a window left in play shows the pause menu when the player returns;
    // not while others play in the world, where it would not pause.
    const bool alone = m_parts.session && m_parts.session->players().size() <= 1;
    if (!active && m_settings.pauseOnFocusLoss() && m_parts.server && alone && m_screens.playing()
        && m_screens.overlay() == QcScreenState::Overlay::None) {
        m_screens.setOverlay(QcScreenState::Overlay::Pause);
    }
    update();
}

void QcWorldPause::sessionStarted()
{
    if (!m_parts.session) {
        return;
    }

    connect(m_parts.session.get(), &QcClientSession::serverPauseChanged, this, &QcWorldPause::sessionChanged);
    connect(m_parts.session.get(), &QcClientSession::playersChanged, this, &QcWorldPause::changed);
    if (m_wanted && m_parts.server) {
        m_parts.session->requestPause(true);
    }
    sessionChanged();
}

void QcWorldPause::update()
{
    const bool focusPause = m_settings.pauseOnFocusLoss() && !m_windowActive;
    const bool wanted = m_screens.playing() && (pausesWorld(m_screens.overlay()) || focusPause);
    if (wanted == m_wanted) {
        return;
    }

    m_wanted = wanted;
    // Only the server of a world hosted here may pause for its player; a joined server is not asked.
    if (m_parts.server && m_parts.session) {
        m_parts.session->requestPause(wanted);
    }
    emit wantedChanged(wanted);
    emit changed();
}

void QcWorldPause::sessionChanged()
{
    m_settings.audio()->setWorldPaused(paused() && m_screens.playing());
    emit changed();
}
