#pragma once

#include "clientcore/session/QcClientLauncher.h"
#include "clientcore/sound/QcSoundCue.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcAdventureRules.h"
#include "common/physics/QcRaycast.h"
#include "common/protocol/QcMessages.h"

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
// and attacking. Sends its requests through the sender; the server decides and answers. What an
// action means for the arm and the screens it returns as a HandEffect, which the session applies.
class QcHandActions
{
public:
    using Sender = std::function<void(const QcMessage &message)>;
    using Cue = std::function<void(const QcSoundCue &cue)>;

    // Mining strikes make a sound every this many ticks.
    static constexpr int miningHitTicks = 4;
    // Melee reach of the player.
    static constexpr double attackReach = 4.0;

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
    // the server; `cue`: where the mining sounds go (both the session's, never empty).
    QcHandActions(const View &view, QcClientGameMode &gameMode, Sender send, Cue cue);

    // Block the player looks at (within reach) and the entity under the crosshair when it is closer.
    const std::optional<QcRaycastHit> &target() const { return m_target; }
    std::optional<quint32> targetEntity() const { return m_targetEntity; }
    // Aims again from the predicted eye along the intent; clearTarget() while there is no world.
    void refreshTarget(const QcClientWorld &world, const QcContentRegistry &content);
    void clearTarget() { m_target.reset(); }

    // While requested, the targeted block is mined; progress runs from 0 to 1 and resets on target change.
    void setMining(bool mining) { m_miningRequested = mining; }
    bool miningRequested() const { return m_miningRequested; }
    double miningProgress() const { return m_miningProgress; }
    // Whether a block is being mined (survival) right now.
    bool isMining() const { return m_miningPos.has_value(); }
    // Once per tick after the prediction: aims again and advances or changes the mining; Swing when
    // a block broke at once (creative mode), Refused while the mode forbids mining the aimed block
    // (adventure mode: no mining at all, no cracks or sounds).
    HandEffect updateMining(const QcClientWorld &world, const QcContentRegistry &content);

    // Once per tick: the launcher's draw and the cooldowns of throwing (`serverTicks` per tick).
    void tickLauncher(const QcContentRegistry &content, double serverTicks);
    // Progress of the drawn launcher within the tick, negative when none is drawn.
    double drawProgress(double alpha) const { return m_launcher.progress(alpha); }

    // Uses the held item on the targeted entity, the block face or itself; None when nothing was used,
    // Refused when the mode forbids the item to act on the block (food is eaten instead).
    HandEffect use(const QcClientWorld &world, const QcContentRegistry &content);
    // The use button released or cancelled: a drawn launcher shoots or lets go (decision E-425);
    // the server hears of it only while `send` (the session plays).
    void endUse(QcMsg::DrawLauncher::Action action, bool send);
    // Hits the targeted entity (Swing); None without one.
    HandEffect attack();

private:
    // Whether the mode lets the held item do `action` to the block at `pos` (the same rule as the
    // server's, QcAdventureRules::permits); false where no block is loaded.
    bool permitted(const QcClientWorld &world, const QcContentRegistry &content, QcAdventureRules::Action action, QcBlockPos pos) const;
    // Uses the held item on the targeted entity; None when the entity does not take it.
    HandEffect useOnEntity(const QcContentRegistry &content, const QcItemDefinition *item);

    View m_view;
    QcClientGameMode &m_gameMode;
    Sender m_send;
    Cue m_cue;
    std::optional<QcRaycastHit> m_target;
    std::optional<quint32> m_targetEntity;
    bool m_miningRequested = false;
    std::optional<QcBlockPos> m_miningPos;
    double m_miningProgress = 0.0;
    int m_miningTicks = 0;
    QcClientLauncher m_launcher;
};
