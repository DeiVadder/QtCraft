#pragma once

#include "common/content/definitions/QcPackReference.h"
#include "common/core/QcChunkPos.h"
#include "common/gameplay/QcAdventureRules.h"
#include "common/gameplay/QcGameMode.h"
#include "common/world/QcPlacedChunkLoader.h"
#include "common/world/QcWeather.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

// Metadata of a saved world (world.json). A world names the format version of its files;
// worlds of older development versions are refused, not converted (decision E-356, QtCraft has not
// been released). What each format changed is recorded in docs/Entscheidungen.md.
struct QcWorldInfo
{
    static constexpr int formatVersion = 17;
    static constexpr int oldestFormatVersion = 16;
    static constexpr auto defaultGenerator = "terrain";

    QString name;
    qint64 seed = 0;
    QDateTime created;
    QDateTime lastPlayed;
    // Seconds since the world was created, drives the day/night cycle.
    double worldTime = 0.0;
    // Content packs (base game and mods) used when the world was last saved.
    QList<QcPackReference> packs;
    // Game rule: players keep their inventory when they die instead of dropping it.
    bool keepInventory = true;
    // Game rules: the time of day runs, creatures spawn by themselves.
    bool daylightCycle = true;
    bool mobSpawning = true;
    // Game rule: blocks get random ticks (growing plants, crops, soil; decision E-295).
    bool randomTicks = true;
    // Game rule: fire spreads to flammable blocks and burns them away (decision E-392); off, fires
    // only burn where they are until they go out.
    bool fireSpread = true;
    // Game rule: pistons that allow it are also powered through the block above them, noticed only
    // with a block update next to them (quasi-connectivity, decision E-338).
    bool quasiConnectivity = true;
    // Game rule: the weather changes by itself (decision E-395); off, it stays as it is until
    // /weather changes it.
    bool weatherCycle = true;
    // Game rule: creatures change blocks of the world (shadewalkers take and place blocks, decision
    // E-460); off, they leave every block as it is.
    bool mobGriefing = true;
    // Game rule: explosions break, light and blow away blocks (decision E-603); off, they still hurt
    // and push bodies but leave every block as it is, lit explosives included.
    bool explosionBlockDamage = true;
    // The weather and its course (world.json "weather"; a world without it begins with a clear spell).
    QcWeatherState weather;
    // World generator type (QcWorldGenerators), its options (the layers of a flat world) and whether
    // its one-time setup has run.
    QString generator = QString::fromLatin1(defaultGenerator);
    QString generatorOptions;
    bool setupDone = true;
    // Game mode of players who join the world for the first time (decision E-282), chosen when the
    // world is created; worlds without the key are survival worlds.
    QcGameMode defaultGameMode = QcGameMode::Survival;
    // A mode name this version does not know (from a newer game): the world plays as survival but
    // keeps the name when saved, until /defaultgamemode sets another mode.
    QString unknownGameMode;
    // What players in adventure mode may break and act on (decision E-735, world.json "adventure";
    // a world without the key allows nothing).
    QcAdventureRules adventure;
    // The world's simulation limits (decision E-349) as written in world.json ("limits"), only the
    // keys an operator set; a single-player world plays with them, a dedicated server ignores them.
    QJsonObject limits;
    // Game rule (decision E-377): the chunks around the spawn column within this many chunks minus
    // one stay loaded (0: none, 1: the spawn chunk, 2: 3 x 3 chunks …).
    int spawnChunkRadius = 0;
    static constexpr int maxSpawnChunkRadius = 16;
    // Game rule (decision E-401): creatures in one block cell beyond this many take cramming damage,
    // as in the reference (0: never).
    int maxEntityCramming = 24;
    static constexpr int maxEntityCrammingLimit = 4096;
    // Game rules (decision E-429): the game ticks a player or a creature stays invulnerable after a
    // hurt, 0 to maxInvulnerabilityTicks (0: every hit counts). The server's hurt window
    // (QcHurtWindow::defaultTicks, maxTicks) takes its limits from here.
    static constexpr int defaultInvulnerabilityTicks = 10;
    static constexpr int maxInvulnerabilityTicks = 200;
    int playerInvulnerabilityTicks = defaultInvulnerabilityTicks;
    int mobInvulnerabilityTicks = defaultInvulnerabilityTicks;
    // The persistent id the next saved creature gets (QcMobManager::nextUid, decision E-420); saved
    // with the chunks in one journal, so it lies beyond every id saved in them.
    quint64 nextCreatureId = 1;
    // The id the next new map gets (QcMapStore::nextId, decision E-799); saved with the maps in one
    // journal, so ids of maps whose files were kept aside or lost are not handed out again.
    quint32 nextMapId = 1;
    // Chunks kept loaded by /forceload (decision E-377), in the order they were added.
    QList<QcChunkPos> forcedChunks;
    // Every chunk loader block of the world in the order they were placed (decision E-377).
    QList<QcPlacedChunkLoader> chunkLoaders;

    // Takes the game rules of world.json ("gameRules"); a missing rule gets its default.
    void readGameRules(const QJsonObject &rules);
};
