#pragma once

#include "server/commands/QcCommandRegistry.h"
#include "server/villages/QcVillage.h"

#include <memory>

class QcServerState;
struct QcVillagePlan;

// Commands about villages (decision E-403): /locate village names the nearest village with its
// style and street plan (decision E-409) for operators, /locate <structure> the nearest copy of a
// structure the generator places by itself (decision E-590), /village answers the state of the nearest village as one line of JSON (tests, bots
// through the control interface's console request, debugging), /profession gives the nearest
// villager a profession and level (decision E-414; trading tests and bots, summoned villagers).
class QcVillageCommands
{
public:
    // How far both commands look around their position (blocks).
    static constexpr int searchRadius = 2048;
    // /profession looks for the villager this far from the player (metres).
    static constexpr double professionReach = 8.0;

    explicit QcVillageCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);

private:
    void locate(const QcCommandContext &context);
    // /locate <structure>: the nearest copy of a structure the generator places by itself.
    void locateStructure(const QcCommandContext &context, QcBlockPos origin);
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
