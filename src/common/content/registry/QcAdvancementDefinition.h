// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <optional>

// How an advancement is framed in the tree and its toast (decision E-923): an ordinary step, a goal
// that takes longer, or a challenge.
enum class QcAdvancementFrame : quint8 { Task, Goal, Challenge };

// The game events an advancement criterion waits for (decision E-923). The server's gameplay
// reports them (QcServerAdvancements::trigger); a mod picks one by name and narrows it with
// conditions, but cannot add new kinds of events.
enum class QcAdvancementTrigger : quint8 {
    // Never fires; only /advancement grant gives the criterion.
    Impossible,
    // The inventory holds `count` items matching `item` (checked whenever the inventory changes).
    ItemObtained,
    BlockPlaced,
    BlockBroken,
    ItemCrafted,
    // An item taken out of a furnace-like machine.
    ItemSmelted,
    EntityKilled,
    EntityBred,
    EntityTamed,
    // The player stands in a biome, a structure or a height range (checked about once a second while
    // the player moves).
    Location,
    ItemUsedOnBlock,
    ItemUsedOnEntity,
    RideVehicle,
    VillagerTrade,
    ItemEnchanted,
    EffectGained,
    LevelReached,
};

// What an event must show for a criterion to be met; empty fields accept anything. `item` and
// `block` are content names or, with a leading '#', item tags (blocks are matched by their item).
struct QcAdvancementConditions
{
    QString item = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    QString block = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    // A creature kind (mob name).
    QString entity = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    QString biome = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    // "village": inside the area of a village.
    QString structure = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    // "minecart" or "boat".
    QString vehicle = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    QString effect = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    std::optional<int> minY = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    std::optional<int> maxY = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    // Items the inventory must hold (ItemObtained).
    int count = 1;
    // The experience level reached at least (LevelReached).
    int level = 0;
};

// One criterion of an advancement: its name inside the advancement, the event and its conditions.
struct QcAdvancementCriterion
{
    QString name;
    QcAdvancementTrigger trigger = QcAdvancementTrigger::Impossible;
    QcAdvancementConditions conditions;
};

// An advancement of the adventure guide (advancements/<name>.json, decision E-923): its place in the
// tree (`parent`, empty for a root), how it shows (icon item, title and description keys, frame,
// hidden until reached, announced in the chat), the handbook entry it leads to ("chapter:<name>", "item:<name>",
// "block:<name>", "mob:<name>", "biome:<name>"), its criteria and which of them it needs:
// `requirements` holds groups, every group must have one met criterion. A mod replaces an
// advancement by name or switches it off with `"disabled": true`. The content sync sends everything
// but the triggers and conditions of the criteria (the client never matches events).
struct QcAdvancementDefinition
{
    static constexpr int maxCriteria = 32;
    static constexpr int maxNameLength = 128;

    QString name;
    QString parent;
    QString icon;
    QString titleKey;
    QString descriptionKey;
    QString handbook;
    QcAdvancementFrame frame = QcAdvancementFrame::Task;
    bool hidden = false;
    // Whether the other players read in the chat that someone reached it (decision E-924; the
    // server's game rule announceAdvancements must allow it too). Only the server uses it.
    bool announce = true;
    bool disabled = false;
    QList<QcAdvancementCriterion> criteria;
    QList<QStringList> requirements;

    // Whether the met criteria `done` fulfil every requirement group.
    [[nodiscard]] bool isDone(const QSet<QString> &done) const
    {
        for (const QStringList &group : requirements) {
            const bool met = std::ranges::any_of(group, [&done](const QString &criterion) { return done.contains(criterion); });
            if (!met) {
                return false;
            }
        }
        return !requirements.isEmpty();
    }

    [[nodiscard]] const QcAdvancementCriterion *criterion(const QString &criterionName) const
    {
        for (const QcAdvancementCriterion &entry : criteria) {
            if (entry.name == criterionName) {
                return &entry;
            }
        }
        return nullptr;
    }
};
