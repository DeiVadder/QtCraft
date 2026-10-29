// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QcServerInventory;
class QcServerProjectiles;
class QcServerState;
struct QcServerClient;

// Players throwing items by hand (a snowball, decision E-427; QcMsg::ThrowItem): the selected item,
// when it is throwable (QcItemDefinition::throwable), leaves the eyes as its projectile at the
// item's speed, turned at random by up to its inaccuracy. Every throw uses one item, except in
// creative mode, and the item cannot be thrown again before its cooldown ends; the server keeps
// the cooldown, the client only keeps the arm from swinging before it.
class QcPlayerThrows
{
public:
    QcPlayerThrows(QcServerState &state, QcServerInventory &inventory, QcServerProjectiles &projectiles);

    // Throws the selected item; false when nothing was thrown (not throwable, the cooldown, the
    // projectile limit, a dead or absent player).
    bool throwHeld(QcServerClient &client);

private:
    QcServerState &m_state;
    QcServerInventory &m_inventory;
    QcServerProjectiles &m_projectiles;
};
