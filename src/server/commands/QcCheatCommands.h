#pragma once

#include "QcCommandRegistry.h"
#include "QcStandCommands.h"

#include <QString>

#include <functional>
#include <optional>

class QcServerCombat;
class QcServerInventory;
class QcServerStands;
struct QcServerState;

// Commands that bend the rules for operators (decision E-243): items, creatures, health and food,
// armour and stands, time, game rules, the game mode and finding biomes.
class QcCheatCommands
{
public:
    // How far /locatebiome searches around the player (blocks).
    static constexpr int biomeSearchRadius = 4096;

    struct Parts
    {
        QcServerInventory &inventory;
        QcServerCombat &combat;
        QcServerStands &stands;
        std::function<void(double seconds)> setWorldTime;
    };

    QcCheatCommands(QcServerState &state, Parts parts);

    void addTo(QcCommandRegistry &registry);

private:
    void addItemCommands(QcCommandRegistry &registry);
    void addPlayerCommands(QcCommandRegistry &registry);
    void addWorldCommands(QcCommandRegistry &registry);

    void give(const QcCommandContext &context);
    void spawnItem(const QcCommandContext &context);
    void summon(const QcCommandContext &context);
    void kill(const QcCommandContext &context);
    void feed(const QcCommandContext &context);
    void heal(const QcCommandContext &context);
    void time(const QcCommandContext &context) const;
    void gamerule(const QcCommandContext &context);
    void gamemode(const QcCommandContext &context);
    void locateBiome(const QcCommandContext &context);
    // Offset along z of the first column of `name` on the line at `dx` of the square ring `radius`.
    std::optional<int> findBiomeAlong(QcBlockPos feet, int radius, int dx, const QString &name) const;

    QcServerState &m_state;
    Parts m_parts;
    QcStandCommands m_standCommands;
};
