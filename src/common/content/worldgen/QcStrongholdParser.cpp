// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcStrongholdParser.h"

#include "common/content/registry/QcJsonFieldReader.h"
#include "common/core/QcWorldConstants.h"

#include <array>
#include <tuple>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

using Settings = QcStrongholdSettings;
// A key of the settings with the field it fills.
using NameField = std::pair<const char16_t *, QString Settings::*>;

// The names of "blocks".
constexpr std::array<NameField, 23> blockNames{{
    {u"bricks", &Settings::bricks},
    {u"cracked", &Settings::cracked},
    {u"mossy", &Settings::mossy},
    {u"chiseled", &Settings::chiseled},
    {u"infested", &Settings::infested},
    {u"stairs", &Settings::stairs},
    {u"slab", &Settings::slab},
    {u"woodDoor", &Settings::woodDoor},
    {u"ironDoor", &Settings::ironDoor},
    {u"bars", &Settings::bars},
    {u"button", &Settings::button},
    {u"bookshelf", &Settings::bookshelf},
    {u"planks", &Settings::planks},
    {u"fence", &Settings::fence},
    {u"ladder", &Settings::ladder},
    {u"cobweb", &Settings::cobweb},
    {u"torch", &Settings::torch},
    {u"water", &Settings::water},
    {u"lava", &Settings::lava},
    {u"chest", &Settings::chest},
    {u"spawner", &Settings::spawner},
    {u"portalFrame", &Settings::portalFrame},
    {u"mob", &Settings::mob},
}};

// The names of the loot tables.
constexpr std::array<NameField, 3> lootNames{{
    {u"corridorLoot", &Settings::corridorLoot},
    {u"storeLoot", &Settings::storeLoot},
    {u"libraryLoot", &Settings::libraryLoot},
}};

// The settings with each name of `names` read through `reader`, and whether none was missing.
template <std::size_t count>
std::pair<Settings, bool> withNames(Settings settings, QcJsonFieldReader &reader, const std::array<NameField, count> &names, const QString &packNamespace)
{
    bool complete = true;
    for (const auto &[key, field] : names) {
        settings.*field = reader.name(QString::fromUtf16(key), packNamespace);
        complete = complete && !(settings.*field).isEmpty();
    }
    return {std::move(settings), complete};
}

} // namespace

QcStrongholdSettings QcStrongholdParser::parseStrongholds(QcJsonFieldReader &reader, const QString &packNamespace)
{
    Settings settings;
    settings.enabled = reader.boolean(u"enabled"_s, true);
    settings.lowestFloor = reader.integer(u"lowestFloor"_s, settings.lowestFloor, 2, QcWorld::worldHeight - 32);
    settings.minStartFloor = reader.integer(u"minStartFloor"_s, settings.minStartFloor, settings.lowestFloor, QcWorld::worldHeight - 32);
    settings.maxStartFloor = reader.integer(u"maxStartFloor"_s, settings.maxStartFloor, settings.minStartFloor, QcWorld::worldHeight - 32);
    settings.groundCover = reader.integer(u"groundCover"_s, settings.groundCover, 1, 32);
    settings.maxPieces = reader.integer(u"maxPieces"_s, settings.maxPieces, 2, 200);
    settings.reach = reader.integer(u"reach"_s, settings.reach, 32, 160);
    settings.eyeChance = reader.number(u"eyeChance"_s, settings.eyeChance, 0.0, 1.0);
    settings.crackedShare = reader.number(u"crackedShare"_s, settings.crackedShare, 0.0, 1.0);
    settings.mossyShare = reader.number(u"mossyShare"_s, settings.mossyShare, 0.0, 1.0);
    settings.infestedShare = reader.number(u"infestedShare"_s, settings.infestedShare, 0.0, 1.0);

    bool blocksComplete = false;
    reader.nestedObject(u"blocks"_s, [&settings, &blocksComplete, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        std::tie(settings, blocksComplete) = withNames(settings, nested, blockNames, packNamespace);
    });
    bool lootComplete = false;
    std::tie(settings, lootComplete) = withNames(settings, reader, lootNames, packNamespace);
    if (reader.ok() && settings.crackedShare + settings.mossyShare + settings.infestedShare > 1.0) {
        reader.fail(u"'crackedShare', 'mossyShare' and 'infestedShare' add up to at most 1"_s);
    }
    if (reader.ok() && (!blocksComplete || !lootComplete)) {
        reader.fail(u"needs every block of 'blocks' and the loot tables 'corridorLoot', 'storeLoot' and 'libraryLoot'"_s);
    }
    return settings;
}
