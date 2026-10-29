#pragma once

#include "server/advancements/QcAdvancementEvent.h"

#include <QList>
#include <QString>

#include <array>

struct QcServerClient;
class QcServerState;

// The adventure guide on the server (decision E-923): tracks per player which criteria of the
// content's advancements are met (QcServerClient::advancements) and tells the client.
//
// The gameplay reports events through trigger() at the places they happen (crafting, mining,
// combat, trading …); only the criteria of that trigger are looked at, so an event costs a few
// comparisons and nothing scans the world. The inventory criteria are checked when the inventory is
// sent (inventoryChanged), the location criteria in tick() for each player once per
// locationInterval ticks and only after he moved to another block. tick() also sends the clients
// what changed: the whole progress once after joining, then deltas. A newly reached advancement is
// announced to the other players in the chat (decision E-924) when the advancement and the game
// rule announceAdvancements allow it, at most announcementsPerTick lines per player and tick; the
// rest of that tick (/advancement grant … everything) becomes one line "… has made N more".
class QcServerAdvancements
{
public:
    static constexpr quint64 locationInterval = 20;
    static constexpr int announcementsPerTick = 5;

    explicit QcServerAdvancements(QcServerState &state);

    void trigger(QcServerClient &client, const QcAdvancementEvent &event);
    // The inventory of `client` changed (QcServerInventory::send).
    void inventoryChanged(QcServerClient &client);
    // Location criteria of the players due this tick, then the changes to the clients.
    void tick();

    // Meets every criterion of `advancement` (grant) or none (revoke); whether anything changed.
    // `advancement` must be a name of the content.
    bool grant(QcServerClient &client, const QString &advancement);
    bool revoke(QcServerClient &client, const QString &advancement);
    // Sends the whole progress to `client` now (also done by tick() after joining).
    void sendAll(QcServerClient &client) const;

    // Places looked at by the location criteria since the start (tests check the cost per tick).
    quint64 locationChecks() const { return m_locationChecks; }
    // Criteria compared with an event or the inventory since the start.
    quint64 comparisons() const { return m_comparisons; }

private:
    struct Ref
    {
        qsizetype advancement = 0;
        qsizetype criterion = 0;
    };
    static constexpr std::size_t triggerCount = static_cast<std::size_t>(QcAdvancementTrigger::LevelReached) + 1;

    // Meets the criterion `ref` for `client` and notes the change for the client.
    void meet(QcServerClient &client, Ref ref);
    void noteChange(QcServerClient &client, const QString &advancement, bool wasDone);
    // Tells the other players in the chat that `client` reached `advancement` (decision E-924), or
    // counts it for the folded line once this tick's lines are used up.
    void tellOthers(QcServerClient &client, const QcAdvancementDefinition &advancement) const;
    // Sends the folded line of `client`'s tick, if any, and starts the count of the next tick.
    void endAnnouncements(QcServerClient &client) const;
    // Drops the met criteria of `advancement` it no longer has (a mod changed it) before more are
    // met, so they never fill its room of QcAdvancementDefinition::maxCriteria.
    static void forgetUnknownCriteria(QcServerClient &client, const QcAdvancementDefinition &advancement);
    void checkLocation(QcServerClient &client);
    void sendChanges(QcServerClient &client) const;
    // Whether `client` has a criterion of `trigger` left.
    bool waitsFor(const QcServerClient &client, QcAdvancementTrigger trigger) const;
    // The criteria of `trigger`.
    const QList<Ref> &refsOf(QcAdvancementTrigger trigger) const { return m_byTrigger[static_cast<std::size_t>(trigger)]; }

    QcServerState &m_state;
    std::array<QList<Ref>, triggerCount> m_byTrigger;
    quint64 m_locationChecks = 0;
    quint64 m_comparisons = 0;
};
