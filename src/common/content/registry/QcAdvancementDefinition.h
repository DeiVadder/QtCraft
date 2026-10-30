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

/*! \brief How an advancement is framed in the tree and its toast (decision E-923): an ordinary
    step, a goal that takes longer, or a challenge. */
enum class QcAdvancementFrame : quint8 {
    Task, // An ordinary step ("task", the default).
    Goal, // A step that takes longer ("goal").
    Challenge // A challenge ("challenge").
};

/*! \brief The game events an advancement criterion waits for (decision E-923).

    The server's gameplay reports them (QcServerAdvancements::trigger); a mod picks one by name
    and narrows it with conditions, but cannot add new kinds of events. */
enum class QcAdvancementTrigger : quint8 {
    /*! Never fires; only /advancement grant gives the criterion. */
    Impossible,
    /*! The inventory holds \c {count} items matching \c {item} (checked whenever the inventory
        changes). */
    ItemObtained,
    BlockPlaced, // The player placed a block matching \c {block}.
    BlockBroken, // The player broke a block matching \c {block}.
    ItemCrafted, // The player crafted an item matching \c {item}.
    /*! An item taken out of a furnace-like machine. */
    ItemSmelted,
    EntityKilled, // The player killed a creature of kind \c {entity}.
    EntityBred, // Two animals bred by the player got a young of kind \c {entity}.
    EntityTamed, // The player tamed a creature of kind \c {entity}.
    /*! The player stands in a biome, a structure or a height range (checked about once a second
        while the player moves). */
    Location,
    ItemUsedOnBlock, // The player used an item matching \c {item} on a block matching \c {block}.
    ItemUsedOnEntity, // The player used an item matching \c {item} on a creature of kind \c {entity}.
    RideVehicle, // The player got into a \c {vehicle} ("minecart" or "boat").
    VillagerTrade, // The player bought an item matching \c {item} from a villager.
    ItemEnchanted, // The player enchanted an item matching \c {item}.
    EffectGained, // The player got the status effect \c {effect}.
    LevelReached, // The player reached at least experience level \c {level}.
};

/*! \brief What an event must show for a criterion to be met; empty fields accept anything.

    \c {item} and \c {block} are content names or, with a leading '#', item tags (blocks are
    matched by their item). */
struct QcAdvancementConditions
{
    /*! An item name or '#' item tag (JSON \c {"item"}). */
    QString item = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! A block name or '#' item tag (JSON \c {"block"}). */
    QString block = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! A creature kind (mob name). */
    QString entity = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! A biome name (Location). */
    QString biome = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! "village": inside the area of a village. */
    QString structure = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! "minecart" or "boat". */
    QString vehicle = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! A status effect name (EffectGained). */
    QString effect = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! The lowest block height the player may stand at (Location); none for no limit. */
    std::optional<int> minY = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! The highest block height the player may stand at (Location); none for no limit. */
    std::optional<int> maxY = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! Items the inventory must hold (ItemObtained). */
    int count = 1;
    /*! The experience level reached at least (LevelReached). */
    int level = 0;
};

/*! \brief One criterion of an advancement: its name inside the advancement, the event and its
    conditions. */
struct QcAdvancementCriterion
{
    /*! The key of the criterion in \c {"criteria"}, used in \c {requirements}. */
    QString name;
    /*! The event it waits for (JSON \c {"trigger"}). */
    QcAdvancementTrigger trigger = QcAdvancementTrigger::Impossible;
    /*! What the event must show (JSON \c {"conditions"}). */
    QcAdvancementConditions conditions;
};

/*! \brief An advancement of the adventure guide (advancements/<name>.json, decision E-923): its
    place in the tree (\c {parent}, empty for a root), how it shows (icon item, title and
    description keys, frame, hidden until reached, announced in the chat), the handbook entry it
    leads to ("chapter:<name>", "item:<name>", "block:<name>", "mob:<name>", "biome:<name>"), its
    criteria and which of them it needs: \c {requirements} holds groups, every group must have one
    met criterion.

    A mod replaces an advancement by name or switches it off with \c {"disabled": true}. The
    content sync sends everything but the triggers and conditions of the criteria (the client
    never matches events). */
struct QcAdvancementDefinition
{
    /*! The most criteria, and requirement groups, an advancement may have. */
    static constexpr int maxCriteria = 32;
    /*! The longest qualified name of an advancement, its parent and its icon. */
    static constexpr int maxNameLength = 128;

    /*! The qualified name (JSON \c {"name"}). */
    QString name;
    /*! The qualified name of the parent advancement; empty for a root. */
    QString parent;
    /*! The qualified name of the item shown as its icon. */
    QString icon;
    /*! The translation key of the title (JSON \c {"title"}, by default
        QcContentFields::translationKey() with the prefix \c {advancement}). */
    QString titleKey;
    /*! The translation key of the description (JSON \c {"description"}, by default the title key
        with \c {.description} appended). */
    QString descriptionKey;
    /*! The handbook entry it leads to, \c {<kind>:<qualified name>}; empty for none. */
    QString handbook;
    /*! How it is framed (JSON \c {"frame"}). */
    QcAdvancementFrame frame = QcAdvancementFrame::Task;
    /*! Whether it stays hidden until reached (JSON \c {"hidden"}). */
    bool hidden = false;
    /*! Whether the other players read in the chat that someone reached it (decision E-924; the
        server's game rule announceAdvancements must allow it too). Only the server uses it. */
    bool announce = true;
    /*! Whether a pack switches it off (JSON \c {"disabled"}); QcAdvancementCatalog::resolve()
        removes it. */
    bool disabled = false;
    /*! Its criteria in the order of the JSON object. */
    QList<QcAdvancementCriterion> criteria;
    /*! Groups of criterion names, each met by one of its criteria (JSON \c {"requirements"}:
        \c {"all"}, the default, gives one group per criterion, \c {"any"} a single group). */
    QList<QStringList> requirements;

    /*! Returns whether the met criteria \a done fulfil every requirement group. */
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

    /*! Returns the criterion named \a criterionName, or null if it has none. */
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
