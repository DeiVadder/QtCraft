// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcSky.h"
#include "common/protocol/QcMessages.h"

/*! \brief The atmosphere of the biome the player stands in as the client shows it (decision
    E-1120): the one the server sent last, and its fog blended in from the fog shown before over
    blendSeconds, so walking into another biome never makes the fog jump.

    The fog is kept apart from the dimension's: a colour and two distances, each with the share
    by which it replaces the dimension's (QcAtmosphereFog). A biome without its own fog lowers
    that share to 0, one with fog raises it to 1. GUI-free; the session ticks it. */
class QcClientAtmosphere
{
public:
    /*! Seconds the fog takes from the old biome's to the new one's (the role model blends the
        fog colour over 5 s). */
    static constexpr double blendSeconds = 5.0;

    /*! Takes the atmosphere the server sent in \a message; the fog starts to blend towards it. */
    void receive(const QcMsg::Atmosphere &message);
    /*! Advances the blend by \a seconds. */
    void tick(double seconds);

    /*! Returns the atmosphere the server sent last (particles, ambience, music). */
    const QcBiomeAtmosphere &current() const { return m_current; }
    /*! Returns the fog as shown now. */
    QcAtmosphereFog fog() const;

private:
    void setCurrent(const QcBiomeAtmosphere &atmosphere) { m_current = atmosphere; }
    const QcAtmosphereFog &from() const { return m_from; }
    void setFrom(const QcAtmosphereFog &fog) { m_from = fog; }
    const QcAtmosphereFog &to() const { return m_to; }
    void setTo(const QcAtmosphereFog &fog) { m_to = fog; }
    double progress() const { return m_progress; }
    void setProgress(double progress) { m_progress = progress; }

    QcBiomeAtmosphere m_current;
    // The fog shown when the latest atmosphere arrived, the one it blends to and how far it got.
    QcAtmosphereFog m_from;
    QcAtmosphereFog m_to;
    double m_progress = 1.0;
};
