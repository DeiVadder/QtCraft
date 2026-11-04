// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/content/mobs/QcMobDefinition.h"
#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcMessages.h"

#include <QHash>

#include <optional>

/*! \brief Drawing a launcher on the client (a bow, decision E-425): the use button held with a
    launcher in the hand draws it, its release shoots.

    The server times the draw and decides about the shot (QcMsg::DrawLauncher); the client times
    it too, only for the animation. A draw belongs to the hotbar slot it began in, as on the
    server (QcPlayerLaunchers). Throwing by hand (decision E-427) keeps the cooldown of each
    item, so the arm swings only for throws the server makes. */
class QcClientLauncher
{
public:
    /*! Starts a draw with \a held, the selected item of \a inventory. Returns true when it started
        now (the session sends the press). Without ammunition in \a inventory only \a creative mode
        draws, as on the server (QcPlayerLaunchers); \a content names the ammunition. */
    bool press(const QcItemDefinition &held, const QcInventory &inventory, const QcContentRegistry &content, bool creative);
    /*! Ends a draw. Returns true when one was running (the session sends the release or the
        cancel). */
    bool release();
    /*! One client tick with \a held in the selected hotbar slot \a slot: a draw whose launcher
        left the hand or whose slot is no longer selected ends (QcHandActions sends the cancel).
        \a serverTicks: the server's ticks in this client tick (QcMsg::WorldTime::speed: 1 at its
        normal rate, more in a sprint, 0 frozen), by which the cooldowns of throws run as on the
        server. */
    void tick(const QcItemDefinition *held, int slot, double serverTicks = 1.0);

    /*! Returns the use button pressed with \a definition (the item \a item, selected in
        \a inventory) in the hand: a throw when it is throwable and its cooldown is over, a cast
        or reel of a fishing rod (also QcMsg::ThrowItem, decision E-503), else the start of a
        draw (press, with \a content and \a creative); nothing when none happens. */
    std::optional<QcMessage> use(QcItemId item, const QcItemDefinition &definition, const QcInventory &inventory, const QcContentRegistry &content,
                                 bool creative);
    /*! Returns whether the use button with \a item in the hand goes to use() (a launcher, a
        throwable item or a fishing rod). */
    static bool uses(const QcItemDefinition &item) { return item.launcher || item.throwable || item.fishing; }
    /*! Returns whether using \a item on a creature of the kind \a creature goes to the creature
        (QcMsg::UseEntity): food, armour, its catching item, its milking item (decision E-500) or
        its breeding food (E-293). */
    static bool usesOnCreature(const QcMobDefinition &creature, const QcItemDefinition &item);
    /*! Returns whether the use button with \a item sends QcMsg::ConsumeItem: food is eaten
        (unless it plants a block and \a aimsAtBlock: then it is placed, decision E-292), drinks
        drunk (decision E-500) and armour put on; an empty map is used anywhere (the server fills
        it, decision E-804). */
    static bool consumes(const QcItemDefinition &item, bool aimsAtBlock);
    /*! Returns the swing of the arm after use() sent \a message: 0 for a throw, which swings it
        from the start, else \a swingTicks. */
    static int swingAfter(const std::optional<QcMessage> &message, int swingTicks)
    {
        return message && std::holds_alternative<QcMsg::ThrowItem>(*message) ? 0 : swingTicks;
    }
    /*! Returns whether \a item may be thrown now; true starts the cooldown of \a throwable (the
        session sends the throw). */
    bool mayThrow(QcItemId item, const QcThrowableDefinition &throwable);

    /*! Returns whether a draw is running. */
    bool drawing() const { return m_launcher.has_value(); }
    /*! Returns the power the draw reached \a alpha into the current tick (0..1); negative
        without a draw. */
    double progress(double alpha) const;

private:
    std::optional<QcLauncherDefinition> m_launcher;
    int m_slot = -1;
    int m_ticks = 0;
    // Server ticks so far as the client counts them, and the tick from which each item may be thrown again.
    double m_clock = 0.0;
    QHash<QcItemId, double> m_throwReady;
};
