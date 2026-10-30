// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcGameMode.h"

#include <QDataStream>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

struct QcBlockDefinition;
struct QcItemDefinition;

/*! \brief The allow-lists of adventure mode (decision E-735), a setting of the world: for each item
    kind, the blocks it may break and the blocks it may act on (be placed on, till, light, pour, …),
    like the reference's can_break and can_place_on, but per item kind instead of per stack.

    The kind \c {anyItem} ("*") stands for every item and the empty hand. Saved in world.json
    ("adventure"), changed with /adventure and sent to adventure players with their mode
    (QcMsg::GameMode), so server and client decide with the same lists (permits). Names are kept
    as written: a block or item of a missing mod stays in the list and matches nothing. */
class QcAdventureRules
{
public:
    /*! What an allow-list permits. */
    enum class Action : quint8 {
        /*! Mining a block (also putting out fire, which is mined like a block). */
        Break,
        /*! Using the held item on a block that does not react to the use itself: placing a
            block, tilling, flattening, carving, fertilizing, lighting fire, pouring or scooping
            a liquid, placing an armour stand or a minecart. */
        PlaceOn,
    };
    /*! The number of actions. */
    static constexpr int actionCount = 2;

    /*! The item kind that stands for every item and the empty hand. */
    static constexpr auto anyItem = "*";
    /*! Item kinds per action and blocks per item kind; names longer than maxNameLength are
        refused. \qcshared */
    static constexpr int maxItems = 256;
    static constexpr int maxBlocks = 256;
    static constexpr int maxNameLength = 128;
    /*! All names together (item kinds and blocks of both actions): keeps the encoded lists at
        about 1 MiB (4 + 2 x 128 bytes a name), far below a protocol frame
        (QcProtocol::maxFrameBytes). */
    static constexpr int maxNames = 4096;

    // What fromJson() read and what it left out, one line per dropped key with the reason.
    struct Read;

    /*! Returns whether \a name can stand in a list: not empty and at most maxNameLength characters
        (any name of content or "*"). */
    [[nodiscard]] static bool isValidName(const QString &name);

    /*! Returns whether a player in \a mode may do \a action with \a held (none: empty hand) to
        \a block. True in every mode without allow-lists (QcGameModes::Rules::buildByAllowList);
        in adventure only when the held kind or anyItem lists the block. Filling a bottle changes
        no block and is always allowed. */
    [[nodiscard]] bool permits(QcGameMode mode, Action action, const QcItemDefinition *held, const QcBlockDefinition &block) const;

    /*! Returns the blocks listed for the item kind \a item (or anyItem) and \a action, sorted. */
    [[nodiscard]] QStringList blocks(Action action, const QString &item) const;
    /*! Returns the item kinds that have a list for \a action, sorted. */
    [[nodiscard]] QStringList items(Action action) const;
    /*! Returns whether no action has a list. */
    [[nodiscard]] bool isEmpty() const;
    /*! Item kinds and blocks of both actions, counted against maxNames. */
    [[nodiscard]] int nameCount() const;

    /*! Adds \a blocks to the list of \a item for \a action. Returns false (nothing changed) when a
        name is not valid or a limit would be passed. */
    [[nodiscard]] bool allow(Action action, const QString &item, const QStringList &blocks);
    /*! Takes \a blocks off the list of \a item for \a action, all of them when \a blocks is empty.
        Returns false when the item had none of them. */
    [[nodiscard]] bool remove(Action action, const QString &item, const QStringList &blocks);
    /*! Empties every list. */
    void clear();
    /*! Adds every entry of \a other; an item's list that would pass a limit is left out. */
    void merge(const QcAdventureRules &other);

    /*! Returns the name of \a action: "canBreak" and "canPlaceOn", the keys of world.json and the
        words of /adventure. */
    static QLatin1StringView name(Action action);
    /*! Returns the action named \a text (case-insensitive); std::nullopt for another word. */
    [[nodiscard]] static std::optional<Action> parseAction(QStringView text);

    /*! Returns the lists as world.json "adventure": {"canBreak": {item: [blocks]}, "canPlaceOn":
        {…}} (also in structure files). */
    [[nodiscard]] QJsonObject toJson() const;
    /*! Returns the lists of \a json in the form of toJson(), with what was left out (Read): an
        action that is no object, and an item whose list is no list of block names or passes a
        limit. */
    [[nodiscard]] static Read fromJson(const QJsonObject &json);
    /*! Writes the lists to \a stream in the wire format of QcMsg::GameMode. */
    void write(QDataStream &stream) const;
    /*! Returns the lists read from \a stream in the format of write(); std::nullopt for counts or
        names beyond the limits. */
    [[nodiscard]] static std::optional<QcAdventureRules> read(QDataStream &stream);

    bool operator==(const QcAdventureRules &) const = default;

private:
    using Lists = QMap<QString, QStringList>;

    const Lists &lists(Action action) const { return m_lists[static_cast<std::size_t>(action)]; }
    Lists &lists(Action action) { return m_lists[static_cast<std::size_t>(action)]; }

    std::array<Lists, actionCount> m_lists;
};

/*! \brief What QcAdventureRules::fromJson() read and what it left out.
    \struct QcAdventureRules::Read
    \inmodule qc_common
    \inheaderfile common/gameplay/QcAdventureRules.h */
struct QcAdventureRules::Read
{
    /*! The lists read.
        \variable QcAdventureRules::Read::rules */
    QcAdventureRules rules;
    /*! One line per dropped entry with the reason.
        \variable QcAdventureRules::Read::problems */
    QStringList problems;
};
