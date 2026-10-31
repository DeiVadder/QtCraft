// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

class QcServerSounds;
class QcServerState;

/*! \brief Creatures that drink to heal (QcMobDefinition::drinks, decision E-623; the witch), every
    game tick after the creatures thought: an archer whose health lies \c {heal} or more below its
    most begins to drink when its last drink is \c {cooldownSeconds} past; it holds the drink's item
    (QcEntitySync) and neither draws nor shoots (QcArcherBrain) for \c {seconds}, then heals by
    \c {heal} (up to its most) with the drink's sound.

    Hurting it does not break the drink. Nothing of it is saved: a loaded witch begins again. */
class QcMobDrinking
{
public:
    QcMobDrinking(QcServerState &state, QcServerSounds &sounds);

    /*! Begins and finishes the drinks of this game tick. */
    void tick();
    /*! Returns the drinks finished since the start (tests, measurements). */
    int drinks() const { return m_drinks; }

private:
    QcServerState &m_state;
    QcServerSounds &m_sounds;
    int m_drinks = 0;
};
