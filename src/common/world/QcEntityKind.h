#pragma once

#include <QLatin1StringView>
#include <QtGlobal>

#include <optional>

// What an entity is (decision E-364): the server keeps every entity in the table of its kind, and
// the protocol names the kind of each entity it sends. The values are part of the protocol.
enum class QcEntityKind : quint8 {
    Player,
    // A creature of the content's mobs (EntitySpawn::creatureType names which).
    Creature,
    // A dropped item stack.
    Item,
    // A block falling while nothing holds it (sand, gravel).
    FallingBlock,
    ArmourStand,
    // A block sliding where a piston moves it (decision E-337), shown only.
    MovingBlock,
};

namespace QcEntityKinds {

inline constexpr int count = 6;

// The kind with this protocol value; nothing for values no kind has.
[[nodiscard]] std::optional<QcEntityKind> fromValue(quint8 value);
// A short English name for logs and dumps ("player", "creature", ...).
QLatin1StringView name(QcEntityKind kind);

} // namespace QcEntityKinds
