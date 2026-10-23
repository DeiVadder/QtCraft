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

// The allow-lists of adventure mode (decision E-735), a setting of the world: for each item kind,
// the blocks it may break and the blocks it may act on (be placed on, till, light, pour, …), like
// the reference's can_break and can_place_on, but per item kind instead of per stack. The kind
// `anyItem` ("*") stands for every item and the empty hand. Saved in world.json ("adventure"),
// changed with /adventure and sent to adventure players with their mode (QcMsg::GameMode), so
// server and client decide with the same lists (permits). Names are kept as written: a block or
// item of a missing mod stays in the list and matches nothing.
class QcAdventureRules
{
public:
    enum class Action : quint8 {
        // Mining a block (also putting out fire, which is mined like a block).
        Break,
        // Using the held item on a block that does not react to the use itself: placing a block,
        // tilling, flattening, carving, fertilizing, lighting fire, pouring or scooping a liquid,
        // placing an armour stand or a minecart.
        PlaceOn,
    };
    static constexpr int actionCount = 2;

    static constexpr auto anyItem = "*";
    // Item kinds per action and blocks per item kind; names longer than maxNameLength are refused.
    static constexpr int maxItems = 256;
    static constexpr int maxBlocks = 256;
    static constexpr int maxNameLength = 128;
    // All names together (item kinds and blocks of both actions): keeps the encoded lists at about
    // 1 MiB (4 + 2 x 128 bytes a name), far below a protocol frame (QcProtocol::maxFrameBytes).
    static constexpr int maxNames = 4096;

    // What fromJson() read and what it left out, one line per dropped key with the reason.
    struct Read;

    // Not empty and at most maxNameLength characters (any name of content or "*").
    [[nodiscard]] static bool isValidName(const QString &name);

    // Whether a player in `mode` may do `action` with `held` (none: empty hand) to `block`. True in
    // every mode without allow-lists (QcGameModes::Rules::buildByAllowList); in adventure only
    // when the held kind or anyItem lists the block. Filling a bottle changes no block and is
    // always allowed.
    [[nodiscard]] bool permits(QcGameMode mode, Action action, const QcItemDefinition *held, const QcBlockDefinition &block) const;

    // The blocks listed for the item kind (or anyItem) and action, sorted.
    [[nodiscard]] QStringList blocks(Action action, const QString &item) const;
    // The item kinds that have a list for the action, sorted.
    [[nodiscard]] QStringList items(Action action) const;
    [[nodiscard]] bool isEmpty() const;
    // Item kinds and blocks of both actions, counted against maxNames.
    [[nodiscard]] int nameCount() const;

    // Adds blocks to the item's list; false (nothing changed) when a limit would be passed.
    [[nodiscard]] bool allow(Action action, const QString &item, const QStringList &blocks);
    // Takes blocks off the item's list, all of them when `blocks` is empty; false when the item had
    // none of them.
    [[nodiscard]] bool remove(Action action, const QString &item, const QStringList &blocks);
    void clear();
    // Adds every entry of `other`; an item's list that would pass a limit is left out.
    void merge(const QcAdventureRules &other);

    // "canBreak" and "canPlaceOn", the keys of world.json and the words of /adventure.
    static QLatin1StringView name(Action action);
    [[nodiscard]] static std::optional<Action> parseAction(QStringView text);

    // world.json "adventure": {"canBreak": {item: [blocks]}, "canPlaceOn": {…}} (also in structure
    // files); entries beyond the limits and names that are no strings are left out.
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Read fromJson(const QJsonObject &json);
    // The wire format of QcMsg::GameMode; read() gives nullopt for counts or names beyond the limits.
    void write(QDataStream &stream) const;
    [[nodiscard]] static std::optional<QcAdventureRules> read(QDataStream &stream);

    bool operator==(const QcAdventureRules &) const = default;

private:
    using Lists = QMap<QString, QStringList>;

    const Lists &lists(Action action) const { return m_lists[static_cast<std::size_t>(action)]; }
    Lists &lists(Action action) { return m_lists[static_cast<std::size_t>(action)]; }

    std::array<Lists, actionCount> m_lists;
};

struct QcAdventureRules::Read
{
    QcAdventureRules rules;
    QStringList problems;
};
