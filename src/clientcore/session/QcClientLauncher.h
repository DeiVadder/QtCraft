#pragma once

#include "common/content/definitions/QcItemDefinition.h"
#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcMessages.h"

#include <QHash>

#include <optional>

// Drawing a launcher on the client (a bow, decision E-425): the use button held with a launcher in
// the hand draws it, its release shoots. The server times the draw and decides about the shot
// (QcMsg::DrawLauncher); the client times it too, only for the animation. A draw belongs to the
// hotbar slot it began in, as on the server (QcPlayerLaunchers). Throwing by hand (decision E-427)
// keeps the cooldown of each item, so the arm swings only for throws the server makes.
class QcClientLauncher
{
public:
    // Starts a draw with `held`, the selected item of `inventory`; true when it started now (the
    // session sends the press). Without ammunition in `inventory` only `creative` mode draws, as on
    // the server (QcPlayerLaunchers).
    bool press(const QcItemDefinition &held, const QcInventory &inventory, const QcContentRegistry &content, bool creative);
    // Ends a draw; true when one was running (the session sends the release or the cancel).
    bool release();
    // One client tick with `held` in the selected hotbar slot `slot`: a draw whose launcher left the
    // hand or whose slot is no longer selected ends without a message, the server lets it go by itself.
    // `serverTicks`: the server's ticks in this client tick (QcMsg::WorldTime::speed: 1 at its normal
    // rate, more in a sprint, 0 frozen), by which the cooldowns of throws run as on the server.
    void tick(const QcItemDefinition *held, int slot, double serverTicks = 1.0);

    // The use button pressed with `definition` (the item `item`, selected in `inventory`) in the
    // hand: a throw when it is throwable and its cooldown is over, else the start of a draw (press);
    // nothing when neither happens.
    std::optional<QcMessage> use(QcItemId item, const QcItemDefinition &definition, const QcInventory &inventory, const QcContentRegistry &content,
                                 bool creative);
    // Whether the use button with `item` in the hand goes to use() (a launcher or a throwable item).
    static bool uses(const QcItemDefinition &item) { return item.launcher || item.throwable; }
    // The swing of the arm after use() sent `message`: a throw swings it from the start.
    static int swingAfter(const std::optional<QcMessage> &message, int swingTicks)
    {
        return message && std::holds_alternative<QcMsg::ThrowItem>(*message) ? 0 : swingTicks;
    }
    // Whether `item` may be thrown now; true starts its cooldown (the session sends the throw).
    bool mayThrow(QcItemId item, const QcThrowableDefinition &throwable);

    bool drawing() const { return m_launcher.has_value(); }
    // The power the draw reached `alpha` into the current tick (0..1); negative without a draw.
    double progress(double alpha) const;

private:
    std::optional<QcLauncherDefinition> m_launcher;
    int m_slot = -1;
    int m_ticks = 0;
    // Server ticks so far as the client counts them, and the tick from which each item may be thrown again.
    double m_clock = 0.0;
    QHash<QcItemId, double> m_throwReady;
};
