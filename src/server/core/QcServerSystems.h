#pragma once

class QcBodyBurning;
class QcFireIgnition;
class QcLightningStrike;
class QcPlayerHealth;
class QcServerCircuits;
class QcServerExperience;
class QcServerExplosions;
class QcServerFire;
class QcServerInteraction;
class QcServerMinecarts;
class QcServerProjectiles;
class QcServerWeather;
class QcSnowfall;

// The gameplay systems of a server: each runs its own part of the tick on the world and its bodies
// (fire, weather, health, minecarts, experience, projectiles, circuits, explosions, the players'
// mining and using). QcServer owns them and
// hands them out together through QcServer::systems(), for the headless game, tools and tests that
// drive or inspect one of them directly.
struct QcServerSystems
{
    // Fire (decision E-392): the step of the burning fires in the tick.
    QcServerFire &fire;
    // Flint and steel, dispensers and lava lighting fire (decision E-393).
    QcFireIgnition &ignition;
    // Players and creatures on fire.
    QcBodyBurning &burning;
    // Health and food of the players (decision E-367).
    QcPlayerHealth &health;
    // The weather, where it rains and snows, and lightning (decision E-395).
    QcServerWeather &weather;
    QcLightningStrike &lightning;
    QcSnowfall &snowfall;
    QcServerMinecarts &minecarts;
    // The players' experience and its orbs (decision E-398).
    QcServerExperience &experience;
    QcServerProjectiles &projectiles;
    // Signal Dust circuits (decision E-335).
    QcServerCircuits &circuits;
    // Explosives and explosions (decisions E-600 to E-604).
    QcServerExplosions &explosions;
    // Mining, using items and entities, and the swings while mining (decision E-881).
    QcServerInteraction &interaction;
};
