#pragma once

#include "QcClientEntity.h"
#include "QcClientWorld.h"
#include "QcLocalSounds.h"
#include "QcMessages.h"
#include "QcSoundCue.h"

#include <QHash>

#include <cmath>

class QcContentRegistry;

// Client mirror of the entities the server reports, with their animation timers and the ones that
// play their death animation.
class QcClientEntities
{
public:
    // Animation lengths in ticks.
    static constexpr int swingTicks = 6;
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
    void move(const QcMsg::EntityMove &move);
    void remove(QcMsg::EntityRemove removed);
    // Advances the animation timers by one tick and drops finished death animations.
    void tick();
    // Passes a step sound to `cue` for every creature that walked a stride.
    template<typename Cue>
    void stepSounds(const QcClientWorld &world, Cue &&cue);

private:
    QHash<quint32, QcClientEntity> m_entities;
    QHash<quint32, QcClientEntity> m_dying;
};

template<typename Cue>
void QcClientEntities::stepSounds(const QcClientWorld &world, Cue &&cue)
{
    // Other creatures' steps, from how far they moved; items do not step.
    for (QcClientEntity &entity : m_entities) {
        if (entity.isItem() || entity.isFallingBlock() || (entity.flags & QcMsg::OnGround) == 0) {
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
