// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientEntity.h"
#include "clientcore/session/QcClientWorld.h"
#include "clientcore/session/QcSnapshotBuffer.h"
#include "clientcore/session/QcSnapshotClock.h"
#include "clientcore/sound/QcLocalSounds.h"
#include "clientcore/sound/QcSoundCue.h"
#include "common/gameplay/QcSwingSpeed.h"
#include "common/protocol/QcMessages.h"

#include <QElapsedTimer>
#include <QHash>

#include <cmath>

class QcContentRegistry;

/*! \brief Client mirror of the entities the server reports, with their animation timers and the
    ones that play their death animation.

    Motion is shown a little in the past from buffered snapshots so that late or bunched messages
    do not make others stutter (decision E-271); health, flags and swings show when they arrive,
    a hurt flash when the shown moment reaches the hurt, together with its knockback (decision
    E-971). */
class QcClientEntities
{
public:
    QcClientEntities() { m_localClock.start(); }

    /*! Animation lengths in ticks. Other players' swings take the base length (their status
        effects are not sent, decision E-881). \qcshared */
    static constexpr int swingTicks = QcSwingSpeed::baseTicks;
    static constexpr int deathTicks = 20;
    /*! The red flash lasts the reference's hurt time (decision E-971). */
    static constexpr int hurtFlashTicks = 10;
    /*! Share of the newest tick in the smoothed walking speed. */
    static constexpr double speedSmoothing = 0.3;
    /*! Creatures step every this many metres. */
    static constexpr double strideMetres = 1.7;
    /*! Volume of a creature's step sound. */
    static constexpr float stepVolume = 0.3f;

    /*! Returns the entities by id as the server last reported them. */
    const QHash<quint32, QcClientEntity> &all() const { return m_entities; }
    /*! Returns the removed entities that still play their death animation, by id. */
    const QHash<quint32, QcClientEntity> &dying() const { return m_dying; }

    /*! Takes the entity of \a spawn (creatures checked against \a content). Returns false when
        the server sent an unknown entity type. */
    bool spawn(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content);
    /*! Takes \a moves, arrived now on the internal steady clock of tick(). */
    void moves(const QcMsg::EntityMoves &moves) { this->moves(moves, m_localClock.elapsed()); }
    /*! Takes \a moves that arrived at \a localMs on the clock of tick(). */
    void moves(const QcMsg::EntityMoves &moves, qint64 localMs);
    /*! Removes the entity \a removed names; one that died plays its death animation first,
        except items, experience orbs and projectiles, which vanish at once. */
    void remove(QcMsg::EntityRemove removed);
    /*! Forgets every entity, the dying ones and their snapshots (another dimension, decision
        E-1002). */
    void clear()
    {
        m_entities.clear();
        m_dying.clear();
        m_tracks.clear();
    }
    /*! Moves every entity to its pose one tick after the shown moment, advances the animation
        timers and drops finished death animations. */
    void tick() { tick(m_localClock.elapsed()); }
    /*! Like tick(), with \a serverTicks the server's ticks in this client tick (its tick rate or
        sprint; 0 while it stands still): the draws of others count those (decision E-987). */
    void tickBy(double serverTicks) { tick(m_localClock.elapsed(), serverTicks); }
    /*! Like tickBy(), with \a localMs as the time on the clock of moves(). */
    void tick(qint64 localMs, double serverTicks = 1.0);

    /*! Sets whether snapshots are \a buffered; without the buffer each tick shows the newest
        snapshot (for comparisons in tests). */
    void setBuffered(bool buffered) { m_buffered = buffered; }
    /*! Returns whether motion is shown from the snapshot buffer (setBuffered()). */
    bool isBuffered() const { return m_buffered; }
    /*! Returns the snapshot clock: how far in the past motion is shown. */
    const QcSnapshotClock &clock() const { return m_clock; }
    /*! The server resumed after a pause (decision E-877): the snapshot clock starts anew. */
    void restartClock() { m_clock.restart(); }
    /*! Passes a step sound to \a cue for every creature that walked a stride, on the ground of
        \a world. */
    template<typename Cue>
    void stepSounds(const QcClientWorld &world, const Cue &cue);

private:
    void applyMove(const QcMsg::EntityMove &move, double serverMs);
    // The entity's pose for the tick ending at `shown` plus one tick (shown: none before any snapshot).
    void advancePose(QcClientEntity &entity, std::optional<double> shown);
    // Starts the flash of a waiting hurt of `entity` once the `shown` moment reached it.
    static void startHurtFlash(QcClientEntity &entity, std::optional<double> shown);
    // Counts the draw of `entity` on by `serverTicks`, or ends it with its flag.
    static void advanceDraw(QcClientEntity &entity, double serverTicks);

    QHash<quint32, QcClientEntity> m_entities;
    QHash<quint32, QcClientEntity> m_dying;
    QHash<quint32, QcSnapshotBuffer> m_tracks;
    QcSnapshotClock m_clock;
    QElapsedTimer m_localClock;
    bool m_buffered = true;
};

template<typename Cue>
void QcClientEntities::stepSounds(const QcClientWorld &world, const Cue &cue)
{
    // Other creatures' steps, from how far they moved; items, carts, boats and riders do not step.
    for (QcClientEntity &entity : m_entities) {
        const bool riding = (entity.flags & QcMsg::Riding) != 0;
        if (entity.isItem() || entity.isBlockEntity() || entity.isMinecart() || entity.isBoat() || riding || (entity.flags & QcMsg::OnGround) == 0) {
            entity.sinceStep = 0.0;
            continue;
        }

        entity.sinceStep += entity.lastStep;
        if (entity.sinceStep >= strideMetres) {
            entity.sinceStep = std::fmod(entity.sinceStep, strideMetres);
            cue(QcSoundCue{QcLocalSounds::stepSound(entity.position, world), entity.position, stepVolume, 1.0f,
                           QcSoundCue::Origin::Local, 0});
        }
    }
}
