#pragma once

#include "QcCommandRegistry.h"
#include "QcGameMode.h"
#include "QcVec3.h"
#include "QcStandCommands.h"

#include <QString>

#include <functional>
#include <optional>

class QcPlayerHealth;
class QcServerGameModes;
class QcServerInventory;
class QcServerStands;
struct QcMobDefinition;
struct QcServerClient;
class QcServerState;

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
        QcPlayerHealth &health;
        QcServerStands &stands;
        QcServerGameModes &gameModes;
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
    // Where a creature summoned for `wanted` appears.
    QcVec3 summonPlace(const QcServerClient &player, const QcMobDefinition &definition, const QcVec3 &wanted) const;
    void kill(const QcCommandContext &context);
    void feed(const QcCommandContext &context);
    void heal(const QcCommandContext &context);
    void time(const QcCommandContext &context) const;
    void gamerule(const QcCommandContext &context);
    void spawnChunkRadius(const QcCommandContext &context);
    void gamemode(const QcCommandContext &context);
    void setGamemode(const QcCommandContext &context, QcServerClient &target, QcGameMode mode);
    void defaultGamemode(const QcCommandContext &context);
    void locateBiome(const QcCommandContext &context);
    // Offset along z of the first column of `name` on the line at `dx` of the square ring `radius`.
    [[nodiscard]] std::optional<int> findBiomeAlong(QcBlockPos feet, int radius, int dx, const QString &name) const;

    QcServerState &m_state;
    Parts m_parts;
    QcStandCommands m_standCommands;
};
