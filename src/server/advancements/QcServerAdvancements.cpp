// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/advancements/QcServerAdvancements.h"

#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/advancements/QcAdvancementMatch.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerState.h"

namespace {

using namespace Qt::StringLiterals;

using Progress = QcMsg::AdvancementProgress;

// The chat line by the frame, as the toast words it; %1 is the player, %2 the advancement's title.
const char *announcementKey(QcAdvancementFrame frame)
{
    switch (frame) {
    case QcAdvancementFrame::Goal:
        return QT_TRANSLATE_NOOP("QcServerMessages", "%1 has reached the goal [%2]");
    case QcAdvancementFrame::Challenge:
        return QT_TRANSLATE_NOOP("QcServerMessages", "%1 has completed the challenge [%2]");
    case QcAdvancementFrame::Task:
        break;
    }
    return QT_TRANSLATE_NOOP("QcServerMessages", "%1 has made the advancement [%2]");
}

// Sends `line` to the playing clients other than `client`.
void sendToOthers(const QcServerState &state, const QcServerClient &client, const QcMsg::ChatMessage &line)
{
    for (const auto &other : state.clients()) {
        if (other.get() != &client && other->state == QcServerClient::State::Playing) {
            other->connection->send(line);
        }
    }
}

Progress::Entry entryOf(const QcAdvancementDefinition &advancement, const QcAdvancementProgress &progress, bool announce)
{
    QStringList criteria = progress.criteria(advancement.name).values();
    criteria.sort();
    return {advancement.name, criteria, progress.isDone(advancement), announce};
}

// Sends `entries` in messages of at most Progress::maxEntries; with `reset` the first one says so
// (and goes out even without entries).
void sendEntries(QcServerClient &client, const QList<Progress::Entry> &entries, bool reset)
{
    for (qsizetype start = 0; start < entries.size() || (reset && start == 0); start += Progress::maxEntries) {
        client.connection->send(Progress{reset && start == 0, entries.mid(start, Progress::maxEntries)});
    }
}

} // namespace

QcServerAdvancements::QcServerAdvancements(QcServerState &state)
    : m_state(state)
{
    const QList<QcAdvancementDefinition> &advancements = m_state.content().advancements().items();
    for (qsizetype a = 0; a < advancements.size(); ++a) {
        for (qsizetype c = 0; c < advancements[a].criteria.size(); ++c) {
            m_byTrigger[static_cast<std::size_t>(advancements[a].criteria[c].trigger)].append({a, c});
        }
    }
}

// The criteria of the event's trigger are compared one by one: the base tree has at most ten per
// trigger, so buckets by item or creature name would not pay for themselves yet.
void QcServerAdvancements::trigger(QcServerClient &client, const QcAdvancementEvent &event)
{
    const QList<QcAdvancementDefinition> &advancements = m_state.content().advancements().items();
    for (const Ref ref : refsOf(event.trigger)) {
        const QcAdvancementDefinition &advancement = advancements[ref.advancement];
        const QcAdvancementCriterion &criterion = advancement.criteria[ref.criterion];
        if (client.advancements.progress.has(advancement.name, criterion.name)) {
            continue;
        }
        ++m_comparisons;
        if (QcAdvancementMatch::matches(m_state.content(), criterion.conditions, event)) {
            meet(client, ref);
        }
    }
}

void QcServerAdvancements::inventoryChanged(QcServerClient &client)
{
    const QList<QcAdvancementDefinition> &advancements = m_state.content().advancements().items();
    for (const Ref ref : refsOf(QcAdvancementTrigger::ItemObtained)) {
        const QcAdvancementDefinition &advancement = advancements[ref.advancement];
        const QcAdvancementCriterion &criterion = advancement.criteria[ref.criterion];
        if (client.advancements.progress.has(advancement.name, criterion.name)) {
            continue;
        }
        ++m_comparisons;
        if (QcAdvancementMatch::holds(m_state.content(), criterion.conditions, client.inventory)) {
            meet(client, ref);
        }
    }
}

void QcServerAdvancements::meet(QcServerClient &client, Ref ref)
{
    const QcAdvancementDefinition &advancement = m_state.content().advancements().items()[ref.advancement];
    forgetUnknownCriteria(client, advancement);
    const bool wasDone = client.advancements.progress.isDone(advancement);
    if (client.advancements.progress.grant(advancement.name, advancement.criteria[ref.criterion].name)) {
        noteChange(client, advancement.name, wasDone);
    }
}

void QcServerAdvancements::forgetUnknownCriteria(QcServerClient &client, const QcAdvancementDefinition &advancement)
{
    const QSet<QString> met = client.advancements.progress.criteria(advancement.name);
    QSet<QString> known;
    for (const QString &criterion : met) {
        if (advancement.criterion(criterion)) {
            known.insert(criterion);
        }
    }
    if (known.size() != met.size()) {
        client.advancements.progress.setCriteria(advancement.name, known);
    }
}

void QcServerAdvancements::noteChange(QcServerClient &client, const QString &advancement, bool wasDone)
{
    const QcAdvancementDefinition *definition = m_state.content().advancements().find(advancement);
    const bool announce = !wasDone && definition && client.advancements.progress.isDone(*definition);
    client.advancements.unsent[advancement] = client.advancements.unsent.value(advancement) || announce;
    if (announce) {
        tellOthers(client, *definition);
    }
}

void QcServerAdvancements::tellOthers(QcServerClient &client, const QcAdvancementDefinition &advancement) const
{
    if (!advancement.announce || !m_state.info().announceAdvancements) {
        return;
    }
    if (client.advancements.announced >= announcementsPerTick) {
        ++client.advancements.folded;
        return;
    }

    // The title travels as its content key: every client shows it in its own content language.
    ++client.advancements.announced;
    const QString title = m_state.content().translations().translate(advancement.titleKey, u"en"_s);
    sendToOthers(m_state, client, QcChatText::message(announcementKey(advancement.frame), {client.name, title}, {QString(), advancement.titleKey}));
}

void QcServerAdvancements::endAnnouncements(QcServerClient &client) const
{
    if (client.advancements.folded > 0) {
        sendToOthers(m_state, client, QcChatText::counted(QT_TRANSLATE_N_NOOP("QcServerMessages", "%1 has made %n more advancements"),
                                                 client.advancements.folded, {client.name}));
    }
    client.advancements.announced = 0;
    client.advancements.folded = 0;
}

bool QcServerAdvancements::grant(QcServerClient &client, const QString &advancement)
{
    const QcAdvancementDefinition *definition = m_state.content().advancements().find(advancement);
    if (!definition) {
        return false;
    }
    forgetUnknownCriteria(client, *definition);
    const bool wasDone = client.advancements.progress.isDone(*definition);
    bool changed = false;
    for (const QcAdvancementCriterion &criterion : definition->criteria) {
        changed = client.advancements.progress.grant(advancement, criterion.name) || changed;
    }
    if (changed) {
        noteChange(client, advancement, wasDone);
    }
    return changed;
}

bool QcServerAdvancements::revoke(QcServerClient &client, const QString &advancement)
{
    if (client.advancements.progress.criteria(advancement).isEmpty()) {
        return false;
    }
    client.advancements.progress.setCriteria(advancement, {});
    noteChange(client, advancement, true);
    return true;
}

bool QcServerAdvancements::waitsFor(const QcServerClient &client, QcAdvancementTrigger trigger) const
{
    const QList<QcAdvancementDefinition> &advancements = m_state.content().advancements().items();
    const QList<Ref> &refs = refsOf(trigger);
    return std::ranges::any_of(refs, [&](Ref ref) {
        const QcAdvancementDefinition &advancement = advancements[ref.advancement];
        return !client.advancements.progress.has(advancement.name, advancement.criteria[ref.criterion].name);
    });
}

void QcServerAdvancements::checkLocation(QcServerClient &client)
{
    const QcBlockPos pos = QcPhysics::blockContaining(client.motion.position);
    if (client.advancements.checkedAt == pos || !waitsFor(client, QcAdvancementTrigger::Location)) {
        client.advancements.checkedAt = pos;
        return;
    }
    client.advancements.checkedAt = pos;
    ++m_locationChecks;

    // The biome and the village are looked up only when a criterion left asks for them.
    const QList<QcAdvancementDefinition> &advancements = m_state.content().advancements().items();
    const QList<Ref> &refs = refsOf(QcAdvancementTrigger::Location);
    const auto asks = [&](auto field) {
        return std::ranges::any_of(refs, [&](Ref ref) { return !field(advancements[ref.advancement].criteria[ref.criterion].conditions).isEmpty(); });
    };
    QcAdvancementMatch::Place place{pos.y, {}, false};
    if (asks([](const QcAdvancementConditions &c) { return c.biome; })) {
        place.biome = m_state.world().generator().biomeAt(pos.x, pos.z);
    }
    if (asks([](const QcAdvancementConditions &c) { return c.structure; })) {
        place.inVillage = m_state.world().villages().at(pos) != nullptr;
    }

    for (const Ref ref : refs) {
        const QcAdvancementDefinition &advancement = advancements[ref.advancement];
        const QcAdvancementCriterion &criterion = advancement.criteria[ref.criterion];
        if (!client.advancements.progress.has(advancement.name, criterion.name) && QcAdvancementMatch::matchesPlace(criterion.conditions, place)) {
            meet(client, ref);
        }
    }
}

void QcServerAdvancements::tick()
{
    const quint64 tick = m_state.clock().tick();
    for (const auto &client : m_state.clients()) {
        endAnnouncements(*client);
        if (client->state != QcServerClient::State::Playing) {
            continue;
        }
        if (client->hasBody() && (tick + client->id) % locationInterval == 0) {
            checkLocation(*client);
        }
        sendChanges(*client);
    }
}

void QcServerAdvancements::sendAll(QcServerClient &client) const
{
    QList<Progress::Entry> entries;
    for (const QcAdvancementDefinition &advancement : m_state.content().advancements().items()) {
        if (!client.advancements.progress.criteria(advancement.name).isEmpty()) {
            entries.append(entryOf(advancement, client.advancements.progress, false));
        }
    }
    sendEntries(client, entries, true);
}

void QcServerAdvancements::sendChanges(QcServerClient &client) const
{
    QcServerClient::Advancements &state = client.advancements;
    if (!state.synced) {
        sendAll(client);
        state.synced = true;
        state.unsent.clear();
        return;
    }
    if (state.unsent.isEmpty()) {
        return;
    }

    QList<Progress::Entry> entries;
    for (const QcAdvancementDefinition &advancement : m_state.content().advancements().items()) {
        if (const auto it = state.unsent.constFind(advancement.name); it != state.unsent.constEnd()) {
            entries.append(entryOf(advancement, state.progress, *it));
        }
    }
    state.unsent.clear();
    sendEntries(client, entries, false);
}
