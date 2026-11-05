// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcEffectPuff.h"
#include "common/core/QcVec3.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

struct QcClientEntity;
class QcContentRegistry;
namespace QcMsg {
struct EntityMoves;
}

/*! \brief The clouds of ink that struck squids leave in the water (decision E-974): a creature
    whose kind has an ink texture (QcMobDefinition::ink) and whose move says a blow struck it
    (QcMsg::EntityMove::struck: an attack, a projectile, an explosion; not burning, drowning or
    suffocation) leaves a few dark puffs around the middle of its body as drawn that drift apart,
    swell and shrink again within about two seconds, drawn as small cubes like the smoke of
    explosions.

    Only a client effect: the server sends nothing for it, the clouds hide nothing and are not
    saved. The directions come from the creature's id and the tick, so the same hit looks the
    same; the number of puffs in all is bounded. */
class QcInkClouds
{
public:
    /*! \brief One puff of a cloud. \qcfields */
    struct Puff
    {
        QcVec3 centre;
        QcVec3 velocity;
        quint64 startTick = 0;
        int lifeTicks = 30;
        double size = 0.4;
        QString texture;
    };

    /*! Puffs of one cloud. */
    static constexpr int puffsPerCloud = 16;
    /*! Most puffs at a time; a cloud that would exceed it adds none. */
    static constexpr qsizetype maxPuffs = 256;
    /*! Ticks a puff lasts, at least and at most. \qcshared */
    static constexpr int minLifeTicks = 24;
    static constexpr int maxLifeTicks = 44;

    /*! Adds a cloud of \a texture around \a centre for the creature \a id at client \a tick. */
    void add(const QcVec3 &centre, const QString &texture, quint32 id, quint64 tick);
    /*! Adds a cloud for every struck creature of \a moves whose kind (of \a entities, kinds
        from \a content) leaves ink, around where it is drawn, at client \a tick. */
    void addStruck(const QcMsg::EntityMoves &moves, const QHash<quint32, QcClientEntity> &entities, const QcContentRegistry &content, quint64 tick);
    /*! Forgets the puffs that are over at \a tick. */
    void prune(quint64 tick);
    /*! Returns the puffs not yet pruned. */
    const QList<Puff> &puffs() const { return m_puffs; }
    /*! Returns how \a puff looks at \a ticks (client ticks with the frame's fraction); nothing
        when it is over or not yet there. */
    [[nodiscard]] static std::optional<QcEffectPuff> lookAt(const Puff &puff, double ticks);

private:
    // NOLINT(qc-member-access): add() appends and prune() removes; nothing else writes it.
    QList<Puff> m_puffs;
};
