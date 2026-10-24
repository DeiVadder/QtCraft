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

// Client mirror of the entities the server reports, with their animation timers and the ones that
// play their death animation. Motion is shown a little in the past from buffered snapshots so that
// late or bunched messages do not make others stutter (decision E-271); health, flags, swings and
// hurt flashes show when they arrive.
class QcClientEntities
{
public:
    QcClientEntities() { m_localClock.start(); }

    // Animation lengths in ticks. Other players' swings take the base length (their status
    // effects are not sent, decision E-881).
    static constexpr int swingTicks = QcSwingSpeed::baseTicks;
    static constexpr int deathTicks = 20;
    static constexpr int hurtFlashTicks = 6;
    // Share of the newest tick in the smoothed walking speed.
    static constexpr double speedSmoothing = 0.3;
    // Creatures step every this many metres.
    static constexpr double strideMetres = 1.7;
    static constexpr float stepVolume = 0.3f;

    const QHash<quint32, QcClientEntity> &all() const { return m_entities; }
    const QHash<quint32, QcClientEntity> &dying() const { return m_dying; }

    // False when the server sent an unknown entity type.
    bool spawn(const QcMsg::EntitySpawn &spawn, const QcContentRegistry *content);
    // `localMs`: when the message arrived, on the clock of tick() (default: an internal steady clock).
    void moves(const QcMsg::EntityMoves &moves) { this->moves(moves, m_localClock.elapsed()); }
    void moves(const QcMsg::EntityMoves &moves, qint64 localMs);
    void remove(QcMsg::EntityRemove removed);
    // Moves every entity to its pose one tick after the shown moment, advances the animation timers
    // and drops finished death animations.
    void tick() { tick(m_localClock.elapsed()); }
    void tick(qint64 localMs);

    // Without the buffer each tick shows the newest snapshot (for comparisons in tests).
    void setBuffered(bool buffered) { m_buffered = buffered; }
    bool isBuffered() const { return m_buffered; }
    const QcSnapshotClock &clock() const { return m_clock; }
    // The server resumed after a pause (decision E-877): the snapshot clock starts anew.
    void restartClock() { m_clock.restart(); }
    // Passes a step sound to `cue` for every creature that walked a stride.
    template<typename Cue>
    void stepSounds(const QcClientWorld &world, Cue &&cue);

private:
    void applyMove(const QcMsg::EntityMove &move, double serverMs);
    // The entity's pose for the tick ending at `shown` plus one tick (shown: none before any snapshot).
    void advancePose(QcClientEntity &entity, std::optional<double> shown);

    QHash<quint32, QcClientEntity> m_entities;
    QHash<quint32, QcClientEntity> m_dying;
    QHash<quint32, QcSnapshotBuffer> m_tracks;
    QcSnapshotClock m_clock;
    QElapsedTimer m_localClock;
    bool m_buffered = true;
};

template<typename Cue>
void QcClientEntities::stepSounds(const QcClientWorld &world, Cue &&cue)
{
    // Other creatures' steps, from how far they moved; items, carts and riders do not step.
    for (QcClientEntity &entity : m_entities) {
        const bool riding = (entity.flags & QcMsg::Riding) != 0;
        if (entity.isItem() || entity.isBlockEntity() || entity.isMinecart() || riding || (entity.flags & QcMsg::OnGround) == 0) {
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
