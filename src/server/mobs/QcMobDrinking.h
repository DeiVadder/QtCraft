// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/mobs/QcMobDrinkDefinition.h"

#include <QList>
#include <QtGlobal>

class QcContentRegistry;
class QcServerSounds;
class QcServerState;
struct QcCreature;
struct QcMobDefinition;

/*! \brief Creatures that drink potions when they need them (QcMobDefinition::drinks, decisions
    E-623, E-1105; the witch), every game tick after the creatures thought.

    An archer that does not drink tries its potions in order: the first whose need holds (eyes under
    water, burning, hurt, target farther than its distance), whose chance comes up and none of whose
    lasting effects it has already or is immune to, begins with the drink's sound. It holds the
    potion (QcEntitySync), neither draws, shoots nor walks (QcArcherBrain) for \c {seconds}, then
    gets the status effects the potion's item gives when drunk (QcDrinkDefinition::effects).

    Hurting it does not break the drink; dying while it drinks drops the potion it holds by
    \c {dropChance} (heldDrops()). Nothing of it is saved: a loaded witch begins again. */
class QcMobDrinking
{
public:
    QcMobDrinking(QcServerState &state, QcServerSounds &sounds);

    /*! Begins and finishes the drinks of this game tick. */
    void tick();
    /*! Returns the drinks finished since the start (tests, measurements). */
    int drinks() const { return m_drinks; }
    /*! Returns what \a mob drops of its drink when it dies: the potion it holds with its kind's
        \c {dropChance} as \a content defines it, or nothing when it does not drink (decision
        E-1105). */
    static QList<QcItemDrop> heldDrops(const QcContentRegistry &content, const QcCreature &mob);

private:
    // Returns the potion `mob` begins to drink now, if any.
    const QcMobDrinkDefinition::Potion *choose(const QcCreature &mob, const QcMobDefinition &definition);
    // Returns whether `need` of `potion` holds for `mob`.
    bool needs(const QcCreature &mob, const QcMobDefinition &definition, const QcMobDrinkDefinition::Potion &potion) const;
    // Returns whether `mob` has, or is immune to, one of the lasting effects of the potion `item`.
    bool hasEffectOf(const QcCreature &mob, const QcMobDefinition &definition, QcItemId item) const;
    // Gives `mob` what its potion gives and ends the drink.
    void finish(QcCreature &mob);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    int m_drinks = 0;
};
