#pragma once

#include "QcCommandRegistry.h"
#include "QcVillage.h"

#include <memory>

class QcServerState;
struct QcVillagePlan;

// Commands about villages (decision E-403): /locate village names the nearest village with its
// style and street plan (decision E-409) for operators, /village answers the state of the nearest village as one line of JSON (tests, bots
// through the control interface's console request, debugging).
class QcVillageCommands
{
public:
    // How far both commands look around their position (blocks).
    static constexpr int searchRadius = 2048;

    explicit QcVillageCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);

private:
    void locate(const QcCommandContext &context);
    void village(const QcCommandContext &context);
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
