// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientLauncher.h"
#include "clientcore/sound/QcSoundCue.h"
#include "common/core/QcAabb.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcAdventureRules.h"
#include "common/physics/QcRaycast.h"
#include "common/protocol/QcMessages.h"

#include <QList>

#include <functional>
#include <optional>

class QcClientEntities;
class QcClientGameMode;
class QcClientInventory;
class QcClientWorld;
class QcContentRegistry;
class QcPrediction;

// What the local player's hand does: the block and the entity under the crosshair, mining the
// targeted block (survival progress and hit sounds, instant breaking in creative mode), using the
// held item on a block, an entity or itself, drawing and releasing a launcher (QcClientLauncher)
// and attacking. Sends its requests through the sender; the server decides and answers. The blocks
// a break or a placement is expected to set (QcBlockForecast) go to the predictor first, which shows
// them at once and numbers the request (decision E-853). What an action means for the arm and the
// screens it returns as a HandEffect, which the session applies.
class QcHandActions
{
public:
    using Sender = std::function<void(const QcMessage &message)>;
    using Cue = std::function<void(const QcSoundCue &cue)>;
    // Shows the blocks an action is expected to set and returns the number the request carries
    // (QcMsg::FinishMining::sequence, QcMsg::UseItem::sequence); 0 when nothing is predicted.
    using Predictor = std::function<quint32(const QList<QcMsg::BlockUpdate> &blocks)>;

    // Mining strikes make a sound every this many ticks.
    static constexpr int miningHitTicks = 4;
    // Melee reach of the player.
    static constexpr double attackReach = 4.0;
    // While the use button is held, the use repeats every this many ticks (decision E-885).
    static constexpr int useRepeatTicks = 4;

    // What an action did beyond its requests, for the session to apply after them.
    enum class HandEffect
    {
        // Nothing was done.
        None,
        // Done without a visible effect (a launcher draws).
        Handled,
        // The arm swings (a strike, a throw, a block broken at once).
        Swing,
        // The arm makes the use motion.
        Use,
        // Use motion; the player used a block, so a machine screen it opens shows again.
        UsedBlock,
        // Use motion; the player used an entity, so a machine or trading screen it opens shows again.
        UsedEntity,
        // Nothing was done because the mode forbids it (adventure allow-lists, decision E-735); the
        // HUD says so.
        Refused,
    };

    // What the hand reads of the session: the predicted player, its items and the entity mirror.
    // The session declares them before the hand, so they outlive it.
    struct View
    {
        const QcPrediction &prediction;
        const QcClientInventory &inventory;
        const QcClientEntities &entities;
    };

    // `gameMode`: the rules and the instant breaking of creative mode; `send`: how requests reach
    // the server; `cue`: where the mining sounds go; `predict`: where predicted blocks go (all the
    // session's, never empty).
    QcHandActions(const View &view, QcClientGameMode &gameMode, Sender send, Cue cue, Predictor predict);

    // Block the player looks at (within reach) and the entity under the crosshair when it is closer.
    const std::optional<QcRaycastHit> &target() const { return m_target; }
    std::optional<quint32> targetEntity() const { return m_targetEntity; }
    // Aims again from the predicted eye along the intent; clearTarget() while there is no world.
    void refreshTarget(const QcClientWorld &world, const QcContentRegistry &content);
    void clearTarget() { m_target.reset(); }

    // While requested, the targeted block is mined; progress runs from 0 to 1 and resets on target
    // change. Releasing ends the pace of held instant breaking, so the next press breaks at once.
    void setMining(bool mining);
    bool miningRequested() const { return m_miningRequested; }
    double miningProgress() const { return m_miningProgress; }
    // Whether a block is being mined (survival) right now.
    bool isMining() const { return m_miningPos.has_value(); }
    // Once per tick after the prediction: aims again and advances or changes the mining; Swing when
    // a block broke at once (creative mode), Refused while the mode forbids mining the aimed block
    // (adventure mode: no mining at all, no cracks or sounds).
    HandEffect updateMining(const QcClientWorld &world, const QcContentRegistry &content);
    // The mining button was just pressed: in creative mode the aimed block breaks now instead of at
    // the next tick, at the pace of updateMining (decision E-853); Swing when it broke.
    HandEffect strike(const QcClientWorld &world, const QcContentRegistry &content);

    // Once per tick: the launcher's draw and the cooldowns of throwing (`serverTicks` per tick).
    void tickLauncher(const QcContentRegistry &content, double serverTicks);
    // Progress of the drawn launcher within the tick, negative when none is drawn.
    double drawProgress(double alpha) const { return m_launcher.progress(alpha); }

    // Uses the held item on the targeted entity, the block face or itself; None when nothing was used,
    // Refused when the mode forbids the item to act on the block (food is eaten instead).
    HandEffect use(const QcClientWorld &world, const QcContentRegistry &content);
    // The use button was pressed (after its first use()): from now on tickUse repeats the use until
    // endUse (decision E-885).
    void holdUse();
    bool useHeld() const { return m_useHeld; }
    // Once per tick after the aim: while the use button is held and no mining is wanted, uses the held
    // item again every useRepeatTicks ticks, but only where a repeat is wanted (repeatsUse); None
    // otherwise.
    HandEffect tickUse(const QcClientWorld &world, const QcContentRegistry &content);
    // The use button released or cancelled: the repeats stop and a drawn launcher shoots or lets go
    // (decision E-425); the server hears of it only while `send` (the session plays).
    void endUse(QcMsg::DrawLauncher::Action action, bool send);
    // Hits the targeted entity (Swing); None without one.
    HandEffect attack();

private:
    // The block mining is aimed at, none when the mode forbids mining it (`refused` then).
    struct Aimed
    {
        std::optional<QcBlockPos> block;
        bool refused = false;
    };

    [[nodiscard]] Aimed aimedForMining(const QcClientWorld &world, const QcContentRegistry &content) const;
    // Creative mode: breaks the aimed block when the pace allows (QcClientGameMode::nextInstantBreak).
    HandEffect breakInstantly(const QcClientWorld &world, std::optional<QcBlockPos> aimed);
    // Sends the end of a mining with the predicted break.
    void finishMining(const QcClientWorld &world, QcBlockPos pos);
    // The number of the predicted placement of the held item on `target`, aimed at `hit` inside it (0
    // when none is predicted).
    quint32 predictPlacement(const QcClientWorld &world, const QcContentRegistry &content, const QcRaycastHit &target, const QcVec3 &hit);
    // The blocks placing the held item on `target` at `hit` is forecast to set (empty: none forecast).
    [[nodiscard]] QList<QcMsg::BlockUpdate> placementForecast(const QcClientWorld &world, const QcContentRegistry &content, const QcRaycastHit &target,
                                                              const QcVec3 &hit) const;
    // Where `target` was hit, relative to its block and clamped into it.
    [[nodiscard]] static QcVec3 hitInside(const QcRaycastHit &target);
    // The living bodies a placed block must not enclose: the player's own and those of the players and
    // creatures the entity mirror shows.
    QList<QcAabb> livingBodies(const QcContentRegistry &content) const;
    // Whether the mode lets the held item do `action` to the block at `pos` (the same rule as the
    // server's, QcAdventureRules::permits); false where no block is loaded.
    bool permitted(const QcClientWorld &world, const QcContentRegistry &content, QcAdventureRules::Action action, QcBlockPos pos) const;
    // Whether a held use button repeats on what is aimed at now (decision E-885): a held item used on
    // a block that neither reacts to the use (screens, doors, levers) nor takes food, and only where
    // the placement of a block is forecast. Launchers, food (planting food too), buckets and bottles,
    // entities and the empty hand do not repeat.
    bool repeatsUse(const QcClientWorld &world, const QcContentRegistry &content) const;
    // Uses the held item on the targeted entity; None when the entity does not take it.
    HandEffect useOnEntity(const QcContentRegistry &content, const QcItemDefinition *item);

    View m_view;
    QcClientGameMode &m_gameMode;
    Sender m_send;
    Cue m_cue;
    Predictor m_predict;
    std::optional<QcRaycastHit> m_target;
    std::optional<quint32> m_targetEntity;
    bool m_miningRequested = false;
    std::optional<QcBlockPos> m_miningPos;
    double m_miningProgress = 0.0;
    int m_miningTicks = 0;
    QcClientLauncher m_launcher;
    bool m_useHeld = false;
    int m_useTicks = 0;
};
