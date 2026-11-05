// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcPlayerMessages.h"

#include <QString>
#include <QtGlobal>

class QcContentRegistry;
class QcServerInventory;
class QcServerProjectiles;
class QcServerSounds;
class QcServerState;
struct QcItemDefinition;
struct QcLauncherDefinition;
struct QcServerClient;

/*! \brief Players drawing and shooting launchers (a bow, decision E-425).

    The server times the draw from the press of the use button (QcMsg::DrawLauncher) to its
    release in ticks; the release shoots the first ammunition of the inventory the launcher takes
    (decision E-427; in creative mode, when the inventory has none, creativeProjectile, and
    nothing is used) at the launcher's speed times the power of the draw. A shot uses one item of
    ammunition and wears the launcher by one, except in creative mode. Without ammunition a draw
    does not begin outside creative mode. A draw lets go without a shot when the client cancels
    it, the selected hotbar slot changes (also to another launcher), the selected item is no
    launcher any more, the player dies or the power stays below the launcher's minPower. A draw
    slows the player (draws(), decision E-986). */
class QcPlayerLaunchers
{
public:
    /*! How far ahead of the eyes a shot starts (m), so the shooter's box is behind it. */
    static constexpr double muzzle = 0.3;

    QcPlayerLaunchers(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcServerProjectiles &projectiles);

    /*! The player of \a client pressed or released the use button with the launcher, or
        cancelled the draw (\a action). */
    void draw(QcServerClient &client, QcMsg::DrawLauncher::Action action);
    /*! Ends draws whose launcher left the hand. */
    void tick();
    /*! Returns whether the player of \a client draws a launcher that is still in the hand, also
        between the change of the hand and the next tick(). */
    [[nodiscard]] bool draws(const QcServerClient &client) const;

    /*! The player of \a client shoots with the held launcher at \a power; returns false when
        nothing was shot (no launcher, no ammunition, power below its minimum, the projectile
        limit). */
    bool shoot(QcServerClient &client, double power);

    /*! Returns what creative mode shoots with \a launcher without ammunition: the projectile of
        the item of the first kind (in the order of \a content) the launcher takes; empty when it
        takes none. */
    static QString creativeProjectile(const QcContentRegistry &content, const QcLauncherDefinition &launcher);

private:
    // Whether `client` may draw (or keep drawing) with `held`, its selected item.
    static bool mayDraw(const QcServerClient &client, const QcItemDefinition *held);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcServerProjectiles &m_projectiles;
};
