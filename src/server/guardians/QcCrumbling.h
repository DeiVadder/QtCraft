// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcMobDefinition.h"
#include "common/core/QcBlockPos.h"

#include <QList>

class QcServerBlocks;
class QcServerSounds;
class QcServerState;
struct QcCreature;

// Creatures that crumble at a liquid's source (QcMobDefinition::crumbles, decision E-593; the sand
// golem in water), every game tick after the creatures moved: an awake one whose body (grown by
// `touch` sideways and below) reaches into a source block of its liquid is removed without loot or
// experience, and its build stands where it stood: `body` at its feet, in the middle above them and
// on both sides of the middle across its facing, `head` on the middle, facing where it looked. Each
// block goes into an open cell (air, a replaceable block, flowing liquid; never a source) with the
// usual block updates (sand falls, and falling sand may fill a source below: a farm keeps its water
// out of the arms' way); a block without an open cell drops as an item there. A creature that may
// not change the world (QcCreature::mayGrief: the game rule mobGriefing off, confined, no mind;
// decision E-510) leaves its whole build as items. Games count what crumbled
// (tests, measurements).
class QcCrumbling
{
public:
    // How far beyond its body a creature feels a source: a source beside it or under its feet counts.
    static constexpr double touch = 0.05;

    QcCrumbling(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);

    void tick();
    // Creatures that crumbled since the start.
    int crumbled() const { return m_crumbled; }
    // Whether the body of `mob` touches a source block of `liquid` (a block name).
    bool touchesSource(const QcCreature &mob, const QcMobDefinition &definition, const QString &liquid) const;
    // The cells of a crumbled build at `feet`: feet, middle, the two arms, the head; the arms across
    // the facing of `yaw` (degrees, 0 north).
    static QList<QcBlockPos> buildCells(QcBlockPos feet, double yaw);

private:
    void crumble(const QcCreature &mob, const QcCrumbleDefinition &rule);
    // Puts `block` into `cell` when it is open; else drops the block's item there.
    void place(QcBlockPos cell, QcBlockId block);
    void drop(QcBlockPos cell, QcBlockId block);
    // The head's state facing along `yaw` where the block has a facing.
    QcBlockId headFacing(QcBlockId head, double yaw) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    int m_crumbled = 0;
};
