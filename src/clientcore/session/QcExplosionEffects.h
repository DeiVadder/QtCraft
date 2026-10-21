#pragma once

#include "common/core/QcVec3.h"
#include "common/protocol/QcWorldMessages.h"

#include <QList>

#include <optional>

// The flash and smoke of the explosions the server told about (QcMsg::Explosion, decision E-604):
// each explosion leaves a few bright puffs of fire that burst out and fade fast and more puffs of
// smoke that drift up and shrink, all as small cubes the entity frame draws. Directions and sizes
// come from the explosion's position, so the same explosion looks the same; the number of puffs grows
// with the power and is bounded, and so is the number of puffs in all.
class QcExplosionEffects
{
public:
    struct Puff
    {
        QcVec3 centre;
        // Metres per second of its burst.
        QcVec3 velocity;
        quint64 startTick = 0;
        int lifeTicks = 20;
        double size = 1.0;
        bool flash = false;
    };

    // What a puff looks like `ticks` after the explosion (with the frame's fraction of a tick).
    struct Look
    {
        QcVec3 position;
        double size = 0.0;
        bool flash = false;
    };

    static constexpr int flashPuffs = 4;
    static constexpr int smokePuffsPerPower = 4;
    static constexpr int maxPuffs = 512;
    // Content textures of the puffs (atlas names with the namespace); the campfire smoke
    // (QcCampfireSmoke) is drawn with the smoke texture too.
    static constexpr auto flashTexture = "qtcraft:explosion_flash";
    static constexpr auto smokeTexture = "qtcraft:explosion_smoke";

    void add(const QcMsg::Explosion &explosion, quint64 tick);
    // Forgets the puffs that are over at `tick`.
    void prune(quint64 tick);
    const QList<Puff> &puffs() const { return m_puffs; }
    // Nothing while the puff is over.
    static std::optional<Look> lookAt(const Puff &puff, double ticks);

private:
    QList<Puff> m_puffs;
};
