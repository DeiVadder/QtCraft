// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcEffectPuff.h"
#include "common/core/QcVec3.h"
#include "common/protocol/QcWorldMessages.h"

#include <QList>

#include <optional>

/*! \brief The flash and smoke of the explosions the server told about (QcMsg::Explosion, decision
    E-604): each explosion leaves a few bright puffs of fire that burst out and fade fast and more
    puffs of smoke that drift up and shrink, all as small cubes the entity frame draws.

    Directions and sizes come from the explosion's position, so the same explosion looks the
    same; the number of puffs grows with the power and is bounded, and so is the number of puffs
    in all. Near the eye an explosion also lights the world in a short warm flash and shakes the
    camera (decision E-879). */
class QcExplosionEffects
{
public:
    /*! \brief One puff of fire or smoke of an explosion. */
    struct Puff
    {
        /*! Where it starts: the explosion's centre. */
        QcVec3 centre;
        /*! Metres per second of its burst. */
        QcVec3 velocity;
        /*! The client tick the explosion was added at. */
        quint64 startTick = 0;
        /*! How many ticks the puff lasts. */
        int lifeTicks = 20;
        /*! Its edge length in metres at the start. */
        double size = 1.0;
        /*! Whether it is a bright puff of fire (else smoke). */
        bool flash = false;
    };

    /*! \brief What a puff looks like at a moment (lookAt()). */
    struct Look
    {
        /*! Where the puff is now (its middle) and its edge length now, in metres. */
        QcEffectPuff cube;
        /*! Whether it is a puff of fire. */
        bool flash = false;
    };

    /*! \brief Camera offsets of a shake, in degrees. */
    struct Shake // NOLINT(qc-duplicate-struct): camera offsets of a shake, not the head angles of QcPreviewPose::Gaze
    {
        /*! Offset to the left or right. */
        double yaw = 0.0;
        /*! Offset up or down. */
        double pitch = 0.0;
    };

    /*! Puffs of fire per explosion. */
    static constexpr int flashPuffs = 10;
    /*! Puffs of smoke per unit of power (rounded up). */
    static constexpr int smokePuffsPerPower = 7;
    /*! How long an explosion lights the world and shakes the camera. \qcshared */
    static constexpr int flashTicks = 4;
    static constexpr int shakeTicks = 24;
    /*! Metres per unit of power within which the flash shows and the camera shakes. \qcshared */
    static constexpr double flashReachPerPower = 10.0;
    static constexpr double shakeReachPerPower = 6.0;
    /*! Largest offsets of a full-strength shake. */
    static constexpr double maxShakeDegrees = 2.5;
    /*! Most puffs at a time; an explosion that would exceed it adds none (its flash and shake still
        count). */
    static constexpr int maxPuffs = 512;
    /*! Most explosions whose flash and shake are followed at a time; a new one replaces the
        weakest. */
    static constexpr qsizetype maxBlasts = 64;
    /*! Content textures of the puffs (atlas names with the namespace); the campfire smoke
        (QcCampfireSmoke) is drawn with the smoke texture too. \qcshared */
    static constexpr auto flashTexture = "qtcraft:explosion_flash";
    static constexpr auto smokeTexture = "qtcraft:explosion_smoke";

    /*! Adds the puffs and the blast of \a explosion, which happened at client \a tick. */
    void add(const QcMsg::Explosion &explosion, quint64 tick);
    /*! Forgets the puffs and blasts that are over at \a tick. */
    void prune(quint64 tick);
    /*! Returns the puffs that are still running or not yet pruned. */
    const QList<Puff> &puffs() const { return m_puffs; }
    /*! Returns how \a puff looks at \a ticks (client ticks with the frame's fraction); nothing
        when the puff is over. */
    static std::optional<Look> lookAt(const Puff &puff, double ticks);
    /*! Returns the brightness (0..1) of the explosions' flash seen from \a eye at \a ticks (with
        the frame's fraction). */
    [[nodiscard]] double flashAt(const QcVec3 &eye, double ticks) const;
    /*! Returns the strength (0..1) of the shake the explosions give a camera at \a eye at
        \a ticks. */
    [[nodiscard]] double shakeAt(const QcVec3 &eye, double ticks) const;
    /*! Returns the camera offsets of a shake of \a strength at \a ticks: a quick, irregular
        tremble. */
    [[nodiscard]] static Shake shake(double strength, double ticks);

private:
    // An explosion while its flash and shake last.
    struct Blast
    {
        QcVec3 centre;
        double power = 1.0;
        quint64 tick = 0;
    };

    QList<Puff> m_puffs;
    QList<Blast> m_blasts;
};
