// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/commands/QcCommandRegistry.h"
#include "server/villages/QcVillage.h"

#include <array>
#include <memory>

class QcServerState;
struct QcVillagePlan;

// Commands about villages (decision E-403): /locate village names the nearest village with its
// style and street plan (decision E-409) for operators, /locate <structure> the nearest copy of a
// structure the generator places by itself (decision E-590) or the nearest mine shaft (with its
// way in), dungeon or coastal tower (decision E-911; /help locate names what the world has), /village answers the state of the nearest village as one line of JSON (tests, bots
// through the control interface's console request, debugging), /profession gives the nearest
// villager a profession and level (decision E-414; trading tests and bots, summoned villagers).
class QcVillageCommands
{
public:
    // How far both commands look around their position (blocks).
    static constexpr int searchRadius = 2048;
    // /profession looks for the villager this far from the player (metres).
    static constexpr double professionReach = 8.0;

    // A word of /locate that names a place of the generator rather than a structure, with the name
    // the answer shows (a server message key, translated by the client).
    struct PlaceKind
    {
        QStringView word;
        const char *name = nullptr;
    };
    static constexpr std::array<PlaceKind, 3> placeKinds{{{u"mineshaft", QT_TRANSLATE_NOOP("QcServerMessages", "Mine shaft")},
                                                          {u"dungeon", QT_TRANSLATE_NOOP("QcServerMessages", "Dungeon")},
                                                          {u"coastal_tower", QT_TRANSLATE_NOOP("QcServerMessages", "Coastal tower")}}};

    explicit QcVillageCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);

private:
    void locate(const QcCommandContext &context);
    // /locate <structure>: the nearest copy of a structure the generator places by itself.
    void locateStructure(const QcCommandContext &context, QcBlockPos origin);
    // /locate mineshaft|dungeon|coastal_tower (QcWorldGenerator::locate); false for other words.
    bool locatePlace(const QcCommandContext &context, const QString &kind, QcBlockPos origin);
    void village(const QcCommandContext &context);
    void profession(const QcCommandContext &context);
    // The village nearest to `from`: as the world keeps it once a chunk of it was loaded, else as
    // the generator plans it (`known` false), with its plan; std::nullopt without one within
    // searchRadius.
    struct Found
    {
        QcVillage village;
        bool known = false;
        std::shared_ptr<const QcVillagePlan> plan;
    };
    [[nodiscard]] std::optional<Found> nearest(QcBlockPos from) const;

    QcServerState &m_state;
};
